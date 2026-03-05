#include "rpyl_alloc.h"

#include <stdlib.h> /* malloc, realloc, free */
#include <string.h> /* memset */

static void* g_user = NULL;
static RpylMallocFn g_malloc = NULL;
static RpylReallocFn g_realloc = NULL;
static RpylFreeFn g_free = NULL;

static void* default_malloc(void* user, size_t size) {
    (void)user;
    return malloc(size);
}

static void* default_realloc(void* user, void* ptr, size_t size) {
    (void)user;
    return realloc(ptr, size);
}

static void default_free(void* user, void* ptr) {
    (void)user;
    free(ptr);
}

static void ensure_defaults(void) {
    if (!g_malloc) g_malloc = default_malloc;
    if (!g_realloc) g_realloc = default_realloc;
    if (!g_free) g_free = default_free;
}

void rpyl_set_allocators(RpylMallocFn m, RpylReallocFn r, RpylFreeFn f, void* user) {
    g_user = user;
    g_malloc = m ? m : default_malloc;
    g_realloc = r ? r : default_realloc;
    g_free = f ? f : default_free;
}

void* rpyl_alloc_user(void) {
    return g_user;
}

void* rpyl_malloc(size_t size) {
    ensure_defaults();
    if (size == 0) size = 1; /* match common malloc behavior */
    return g_malloc(g_user, size);
}

void* rpyl_realloc(void* ptr, size_t size) {
    ensure_defaults();
    if (size == 0) size = 1;
    return g_realloc(g_user, ptr, size);
}

void rpyl_free(void* ptr) {
    ensure_defaults();
    if (!ptr) return;
    g_free(g_user, ptr);
}

void* rpyl_calloc(size_t count, size_t size) {
    size_t total;
    void* p;

    if (count == 0 || size == 0) {
        return rpyl_malloc(1);
    }

    /* overflow check */
    total = count * size;
    if (size != 0 && total / size != count) {
        return NULL;
    }

    p = rpyl_malloc(total);
    if (p) memset(p, 0, total);
    return p;
}
