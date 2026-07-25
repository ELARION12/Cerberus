#ifndef CA_THREAD_POOL_H
#define CA_THREAD_POOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CA_POOL_MAX_THREADS  32
#define CA_POOL_QUEUE_SIZE   512

typedef void (*ca_task_fn)(void* arg);

typedef struct {
    ca_task_fn  fn;
    void*       arg;
} ca_task_t;

typedef struct ca_thread_pool ca_thread_pool_t;

ca_thread_pool_t* ca_pool_create(uint32_t num_threads);
void              ca_pool_destroy(ca_thread_pool_t* pool);

int               ca_pool_submit(ca_thread_pool_t* pool,
                                 ca_task_fn fn,
                                 void* arg);
void              ca_pool_wait(ca_thread_pool_t* pool);

uint64_t          ca_pool_tasks_completed(const ca_thread_pool_t* pool);
uint32_t          ca_pool_active_threads(const ca_thread_pool_t* pool);

#ifdef __cplusplus
}
#endif

#endif /* CA_THREAD_POOL_H */