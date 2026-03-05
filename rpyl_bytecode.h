#ifndef RPYL_BYTECODE_H
#define RPYL_BYTECODE_H

/*
    rpyl_bytecode.h

    Tiny bytecode format for RPYL.

    Design goals:
    - C89-friendly (no stdint.h, no C99 features required).
    - Simple variable-length instruction stream.
    - String table for command names, arguments, labels, defines.

    Note:
    - This bytecode is meant as a *fast path*.
    - The AST interpreter remains available.
*/

#include <stddef.h> /* size_t */

#include "rpyl_ast.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long rpyl_u32;
typedef long rpyl_i32;

#define RPYL_BC_VERSION 1

typedef enum {
    RPYL_BC_OP_NOP = 0,

    /* Generic command dispatch: name + argv */
    RPYL_BC_OP_CMD = 1,

    /* Builtins */
    RPYL_BC_OP_SET = 2,       /* flags, var_sid, argc, value_sids... */
    RPYL_BC_OP_CALL = 3,      /* argc, arg_sids... */
    RPYL_BC_OP_JUMP = 4,      /* argc, arg_sids... */
    RPYL_BC_OP_RETURN = 5,

    /* Structured control */
    RPYL_BC_OP_ONCE_CHECK = 6,     /* once_id, skip_ip */
    RPYL_BC_OP_ON_ENTER_CHECK = 7, /* skip_ip */

    /* End of current label block */
    RPYL_BC_OP_END = 8
} RpylBcOp;

#define RPYL_BC_SET_GLOBAL 1

typedef struct {
    rpyl_u32 name_sid;
    rpyl_u32 ip;
    rpyl_u32 end_ip;
} RpylBcLabel;

typedef struct {
    rpyl_u32 name_sid;
    rpyl_u32 value_sid;
} RpylBcDefine;

typedef struct RpylBytecode {
    rpyl_u32 version;

    /* Instruction words */
    rpyl_u32* code;
    size_t code_count;
    size_t code_cap;

    /* Interned strings (NUL-terminated, heap-owned) */
    char** strings;
    size_t string_count;
    size_t string_cap;

    /* Labels */
    RpylBcLabel* labels;
    size_t label_count;
    size_t label_cap;

    /* Defines */
    RpylBcDefine* defines;
    size_t define_count;
    size_t define_cap;

    /* Internal: monotonically increasing once IDs */
    int next_once_id;
} RpylBytecode;

RpylBytecode* rpyl_bytecode_create(void);
void rpyl_bytecode_destroy(RpylBytecode* bc);
void rpyl_bytecode_clear(RpylBytecode* bc);

/* Compile from an AST root produced by rpyl_parse(). */
int rpyl_bytecode_compile_ast(RpylBytecode* out, AstNode* root);

/* Save / load the bytecode binary format (.rpb). */
int rpyl_bytecode_save(const RpylBytecode* bc, const char* path);
int rpyl_bytecode_load_into(RpylBytecode* bc, const char* path);

#ifdef __cplusplus
}
#endif

#endif /* RPYL_BYTECODE_H */
