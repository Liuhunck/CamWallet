#ifndef CAMWALLET_CUR_MEMORY_H
#define CAMWALLET_CUR_MEMORY_H

#include <stddef.h>
#include <stdlib.h>

#define CW_UR_ARENA_BYTES (16u * 1024u)
void *cw_ur_malloc(size_t size);
void *cw_ur_calloc(size_t count, size_t size);
void *cw_ur_realloc(void *ptr, size_t size);
void cw_ur_free(void *ptr);
size_t cw_ur_memory_used(void);
size_t cw_ur_memory_peak(void);

/* Force-included only when compiling upstream cUR. Its allocations, including
 * direct malloc/free calls, must all use the same bounded allocator. */
#ifdef CAMWALLET_CUR_ALLOCATOR
#define malloc cw_ur_malloc
#define calloc cw_ur_calloc
#define realloc cw_ur_realloc
#define free cw_ur_free
#endif
#endif
