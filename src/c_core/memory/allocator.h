#ifndef CA_ALLOCATOR_H
#define CA_ALLOCATOR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CA_ARENA_DEFAULT_SIZE  (1 * 1024 * 1024)
#define CA_ALIGN               32

#if defined(__GNUC__) || defined(__clang__)
    #define CA_CACHE_ALIGN __attribute__((aligned(64)))
#else
    #define CA_CACHE_ALIGN
#endif

typedef struct CA_CACHE_ALIGN {
    uint8_t*  base;
    size_t    capacity;
    size_t    offset;
    size_t    peak;
    uint32_t  alloc_count;
} ca_arena_t;

ca_arena_t* ca_arena_create(size_t capacity);
void        ca_arena_destroy(ca_arena_t* arena);
void        ca_arena_reset(ca_arena_t* arena);

void*       ca_arena_alloc(ca_arena_t* arena, size_t size);
void*       ca_arena_alloc_zero(ca_arena_t* arena, size_t size);
char*       ca_arena_strdup(ca_arena_t* arena, const char* str);

size_t      ca_arena_used(const ca_arena_t* arena);
size_t      ca_arena_remaining(const ca_arena_t* arena);
void        ca_arena_print_stats(const ca_arena_t* arena);

typedef size_t ca_arena_mark_t;
ca_arena_mark_t ca_arena_save(const ca_arena_t* arena);
void            ca_arena_restore(ca_arena_t* arena, ca_arena_mark_t mark);

#ifdef __cplusplus
}
#endif

#endif /* CA_ALLOCATOR_H */