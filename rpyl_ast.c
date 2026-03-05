#include "rpyl_ast.h"


#include "rpyl_alloc.h"

#include <string.h>

AstNode* ast_new_ex(AstNodeType type, RpylArena* arena) {
    AstNode* n;
    if (arena) {
        n = (AstNode*)rpyl_arena_calloc(arena, sizeof(AstNode), 8);
        if (!n) return NULL;
        n->alloc_kind = (unsigned char)AST_ALLOC_ARENA;
    } else {
        n = (AstNode*)rpyl_calloc(1, sizeof(AstNode));
        if (!n) return NULL;
        n->alloc_kind = (unsigned char)AST_ALLOC_HEAP;
    }

    n->type = type;
    n->name[0] = 0;
    n->value[0] = 0;
    n->arg_count = 0;
    n->child_count = 0;
    n->children = NULL;
    n->next = NULL;
    return n;
}

AstNode* ast_new(AstNodeType type) {
    return ast_new_ex(type, (RpylArena*)0);
}

AstNode* ast_new_in_arena(AstNodeType type, RpylArena* arena) {
    return ast_new_ex(type, arena);
}

void ast_add_child(AstNode* parent, AstNode* child) {
    AstNode* n;
    if (!parent || !child) return;

    child->next = NULL;
    if (!parent->children) {
        parent->children = child;
    } else {
        n = parent->children;
        while (n->next) n = n->next;
        n->next = child;
    }
    parent->child_count++;
}

void ast_free(AstNode* node) {
    AstNode* child;
    AstNode* next;
    if (!node) return;

    child = node->children;
    while (child) {
        next = child->next;
        ast_free(child);
        child = next;
    }

    if (node->alloc_kind == (unsigned char)AST_ALLOC_HEAP) {
        rpyl_free(node);
    }
}
