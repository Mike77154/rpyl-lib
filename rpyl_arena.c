#include "rpyl_arena.h"

#include "rpyl_alloc.h"

#include <string.h> /* memset */

/* Internal block */
typedef struct RpylArenaBlock {
    struct RpylArenaBlock* next;
    size_t capacity;
    size_t offset;

    unsigned char* data; /* points to a region of size `capacity` */
    int owns_data;       /* 1 if `data` is owned by this block allocation */
} RpylArenaBlock;

struct RpylArena {
    RpylArenaBlock* head;
    RpylArenaBlock* tail;
};

static size_t rpyl_align_up(size_t v, size_t align) {
    size_t mask;
    if (align == 0) return v;
    mask = align - 1;
    return (v + mask) & ~mask;
}

static RpylArenaBlock* rpyl_block_create_owned(size_t cap) {
    RpylArenaBlock* b;
    size_t alloc_sz;

    if (cap < 1024) cap = 1024;

    /* one allocation: block header + data */
    alloc_sz = sizeof(RpylArenaBlock) + cap;
    b = (RpylArenaBlock*)rpyl_malloc(alloc_sz);
    if (!b) return NULL;

    b->next = NULL;
    b->capacity = cap;
    b->offset = 0;
    b->owns_data = 1;
    b->data = (unsigned char*)(b + 1);
    return b;
}

static RpylArenaBlock* rpyl_block_create_external(void* buffer, size_t cap) {
    RpylArenaBlock* b;
    if (!buffer || cap == 0) return NULL;

    b = (RpylArenaBlock*)rpyl_malloc(sizeof(RpylArenaBlock));
    if (!b) return NULL;

    b->next = NULL;
    b->capacity = cap;
    b->offset = 0;
    b->owns_data = 0;
    b->data = (unsigned char*)buffer;
    return b;
}

static void rpyl_block_destroy(RpylArenaBlock* b) {
    if (!b) return;
    /* If owns_data == 1, the data lives in the same allocation as the header. */
    rpyl_free(b);
}

RpylArena* rpyl_arena_create(size_t initial_capacity) {
    RpylArena* a;
    RpylArenaBlock* b;

    a = (RpylArena*)rpyl_malloc(sizeof(RpylArena));
    if (!a) return NULL;

    if (initial_capacity == 0) initial_capacity = 64 * 1024;
    b = rpyl_block_create_owned(initial_capacity);
    if (!b) {
        rpyl_free(a);
        return NULL;
    }

    a->head = b;
    a->tail = b;
    return a;
}

RpylArena* rpyl_arena_create_with_buffer(void* buffer, size_t capacity) {
    RpylArena* a;
    RpylArenaBlock* b;

    a = (RpylArena*)rpyl_malloc(sizeof(RpylArena));
    if (!a) return NULL;

    b = rpyl_block_create_external(buffer, capacity);
    if (!b) {
        rpyl_free(a);
        return NULL;
    }

    a->head = b;
    a->tail = b;
    return a;
}

void rpyl_arena_destroy(RpylArena* arena) {
    RpylArenaBlock* b;
    RpylArenaBlock* next;
    if (!arena) return;

    b = arena->head;
    while (b) {
        next = b->next;
        rpyl_block_destroy(b);
        b = next;
    }

    arena->head = NULL;
    arena->tail = NULL;
    rpyl_free(arena);
}

void* rpyl_arena_alloc(RpylArena* arena, size_t size, size_t align) {
    RpylArenaBlock* b;
    size_t off;
    size_t new_off;
    unsigned char* p;

    if (!arena || size == 0) return NULL;
    if (align == 0) align = 8;

    b = arena->tail;
    if (!b) return NULL;

    off = rpyl_align_up(b->offset, align);
    new_off = off + size;

    if (new_off > b->capacity) {
        /* allocate a new block */
        size_t grow;
        RpylArenaBlock* nb;

        grow = b->capacity * 2;
        if (grow < size + align) grow = size + align;

        nb = rpyl_block_create_owned(grow);
        if (!nb) return NULL;

        b->next = nb;
        arena->tail = nb;
        b = nb;

        off = rpyl_align_up(b->offset, align);
        new_off = off + size;
        if (new_off > b->capacity) return NULL;
    }

    p = b->data + off;
    b->offset = new_off;
    return (void*)p;
}

void* rpyl_arena_calloc(RpylArena* arena, size_t size, size_t align) {
    void* p;
    p = rpyl_arena_alloc(arena, size, align);
    if (p) memset(p, 0, size);
    return p;
}

RpylArenaMark rpyl_arena_mark(RpylArena* arena) {
    RpylArenaMark m;
    m.block = NULL;
    m.offset = 0;
    if (!arena || !arena->tail) return m;
    m.block = (void*)arena->tail;
    m.offset = arena->tail->offset;
    return m;
}

void rpyl_arena_rewind(RpylArena* arena, RpylArenaMark mark) {
    RpylArenaBlock* b;

    if (!arena) return;

    /* If mark.block is NULL, full reset. */
    if (!mark.block) {
        rpyl_arena_reset(arena);
        return;
    }

    /* Find mark block and free everything after it. */
    b = arena->head;
    while (b && b != (RpylArenaBlock*)mark.block) {
        b = b->next;
    }

    if (!b) {
        /* Unknown mark: ignore. */
        return;
    }

    /* Free blocks after b */
    {
        RpylArenaBlock* to_free;
        RpylArenaBlock* next;
        to_free = b->next;
        b->next = NULL;
        while (to_free) {
            next = to_free->next;
            rpyl_block_destroy(to_free);
            to_free = next;
        }
    }

    arena->tail = b;
    if (mark.offset <= b->capacity) b->offset = mark.offset;
    else b->offset = b->capacity;
}

void rpyl_arena_reset(RpylArena* arena) {
    RpylArenaBlock* b;
    RpylArenaBlock* next;

    if (!arena) return;
    b = arena->head;
    if (!b) return;

    next = b->next;
    b->next = NULL;
    b->offset = 0;

    while (next) {
        RpylArenaBlock* nn;
        nn = next->next;
        rpyl_block_destroy(next);
        next = nn;
    }

    arena->tail = b;
}
