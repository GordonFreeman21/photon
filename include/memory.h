/*
 * Photon - Memory Allocators and Pools
 */
#ifndef PHOTON_MEMORY_H
#define PHOTON_MEMORY_H

#include "photon_types.h"

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Linear Arena Allocator                                                    */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    void*   base_address;
    size_t  size;
    size_t  used;
    size_t  peak_used;
} LinearAllocator;

LinearAllocator arena_create(size_t size);
void            arena_destroy(LinearAllocator* arena);
void*           arena_alloc(LinearAllocator* arena, size_t size, size_t alignment);
void            arena_reset(LinearAllocator* arena);
float           arena_usage_percent(const LinearAllocator* arena);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Pool Allocator (fixed-size blocks)                                        */
/* ═══════════════════════════════════════════════════════════════════════════ */

typedef struct PoolFreeNode {
    struct PoolFreeNode* next;
} PoolFreeNode;

typedef struct {
    void*           base_address;
    PoolFreeNode*   free_list;
    size_t          block_size;
    size_t          block_count;
    size_t          allocated_count;
} PoolAllocator;

PoolAllocator   pool_create(size_t block_size, size_t block_count);
void            pool_destroy(PoolAllocator* pool);
void*           pool_alloc(PoolAllocator* pool);
void            pool_free(PoolAllocator* pool, void* ptr);
void            pool_reset(PoolAllocator* pool);

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Aligned allocation helpers                                                */
/* ═══════════════════════════════════════════════════════════════════════════ */

void*   photon_aligned_alloc(size_t size, size_t alignment);
void    photon_aligned_free(void* ptr);
void*   photon_realloc_array(void* ptr, size_t old_count, size_t new_count, size_t elem_size);

#endif /* PHOTON_MEMORY_H */
