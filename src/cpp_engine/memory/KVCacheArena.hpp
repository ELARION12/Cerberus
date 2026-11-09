#pragma once

/*
 * KVCacheArena - a pool of page-aligned blocks in POSIX shared memory
 * ===================================================================
 * Hands out blocks of 4 KB pages (shm_open + mmap, with an anonymous mapping as
 * a fallback). A block is found by the 64-bit FNV-1a hash of a prompt prefix and
 * is reference counted: asking again for a prefix that already exists returns
 * the same block, and the pages are released when the last reference is dropped.
 *
 * Another process can map a block by its shared-memory name. Nothing in this
 * repository connects the pool to llama.cpp, vLLM or any other runtime, so no
 * attention cache is shared today. The reference counts are atomic, and a mutex
 * guards the lookup tables.
 */

#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <atomic>
#include <mutex>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

namespace geryon {

constexpr size_t CA_PAGE_SIZE = 4096; // Standard 4KB memory page

/* Block descriptor representing a single KV-cache page in memory */
struct PageBlock {
    uint32_t             block_id;
    size_t               page_count;
    size_t               size_bytes;
    uint8_t*             data_ptr;
    std::string          shm_name;
    uint64_t             prefix_hash;
    std::atomic<uint32_t> ref_count{1};

    PageBlock(uint32_t id, size_t num_pages, uint8_t* ptr, std::string_view name, uint64_t hash)
        : block_id(id), page_count(num_pages), size_bytes(num_pages * CA_PAGE_SIZE),
          data_ptr(ptr), shm_name(name), prefix_hash(hash) {}
};

class KVCacheArena {
public:
    /*
     * capacity_pages — Maximum number of 4KB pages to reserve for the arena pool
     * shm_prefix     — POSIX shared memory object prefix (e.g. "/geryon_kv_")
     */
    explicit KVCacheArena(size_t capacity_pages = 1024, std::string shm_prefix = "/geryon_kv_")
        : capacity_pages_(capacity_pages), shm_prefix_(std::move(shm_prefix)), next_block_id_(1)
    {
        total_capacity_bytes_ = capacity_pages_ * CA_PAGE_SIZE;
    }

    ~KVCacheArena() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [id, block] : blocks_) {
            if (block->data_ptr) {
                munmap(block->data_ptr, block->size_bytes);
            }
            if (!block->shm_name.empty()) {
                shm_unlink(block->shm_name.c_str());
            }
        }
        blocks_.clear();
        hash_table_.clear();
    }

    KVCacheArena(const KVCacheArena&) = delete;
    KVCacheArena& operator=(const KVCacheArena&) = delete;

    /*
     * Compute 64-bit FNV-1a hash of a token sequence string for prefix caching
     */
    static uint64_t hashPrefix(std::string_view tokens) {
        uint64_t hash = 14695981039346656037ULL;
        for (char c : tokens) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    /*
     * Allocate a paged KV-cache block for a given token sequence prefix
     */
    std::shared_ptr<PageBlock> allocateBlock(std::string_view token_prefix, size_t min_bytes) {
        uint64_t hash = hashPrefix(token_prefix);

        std::lock_guard<std::mutex> lock(mutex_);

        /* Check if prefix is already cached */
        auto hit = hash_table_.find(hash);
        if (hit != hash_table_.end()) {
            auto block = hit->second.lock();
            if (block) {
                block->ref_count.fetch_add(1, std::memory_order_relaxed);
                return block;
            }
        }

        size_t required_pages = (min_bytes + CA_PAGE_SIZE - 1) / CA_PAGE_SIZE;
        if (used_pages_ + required_pages > capacity_pages_) {
            throw std::runtime_error("[KVCacheArena] Out of page memory in arena pool");
        }

        uint32_t id = next_block_id_++;
        std::string shm_name = shm_prefix_ + std::to_string(id);
        size_t alloc_bytes = required_pages * CA_PAGE_SIZE;

        /* Create a POSIX shared memory object so another process can map this block by name */
        int fd = shm_open(shm_name.c_str(), O_CREAT | O_RDWR, 0666);
        if (fd < 0) {
            /* Fallback to anonymous memory if shm fails */
            uint8_t* anon_ptr = static_cast<uint8_t*>(mmap(nullptr, alloc_bytes,
                                PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
            if (anon_ptr == MAP_FAILED) {
                throw std::runtime_error("[KVCacheArena] Failed to allocate mmap page memory");
            }
            shm_name.clear();
            auto block = std::make_shared<PageBlock>(id, required_pages, anon_ptr, shm_name, hash);
            blocks_[id] = block;
            hash_table_[hash] = block;
            used_pages_ += required_pages;
            return block;
        }

        if (ftruncate(fd, alloc_bytes) != 0) {
            close(fd);
            shm_unlink(shm_name.c_str());
            throw std::runtime_error("[KVCacheArena] ftruncate failed for shm page block");
        }

        uint8_t* ptr = static_cast<uint8_t*>(mmap(nullptr, alloc_bytes,
                       PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        close(fd);

        if (ptr == MAP_FAILED) {
            shm_unlink(shm_name.c_str());
            throw std::runtime_error("[KVCacheArena] mmap failed for shm page block");
        }

        auto block = std::make_shared<PageBlock>(id, required_pages, ptr, shm_name, hash);
        blocks_[id] = block;
        hash_table_[hash] = block;
        used_pages_ += required_pages;

        return block;
    }

    /*
     * Fast prefix lookup: returns cached PageBlock if available, else nullptr
     */
    std::shared_ptr<PageBlock> lookupPrefix(std::string_view token_prefix) {
        uint64_t hash = hashPrefix(token_prefix);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = hash_table_.find(hash);
        if (it != hash_table_.end()) {
            auto block = it->second.lock();
            if (block) {
                block->ref_count.fetch_add(1, std::memory_order_relaxed);
                return block;
            }
        }
        return nullptr;
    }

    /*
     * Release a reference to a page block
     */
    void releaseBlock(uint32_t block_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = blocks_.find(block_id);
        if (it != blocks_.end()) {
            auto block = it->second;
            if (block->ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                hash_table_.erase(block->prefix_hash);
                if (block->data_ptr) {
                    munmap(block->data_ptr, block->size_bytes);
                    block->data_ptr = nullptr;
                }
                if (!block->shm_name.empty()) {
                    shm_unlink(block->shm_name.c_str());
                }
                used_pages_ -= block->page_count;
                blocks_.erase(it);
            }
        }
    }

    /* Stats & Accessors */
    size_t capacityPages() const { return capacity_pages_; }
    size_t usedPages()     const { return used_pages_; }
    size_t activeBlocks()  const { std::lock_guard<std::mutex> lock(mutex_); return blocks_.size(); }
    size_t bytesUsed()     const { return used_pages_ * CA_PAGE_SIZE; }

private:
    size_t capacity_pages_;
    size_t total_capacity_bytes_;
    size_t used_pages_{0};
    std::string shm_prefix_;
    uint32_t next_block_id_;

    mutable std::mutex mutex_;
    std::unordered_map<uint32_t, std::shared_ptr<PageBlock>> blocks_;
    std::unordered_map<uint64_t, std::weak_ptr<PageBlock>>   hash_table_;
};

} // namespace geryon
