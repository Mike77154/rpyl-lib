#ifndef RPYL_ARENA_H
#define RPYL_ARENA_H

/*
    rpyl_arena.h

    Simple growable arena allocator (C89).

    - Fast linear allocations.
    - Optional mark/rewind.
    - Frees everything at once.

    Notes:
    - Alignment is controlled by the caller; rpyl_arena_alloc aligns up to `align`.
*/

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpylArena RpylArena;

typedef struct {
    void* block;
    size_t offset;
} RpylArenaMark;

/* Create an arena with an initial capacity (bytes). If 0, a default is used.
   The arena grows with heap allocations using the active allocator hooks. */
RpylArena* rpyl_arena_create(size_t initial_capacity);

/* Create an arena backed by an externally provided buffer for its first block.
   - The arena will allocate additional blocks (via allocator hooks) if it grows beyond this buffer.
   - The external buffer is NOT freed by rpyl_arena_destroy(). */
RpylArena* rpyl_arena_create_with_buffer(void* buffer, size_t capacity);

/* Destroy the arena and free all blocks.
   NOTE: if you used rpyl_arena_create_with_buffer(), the external buffer is not freed. */
void rpyl_arena_destroy(RpylArena* arena);

/* Allocate size bytes aligned to `align` (must be power of 2, e.g. 8). Returns NULL on OOM. */
void* rpyl_arena_alloc(RpylArena* arena, size_t size, size_t align);

/* Allocate and zero memory. */
void* rpyl_arena_calloc(RpylArena* arena, size_t size, size_t align);

/* Mark current position. */
RpylArenaMark rpyl_arena_mark(RpylArena* arena);

/* Rewind to a previous mark (may free blocks allocated after the mark). */
void rpyl_arena_rewind(RpylArena* arena, RpylArenaMark mark);

/* Reset the arena to empty (keeps the first block, frees the rest). */
void rpyl_arena_reset(RpylArena* arena);

#ifdef __cplusplus
}
#endif

#endif /* RPYL_ARENA_H */
