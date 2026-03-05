#ifndef RPYL_ALLOC_H
#define RPYL_ALLOC_H

/*
    rpyl_alloc.h

    Optional allocation hooks.

    - By default, RPYL uses malloc/realloc/free.
    - You may override the allocators globally via rpyl_set_allocators().

    Notes:
    - Hooks are process-global (not per-context) in this minimal C89 design.
    - Make sure the hooks are set BEFORE creating contexts/bytecode/arenas,
      and do not change them while objects allocated by the previous hooks
      are still alive.
*/

#include <stddef.h> /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

typedef void* (*RpylMallocFn)(void* user, size_t size);
typedef void* (*RpylReallocFn)(void* user, void* ptr, size_t size);
typedef void  (*RpylFreeFn)(void* user, void* ptr);

void rpyl_set_allocators(RpylMallocFn m, RpylReallocFn r, RpylFreeFn f, void* user);

void* rpyl_malloc(size_t size);
void* rpyl_calloc(size_t count, size_t size);
void* rpyl_realloc(void* ptr, size_t size);
void  rpyl_free(void* ptr);

void* rpyl_alloc_user(void);

#ifdef __cplusplus
}
#endif

#endif /* RPYL_ALLOC_H */
