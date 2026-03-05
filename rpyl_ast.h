#ifndef RPYL_AST_H
#define RPYL_AST_H

#include "rpyl_config.h"
#include "rpyl_arena.h"

typedef enum {
    AST_DEFINE,
    AST_BLOCK,
    AST_CALL
} AstNodeType;

typedef struct AstNode AstNode;

typedef enum {
    AST_ALLOC_HEAP = 0,
    AST_ALLOC_ARENA = 1
} AstAllocKind;

struct AstNode {
    AstNodeType type;
    char name[RPYL_AST_MAX_NAME];

    /* allocation info (so ast_free can be a no-op for arena nodes) */
    unsigned char alloc_kind;

    /* define */
    char value[RPYL_AST_MAX_VALUE];

    /* call */
    char args[RPYL_AST_MAX_ARGS][RPYL_AST_MAX_ARG_TEXT];
    int arg_count;

    /* block */
    AstNode* children;
    int child_count;

    AstNode* next;
};

/* Heap allocation (backwards compatible). */
AstNode* ast_new(AstNodeType type);

/* Arena allocation (preferred for performance). */
AstNode* ast_new_in_arena(AstNodeType type, RpylArena* arena);

/* Generic constructor: if arena is NULL, falls back to heap. */
AstNode* ast_new_ex(AstNodeType type, RpylArena* arena);
void ast_add_child(AstNode* parent, AstNode* child);
void ast_free(AstNode* node);

#endif
