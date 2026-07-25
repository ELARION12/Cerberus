#include "shared_memory.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

ca_shm_t* ca_shm_create(size_t capacity) {
    if (capacity == 0) capacity = 2 * 1024 * 1024;

    ca_shm_t* shm = (ca_shm_t*)calloc(1, sizeof(ca_shm_t));
    if (!shm) return NULL;

    shm->arena = ca_arena_create(capacity);
    if (!shm->arena) { free(shm); return NULL; }

    if (pthread_rwlock_init(&shm->rwlock, NULL) != 0) {
        ca_arena_destroy(shm->arena);
        free(shm);
        return NULL;
    }

    shm->head        = NULL;
    shm->entry_count = 0;
    return shm;
}

void ca_shm_destroy(ca_shm_t* shm) {
    if (!shm) return;
    pthread_rwlock_destroy(&shm->rwlock);
    ca_arena_destroy(shm->arena);
    free(shm);
}

void ca_shm_write(ca_shm_t* shm, const char* key,
                  const char* value, const char* author) {
    if (!shm || !key || !value) return;

    pthread_rwlock_wrlock(&shm->rwlock);

    ca_shm_entry_t* entry = shm->head;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            entry->value   = ca_arena_strdup(shm->arena, value);
            entry->author  = ca_arena_strdup(shm->arena, author ? author : "");
            entry->ts_ms   = now_ms();
            pthread_rwlock_unlock(&shm->rwlock);
            return;
        }
        entry = entry->next;
    }

    ca_shm_entry_t* node =
        (ca_shm_entry_t*)ca_arena_alloc(shm->arena, sizeof(ca_shm_entry_t));
    if (!node) {
        pthread_rwlock_unlock(&shm->rwlock);
        return;
    }

    node->key    = ca_arena_strdup(shm->arena, key);
    node->value  = ca_arena_strdup(shm->arena, value);
    node->author = ca_arena_strdup(shm->arena, author ? author : "");
    node->ts_ms  = now_ms();
    node->next   = shm->head;
    shm->head    = node;
    shm->entry_count++;

    pthread_rwlock_unlock(&shm->rwlock);
}

const char* ca_shm_read(ca_shm_t* shm, const char* key) {
    if (!shm || !key) return NULL;

    pthread_rwlock_rdlock(&shm->rwlock);

    const char* result = NULL;
    const ca_shm_entry_t* entry = shm->head;
    while (entry) {
        if (strcmp(entry->key, key) == 0) {
            result = entry->value;
            break;
        }
        entry = entry->next;
    }

    pthread_rwlock_unlock(&shm->rwlock);
    return result;
}

void ca_shm_summary(ca_shm_t* shm, ca_arena_t* scratch,
                    char** out, size_t* out_len) {
    if (!shm || !scratch || !out || !out_len) return;
    *out     = NULL;
    *out_len = 0;

    pthread_rwlock_rdlock(&shm->rwlock);

    size_t total = 0;
    const ca_shm_entry_t* entry = shm->head;
    while (entry) {
        total += strlen(entry->key) + 1
               + strlen(entry->value)
               + 6
               + strlen(entry->author)
               + 2;
        entry = entry->next;
    }

    if (total == 0) {
        pthread_rwlock_unlock(&shm->rwlock);
        return;
    }

    char* buf = (char*)ca_arena_alloc(scratch, total + 1);
    if (!buf) {
        pthread_rwlock_unlock(&shm->rwlock);
        return;
    }

    char* cursor = buf;
    entry = shm->head;
    while (entry) {
        int n = snprintf(cursor,
                         (size_t)(buf + total + 1 - cursor),
                         "%s=%s (by %s)\n",
                         entry->key, entry->value, entry->author);
        if (n > 0) cursor += n;
        entry = entry->next;
    }

    pthread_rwlock_unlock(&shm->rwlock);

    *out     = buf;
    *out_len = (size_t)(cursor - buf);
}

uint32_t ca_shm_count(const ca_shm_t* shm) {
    return shm ? shm->entry_count : 0;
}
