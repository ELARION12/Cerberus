#pragma once

#include <string>
#include <string_view>
#include <stdexcept>

#include "Arena.hpp"

extern "C" {
    #include "shared_memory.h"
}

namespace coreagent {

class SharedMemory {
public:
    explicit SharedMemory(size_t capacity = 2 * 1024 * 1024) {
        shm_ = ca_shm_create(capacity);
        if (!shm_)
            throw std::bad_alloc();
    }

    ~SharedMemory() {
        ca_shm_destroy(shm_);
    }

    SharedMemory(const SharedMemory&) = delete;
    SharedMemory& operator=(const SharedMemory&) = delete;

    void write(std::string_view key, std::string_view value,
               std::string_view author = "agent") {
        std::string k(key), v(value), a(author);
        ca_shm_write(shm_, k.c_str(), v.c_str(), a.c_str());
    }

    std::string read(std::string_view key,
                     std::string_view default_val = {}) const {
        std::string k(key);
        const char* val = ca_shm_read(shm_, k.c_str());
        return val ? std::string(val) : std::string(default_val);
    }

    std::string summary() const {
        Arena scratch(64 * 1024);
        ca_arena_t* raw_scratch = scratch.raw();

        char* buf     = nullptr;
        size_t length = 0;
        ca_shm_summary(shm_, raw_scratch, &buf, &length);

        if (!buf || length == 0) return {};
        return std::string(buf, length);
    }

    uint32_t count() const { return ca_shm_count(shm_); }

    ca_shm_t* raw() { return shm_; }

private:
    ca_shm_t* shm_;
};

} // namespace coreagent
