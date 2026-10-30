#include "cur_memory.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef union block {
    max_align_t alignment;
    struct { size_t size; bool used; } info;
} block_t;

/* Accessed on the main core only. A separate arena keeps fountain decoding
 * from consuming the camera/quirc heap. Adjacent free blocks are coalesced. */
static union { max_align_t alignment; uint8_t bytes[CW_UR_ARENA_BYTES]; } arena;
static bool initialized;
static size_t used_bytes;
static size_t peak_bytes;

void *cw_ur_malloc(size_t size)
{
    const size_t align = _Alignof(max_align_t);
    if (!size || size > CW_UR_ARENA_BYTES - sizeof(block_t)) return NULL;
    size = (size + align - 1u) / align * align;
    if (!initialized) {
        block_t *first = (block_t *)arena.bytes;
        first->info.size = sizeof(arena.bytes) - sizeof(block_t);
        first->info.used = false;
        initialized = true;
    }
    for (size_t pos = 0; pos < sizeof(arena.bytes); ) {
        block_t *block = (block_t *)(arena.bytes + pos);
        if (!block->info.used && block->info.size >= size) {
            if (block->info.size - size >= sizeof(block_t) + align) {
                block_t *next = (block_t *)((uint8_t *)(block + 1) + size);
                next->info.size = block->info.size - size - sizeof(block_t);
                next->info.used = false;
                block->info.size = size;
            }
            block->info.used = true;
            used_bytes += sizeof(block_t) + block->info.size;
            if (used_bytes > peak_bytes) peak_bytes = used_bytes;
            return block + 1;
        }
        pos += sizeof(block_t) + block->info.size;
    }
    return NULL;
}

void cw_ur_free(void *ptr)
{
    if (!ptr) return;
    block_t *block = (block_t *)ptr - 1;
    used_bytes -= sizeof(block_t) + block->info.size;
    block->info.used = false;
    for (size_t pos = 0; pos < sizeof(arena.bytes); ) {
        block = (block_t *)(arena.bytes + pos);
        size_t next_pos = pos + sizeof(block_t) + block->info.size;
        if (!block->info.used && next_pos < sizeof(arena.bytes)) {
            block_t *next = (block_t *)(arena.bytes + next_pos);
            if (!next->info.used) {
                block->info.size += sizeof(block_t) + next->info.size;
                continue;
            }
        }
        pos = next_pos;
    }
}

void *cw_ur_calloc(size_t count, size_t size)
{
    if (!size || count > SIZE_MAX / size) return NULL;
    void *ptr = cw_ur_malloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}

void *cw_ur_realloc(void *ptr, size_t size)
{
    if (!ptr) return cw_ur_malloc(size);
    if (!size) { cw_ur_free(ptr); return NULL; }
    block_t *block = (block_t *)ptr - 1;
    if (size <= block->info.size) return ptr;
    void *next = cw_ur_malloc(size);
    if (next) { memcpy(next, ptr, block->info.size); cw_ur_free(ptr); }
    return next;
}

size_t cw_ur_memory_used(void) { return used_bytes; }
size_t cw_ur_memory_peak(void) { return peak_bytes; }
