#include "thread_pool.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>


/* Circular fixed-capacity task queue shared by all workers. */
typedef struct {
    ca_task_t   tasks[CA_POOL_QUEUE_SIZE]; /* ring buffer of pending tasks */
    uint32_t    head;  /* next slot to dequeue (consumer index) */
    uint32_t    tail;  /* next slot to enqueue (producer index) */
    uint32_t    count; /* number of tasks currently in the queue */
} ca_task_queue_t;

struct ca_thread_pool {
    pthread_t         threads[CA_POOL_MAX_THREADS];
    uint32_t          num_threads;

    ca_task_queue_t   queue;

    pthread_mutex_t   mutex;          /* guards queue, active_count, shutdown */
    pthread_cond_t    task_available; /* signalled when a new task is enqueued */
    pthread_cond_t    all_done;       /* signalled when queue+active both hit 0 */

    uint32_t          active_count;   /* workers currently executing a task */
    uint64_t          completed_count;
    int               shutdown;       /* set to 1 to drain and stop workers */
    int               initialized;    /* 0 until threads are actually spawned */
};

/*
 * Worker thread: loops waiting for tasks, executes them outside the lock,
 * then signals all_done when both the queue and active count reach zero.
 */
static void* worker_fn(void* arg) {
    ca_thread_pool_t* pool = (ca_thread_pool_t*)arg;

    while (1) {
        pthread_mutex_lock(&pool->mutex);

        /* Block until there is work or a shutdown request arrives. */
        while (pool->queue.count == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->task_available, &pool->mutex);

        if (pool->shutdown && pool->queue.count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Dequeue: advance head pointer in the ring buffer. */
        ca_task_t task    = pool->queue.tasks[pool->queue.head];
        pool->queue.head  = (pool->queue.head + 1) % CA_POOL_QUEUE_SIZE;
        pool->queue.count--;
        pool->active_count++;

        pthread_mutex_unlock(&pool->mutex);

        task.fn(task.arg);  /* Execute outside the lock to maximise parallelism. */

        pthread_mutex_lock(&pool->mutex);
        pool->active_count--;
        pool->completed_count++;

        /* Notify ca_pool_wait() callers if everything is idle. */
        if (pool->queue.count == 0 && pool->active_count == 0)
            pthread_cond_broadcast(&pool->all_done);

        pthread_mutex_unlock(&pool->mutex);
    }

    return NULL;
}

ca_thread_pool_t* ca_pool_create(uint32_t num_threads) {
    if (num_threads == 0 || num_threads > CA_POOL_MAX_THREADS)
        return NULL;

    ca_thread_pool_t* pool = 
        (ca_thread_pool_t*)calloc(1, sizeof(ca_thread_pool_t));
    if (!pool) return NULL;

    pool->num_threads = num_threads;
    pool->shutdown    = 0;
    pool->initialized = 0;

    pthread_mutex_init(&pool->mutex,         NULL);
    pthread_cond_init (&pool->task_available, NULL);
    pthread_cond_init (&pool->all_done,       NULL);

    printf("[CoreAgent] Thread pool initialized (lazy loading active)\n");
    return pool;
}

void ca_pool_destroy(ca_thread_pool_t* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->task_available);
    pthread_mutex_unlock(&pool->mutex);

    if (pool->initialized) {
        for (uint32_t i = 0; i < pool->num_threads; i++)
            pthread_join(pool->threads[i], NULL);
    }

    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->task_available);
    pthread_cond_destroy(&pool->all_done);

    free(pool);
}

int ca_pool_submit(ca_thread_pool_t* pool, ca_task_fn fn, void* arg) {
    if (!pool || !fn) return -1;

    pthread_mutex_lock(&pool->mutex);
    /*
     * Lazy spawn: threads are created on the first submit() call rather than
     * at create() time, so pools that are never used cost almost nothing.
     * Stack size is reduced from the system default (usually 8MB) to 512KB
     * because agent tool callbacks don't need deep stacks.
     */
    if (!pool->initialized && !pool->shutdown) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 512 * 1024); /* 512KB per thread */

        for (uint32_t i = 0; i < pool->num_threads; i++) {
            if (pthread_create(&pool->threads[i], &attr, worker_fn, pool) != 0) {
                fprintf(stderr, "[CoreAgent] Failed to lazy-spawn thread %u\n", i);
            }
        }
        pthread_attr_destroy(&attr);
        pool->initialized = 1;
    }

    if (pool->queue.count >= CA_POOL_QUEUE_SIZE) {
        fprintf(stderr, "[CoreAgent] Task queue full!\n");
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    ca_task_t task = { .fn = fn, .arg = arg };
    pool->queue.tasks[pool->queue.tail] = task;
    pool->queue.tail  = (pool->queue.tail + 1) % CA_POOL_QUEUE_SIZE;
    pool->queue.count++;

    pthread_cond_signal(&pool->task_available);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

void ca_pool_wait(ca_thread_pool_t* pool) {
    if (!pool) return;

    pthread_mutex_lock(&pool->mutex);
    while (pool->queue.count > 0 || pool->active_count > 0)
        pthread_cond_wait(&pool->all_done, &pool->mutex);
    pthread_mutex_unlock(&pool->mutex);
}

uint64_t ca_pool_tasks_completed(const ca_thread_pool_t* pool) {
    return pool ? pool->completed_count : 0;
}

uint32_t ca_pool_active_threads(const ca_thread_pool_t* pool) {
    return pool ? pool->active_count : 0;
}