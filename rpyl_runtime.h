#ifndef RPYL_RUNTIME_H
#define RPYL_RUNTIME_H

#include "rpyl_config.h"
#include "rpyl_ast.h"

/* Forward declare bytecode to avoid circular includes. */
struct RpylBytecode;

/* Contexto del runtime */

typedef struct RpylRuntime RpylRuntime;

/* Firma de comando */
typedef void (*RpylCmdFn)(
    RpylRuntime* rt,
    const char* name,
    const char** args,
    int argc
);

/* Crear / destruir runtime */
RpylRuntime* rpyl_runtime_create(void);
void rpyl_runtime_destroy(RpylRuntime* rt);

/* Optional userdata to let embedders associate a context with the runtime. */
void rpyl_runtime_set_userdata(RpylRuntime* rt, void* userdata);
void* rpyl_runtime_get_userdata(RpylRuntime* rt);

/* Registro de comandos */
int rpyl_runtime_register(
    RpylRuntime* rt,
    const char* command,
    RpylCmdFn fn
);

/* Acceso a defines */
const char* rpyl_runtime_get_define(
    RpylRuntime* rt,
    const char* name
);

/* (Re)load defines from an AST root (typically after parsing). */
void rpyl_runtime_load_defines(RpylRuntime* rt, AstNode* root);

/* Reset execution-related state (entered blocks, once cache, variables).
   Also stops any active stepped execution. */
void rpyl_runtime_reset_state(RpylRuntime* rt);

/* Simple variables (string -> string). */
const char* rpyl_runtime_get_var(RpylRuntime* rt, const char* name);
void rpyl_runtime_set_var(RpylRuntime* rt, const char* name, const char* value);

/* -----------------------------
   Generic pause / yield
   ----------------------------- */

/* Request a yield (typically from inside a command). */
void rpyl_runtime_request_yield(RpylRuntime* rt, unsigned long token);

/* If yielded, returns token; otherwise 0. */
unsigned long rpyl_runtime_get_yield_token(RpylRuntime* rt);

/* Signal that a token is ready. */
void rpyl_runtime_signal(RpylRuntime* rt, unsigned long token);

/* -----------------------------
   Execution (run-to-completion)
   ----------------------------- */

/* Ejecutar un bloque (AST interpreter).
   Returns:
     0 = error
     1 = done
     2 = yielded
*/
int rpyl_runtime_execute(
    RpylRuntime* rt,
    AstNode* root,
    const char* block_name
);

/* Execute compiled bytecode (optional fast path).
   Returns:
     0 = error
     1 = done
     2 = yielded
*/
int rpyl_runtime_execute_bytecode(
    RpylRuntime* rt,
    const struct RpylBytecode* bc,
    const char* entry_label
);

/* Load defines table from bytecode (so $define resolution works without AST). */
void rpyl_runtime_load_defines_from_bytecode(
    RpylRuntime* rt,
    const struct RpylBytecode* bc
);

/* -----------------------------
   Coroutine-style stepping
   ----------------------------- */

/* Prepare an AST execution for stepping. */
int rpyl_runtime_begin_ast(RpylRuntime* rt, AstNode* root, const char* entry_block);

/* Prepare a bytecode execution for stepping. */
int rpyl_runtime_begin_bytecode(RpylRuntime* rt, const struct RpylBytecode* bc, const char* entry_label);

/* Execute until either done or yielded.
   Returns:
     0 = error
     1 = done
     2 = yielded
*/
int rpyl_runtime_step(RpylRuntime* rt);

/* True if begin_* has been called and execution has not finished yet. */
int rpyl_runtime_is_running(RpylRuntime* rt);

#endif
