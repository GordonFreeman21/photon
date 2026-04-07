/*
 * Photon - Memory Allocators and Pools
 * Implementation of memory allocation subsystems
 */

#include "../include/memory.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Platform detection for aligned allocation */
#ifdef _WIN32
    #include <malloc.h>
    #define PLATFORM_WIN32 1
#else
    #define PLATFORM_WIN32 0
#endif

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Linear Arena Allocator Implementation                                      */
/* ═══════════════════════════════════════════════════════════════════════════ */

LinearAllocator arena_create(size_t size) {
    assert(size > 0 && "Arena size must be greater than 0");
    
    LinearAllocator arena = {0};
    arena.base_address = malloc(size);
    
    if (arena.base_address == NULL) {
        arena.size = 0;
        arena.used = 0;
        arena.peak_used = 0;
        return arena;
    }
    
    arena.size = size;
    arena.used = 0;
    arena.peak_used = 0;
    
    return arena;
}

void arena_destroy(LinearAllocator* arena) {
    assert(arena != NULL && "Arena pointer is NULL");
    
    if (arena->base_address != NULL) {
        free(arena->base_address);
        arena->base_address = NULL;
    }
    
    arena->size = 0;
    arena->used = 0;
    arena->peak_used = 0;
}

void* arena_alloc(LinearAllocator* arena, size_t size, size_t alignment) {
    assert(arena != NULL && "Arena pointer is NULL");
    assert(size > 0 && "Allocation size must be greater than 0");
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0 && "Alignment must be a power of 2");
    assert(arena->base_address != NULL && "Arena is not initialized");
    
    /* Align the current offset */
    size_t offset = arena->used;
    size_t aligned_offset = (offset + alignment - 1) & ~(alignment - 1);
    size_t padding = aligned_offset - offset;
    
    /* Check if allocation fits in arena */
    if (aligned_offset + size > arena->size) {
        return NULL;  /* Allocation failed: out of memory */
    }
    
    /* Get pointer to aligned address */
    void* ptr = (void*)((uintptr_t)arena->base_address + aligned_offset);
    
    /* Bump the used pointer */
    arena->used = aligned_offset + size;
    
    /* Track peak usage */
    if (arena->used > arena->peak_used) {
        arena->peak_used = arena->used;
    }
    
    return ptr;
}

void arena_reset(LinearAllocator* arena) {
    assert(arena != NULL && "Arena pointer is NULL");
    arena->used = 0;
}

float arena_usage_percent(const LinearAllocator* arena) {
    assert(arena != NULL && "Arena pointer is NULL");
    
    if (arena->size == 0) {
        return 0.0f;
    }
    
    return (float)arena->used / (float)arena->size * 100.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Pool Allocator Implementation                                              */
/* ═══════════════════════════════════════════════════════════════════════════ */

PoolAllocator pool_create(size_t block_size, size_t block_count) {
    assert(block_size > 0 && "Block size must be greater than 0");
    assert(block_count > 0 && "Block count must be greater than 0");
    
    PoolAllocator pool = {0};
    
    /* Minimum block size is sizeof(PoolFreeNode*) to store the linked list pointer */
    size_t min_block_size = sizeof(PoolFreeNode*);
    if (block_size < min_block_size) {
        block_size = min_block_size;
    }
    
    /* Allocate contiguous memory for all blocks */
    size_t total_size = block_size * block_count;
    pool.base_address = malloc(total_size);
    
    if (pool.base_address == NULL) {
        pool.block_size = 0;
        pool.block_count = 0;
        pool.allocated_count = 0;
        pool.free_list = NULL;
        return pool;
    }
    
    pool.block_size = block_size;
    pool.block_count = block_count;
    pool.allocated_count = 0;
    
    /* Build free list linking all blocks */
    pool.free_list = (PoolFreeNode*)pool.base_address;
    PoolFreeNode* current = pool.free_list;
    
    for (size_t i = 0; i < block_count - 1; i++) {
        void* next_block = (void*)((uintptr_t)pool.base_address + (i + 1) * block_size);
        current->next = (PoolFreeNode*)next_block;
        current = current->next;
    }
    
    /* Last block's next pointer is NULL */
    current->next = NULL;
    
    return pool;
}

void pool_destroy(PoolAllocator* pool) {
    assert(pool != NULL && "Pool pointer is NULL");
    
    if (pool->base_address != NULL) {
        free(pool->base_address);
        pool->base_address = NULL;
    }
    
    pool->free_list = NULL;
    pool->block_size = 0;
    pool->block_count = 0;
    pool->allocated_count = 0;
}

void* pool_alloc(PoolAllocator* pool) {
    assert(pool != NULL && "Pool pointer is NULL");
    assert(pool->base_address != NULL && "Pool is not initialized");
    
    /* Check if free list is empty */
    if (pool->free_list == NULL) {
        return NULL;  /* Allocation failed: no free blocks */
    }
    
    /* Pop from free list */
    PoolFreeNode* node = pool->free_list;
    pool->free_list = node->next;
    
    /* Increment allocated count */
    pool->allocated_count++;
    
    return (void*)node;
}

void pool_free(PoolAllocator* pool, void* ptr) {
    assert(pool != NULL && "Pool pointer is NULL");
    assert(ptr != NULL && "Pointer to free is NULL");
    assert(pool->base_address != NULL && "Pool is not initialized");
    
    /* Verify pointer is within pool bounds */
    uintptr_t pool_base = (uintptr_t)pool->base_address;
    uintptr_t pool_end = pool_base + pool->block_size * pool->block_count;
    uintptr_t ptr_addr = (uintptr_t)ptr;
    
    assert(ptr_addr >= pool_base && ptr_addr < pool_end && "Pointer is not in pool");
    assert((ptr_addr - pool_base) % pool->block_size == 0 && "Pointer is not aligned to block boundary");
    
    /* Push back to free list */
    PoolFreeNode* node = (PoolFreeNode*)ptr;
    node->next = pool->free_list;
    pool->free_list = node;
    
    /* Decrement allocated count */
    if (pool->allocated_count > 0) {
        pool->allocated_count--;
    }
}

void pool_reset(PoolAllocator* pool) {
    assert(pool != NULL && "Pool pointer is NULL");
    assert(pool->base_address != NULL && "Pool is not initialized");
    
    /* Rebuild entire free list */
    pool->free_list = (PoolFreeNode*)pool->base_address;
    PoolFreeNode* current = pool->free_list;
    
    for (size_t i = 0; i < pool->block_count - 1; i++) {
        void* next_block = (void*)((uintptr_t)pool->base_address + (i + 1) * pool->block_size);
        current->next = (PoolFreeNode*)next_block;
        current = current->next;
    }
    
    /* Last block's next pointer is NULL */
    current->next = NULL;
    
    /* Reset allocated count */
    pool->allocated_count = 0;
}

/* ═══════════════════════════════════════════════════════════════════════════ */
/*  Aligned Allocation Helpers                                                 */
/* ═══════════════════════════════════════════════════════════════════════════ */

void* photon_aligned_alloc(size_t size, size_t alignment) {
    assert(size > 0 && "Size must be greater than 0");
    assert(alignment > 0 && (alignment & (alignment - 1)) == 0 && "Alignment must be a power of 2");
    
#if PLATFORM_WIN32
    return _aligned_malloc(size, alignment);
#else
    return aligned_alloc(alignment, size);
#endif
}

void photon_aligned_free(void* ptr) {
    if (ptr == NULL) {
        return;
    }
    
#if PLATFORM_WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

void* photon_realloc_array(void* ptr, size_t old_count, size_t new_count, size_t elem_size) {
    assert(new_count > 0 && "New count must be greater than 0");
    assert(elem_size > 0 && "Element size must be greater than 0");
    
    /* Calculate sizes */
    size_t old_size = old_count * elem_size;
    size_t new_size = new_count * elem_size;
    
    /* Allocate new memory */
    void* new_ptr = malloc(new_size);
    if (new_ptr == NULL) {
        return NULL;
    }
    
    /* Copy old data if it exists and old_count > 0 */
    if (ptr != NULL && old_count > 0) {
        size_t copy_size = old_size < new_size ? old_size : new_size;
        memcpy(new_ptr, ptr, copy_size);
        free(ptr);
    }
    
    return new_ptr;
}
