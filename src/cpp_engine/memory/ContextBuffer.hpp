#pragma once

#include <string_view>
#include <deque>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <algorithm>
#include <cstring>
#include <stdexcept>

extern "C" {
    #include "allocator.h"
}

namespace cerberus {

enum class MessageRole { SYSTEM, USER, ASSISTANT, TOOL };

struct Message {
    MessageRole      role;
    std::string_view content;
    std::string_view tool_name;
    uint64_t         timestamp_ms;
};

/*
 * ContextBuffer stores conversation messages in two equal-sized arenas
 * (retained_ / standby_) using a semispace copy-GC strategy:
 *   - New message content is allocated into retained_.
 *   - When retained_ is >75% full, compact() copies live messages into
 *     standby_, swaps the two arenas, and resets the old one.
 * A third arena (scratch_) is used solely for buildPrompt() output and is
 * reset before each call, so the prompt buffer is reused rather than grown.
 */
class ContextBuffer {
public:
    /* arena_capacity applies to BOTH retained_ and standby_ (ping-pong swap requires equal sizes).
     * scratch_capacity should be large enough for one full serialised prompt. */
    ContextBuffer(size_t arena_capacity, size_t scratch_capacity, size_t max_tokens = 4096)
        : max_tokens_(max_tokens), current_tokens_(0) {
        retained_ = ca_arena_create(arena_capacity);
        standby_  = ca_arena_create(arena_capacity);
        scratch_  = ca_arena_create(scratch_capacity);
        if (!retained_ || !standby_ || !scratch_) {
            throw std::bad_alloc();
        }
        scratch_mark_ = ca_arena_save(scratch_);
    }

    ~ContextBuffer() {
        if (retained_) ca_arena_destroy(retained_);
        if (standby_)  ca_arena_destroy(standby_);
        if (scratch_)  ca_arena_destroy(scratch_);
    }

    ContextBuffer(const ContextBuffer&) = delete;
    ContextBuffer& operator=(const ContextBuffer&) = delete;

    void addSystem   (std::string_view content) { push(MessageRole::SYSTEM, content); }
    void addUser     (std::string_view content) { push(MessageRole::USER, content); }
    void addAssistant(std::string_view content) { push(MessageRole::ASSISTANT, content); }

    void addToolResult(std::string_view tool_name, std::string_view content) {
        Message msg;
        msg.role         = MessageRole::TOOL;
        msg.content      = copyToArena(retained_, content);
        msg.tool_name    = copyToArena(retained_, tool_name);
        msg.timestamp_ms = nowMs();
        messages_.push_back(msg);
        current_tokens_ += estimateTokens(msg.content);
        trim();
    }

    /*
     * Serialise all messages into the scratch arena as a flat byte sequence.
     * The arena is restored to its saved mark first so each call overwrites the
     * previous prompt — only one prompt is live at a time, matching the
     * THINK → PLAN → ACT usage pattern.
     * Wire format: '[' ["TOOL:"] content '\n'  for each message.
     * Returned view points into arena memory; valid until the next buildPrompt() call.
     */
    std::string_view buildPrompt() {
        ca_arena_restore(scratch_, scratch_mark_);

        size_t total_len = 0;
        for (const auto& m : messages_) {
            total_len += m.content.size() + (m.role == MessageRole::TOOL ? 5 : 1) + 1;
        }

        char* out = static_cast<char*>(ca_arena_alloc(scratch_, total_len));
        if (!out) {
            throw std::runtime_error("ContextBuffer: scratch arena too small for buildPrompt()");
        }

        char* cursor = out;
        for (const auto& m : messages_) {
            *cursor++ = '[';
            if (m.role == MessageRole::TOOL) {
                std::memcpy(cursor, "TOOL:", 5);
                cursor += 5;
            }
            std::memcpy(cursor, m.content.data(), m.content.size());
            cursor += m.content.size();
            *cursor++ = '\n';
        }

        return std::string_view(out, static_cast<size_t>(cursor - out));
    }

    const std::deque<Message>& messages()     const { return messages_; }
    size_t                     messageCount() const { return messages_.size(); }
    size_t                     tokenCount()   const { return current_tokens_; }

    void clear() {
        messages_.clear();
        current_tokens_ = 0;
        ca_arena_reset(retained_);
    }

    size_t retainedBytesUsed() const { return ca_arena_used(retained_); }

    void printSummary() const {
        printf("[ContextBuffer] messages=%zu tokens=%zu retained_bytes=%zu/%zu\n",
               messages_.size(), current_tokens_,
               ca_arena_used(retained_), retained_->capacity);
    }

private:
    ca_arena_t* retained_;
    ca_arena_t* standby_;
    ca_arena_t* scratch_;
    ca_arena_mark_t scratch_mark_;

    std::deque<Message> messages_;
    size_t max_tokens_;
    size_t current_tokens_;

    std::string_view copyToArena(ca_arena_t* arena, std::string_view text) {
        if (text.empty()) return {};
        char* ptr = static_cast<char*>(ca_arena_alloc(arena, text.size()));
        if (!ptr) {
            compact();
            ptr = static_cast<char*>(ca_arena_alloc(arena, text.size()));
            if (!ptr) {
                throw std::runtime_error("ContextBuffer: retained arena OOM after compaction");
            }
        }
        std::memcpy(ptr, text.data(), text.size());
        return std::string_view(ptr, text.size());
    }

    void push(MessageRole role, std::string_view content) {
        Message msg;
        msg.role         = role;
        msg.content      = copyToArena(retained_, content);
        msg.timestamp_ms = nowMs();
        messages_.push_back(msg);
        current_tokens_ += estimateTokens(msg.content);
        trim();
    }

    size_t estimateTokens(std::string_view text) const { return (text.size() + 3) / 4; }

    void trim() {
        /* Evict oldest non-SYSTEM messages until token budget is satisfied. */
        while (current_tokens_ > max_tokens_ && messages_.size() > 1) {
            for (auto it = messages_.begin(); it != messages_.end(); ++it) {
                if (it->role != MessageRole::SYSTEM) {
                    current_tokens_ -= estimateTokens(it->content);
                    messages_.erase(it);
                    break;
                }
            }
        }
        /* Compact proactively when the arena is 75% full so OOM is the exception,
         * not the normal path. */
        if (ca_arena_used(retained_) > (retained_capacity() * 3) / 4) {
            compact();
        }
    }

    size_t retained_capacity() const { return retained_->capacity; }

    /*
     * Semispace compaction: copy all live message content into standby_,
     * rewriting string_view pointers to the new location, then swap arenas
     * and reset the now-empty old retained_ arena.
     */
    void compact() {
        ca_arena_reset(standby_);
        for (auto& m : messages_) {
            if (!m.content.empty()) {
                m.content = copyToArena(standby_, m.content);
            }
            if (!m.tool_name.empty()) {
                m.tool_name = copyToArena(standby_, m.tool_name);
            }
        }
        std::swap(retained_, standby_);
        ca_arena_reset(standby_); // old retained_, now empty of live refs
    }

    uint64_t nowMs() const {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }
};

} // namespace cerberus