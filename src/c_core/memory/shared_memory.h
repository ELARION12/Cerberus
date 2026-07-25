#ifndef CA_SHARED_MEMORY_H
#define CA_SHARED_MEMORY_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include "allocator.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ca_shm_entry {
    const char*           key;
    const char*           value;
    const char*           author;
    uint64_t              ts_ms;
    struct ca_shm_entry*  next;
} ca_shm_entry_t;

typedef struct {
    ca_arena_t*      arena;
    ca_shm_entry_t*  head;
    pthread_rwlock_t rwlock;
    uint32_t         entry_count;
} ca_shm_t;

ca_shm_t*   ca_shm_create (size_t capacity);
void        ca_shm_destroy(ca_shm_t* shm);
void        ca_shm_write  (ca_shm_t* shm, const char* key,
                            const char* value, const char* author);
const char* ca_shm_read   (ca_shm_t* shm, const char* key);
void        ca_shm_summary(ca_shm_t* shm, ca_arena_t* scratch,
                            char** out, size_t* out_len);
uint32_t    ca_shm_count  (const ca_shm_t* shm);

#ifdef __cplusplus
}
#endif

#endif /* CA_SHARED_MEMORY_H */
