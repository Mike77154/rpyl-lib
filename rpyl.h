#ifndef RPYL_H
#define RPYL_H

#include <stddef.h> /* size_t */

#include "rpyl_alloc.h" /* optional alloc hooks */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpylContext RpylContext;

/* Optional compiled form (forward declared). */
struct RpylBytecode;

typedef void (*RpylCommandFn)(RpylContext* ctx, const char** args, int argc);

/* -----------------------------
   Virtual file system (optional)
   ----------------------------- */

typedef void*  (*RpylVfsOpenFn)(void* user, const char* path);
typedef size_t (*RpylVfsReadFn)(void* user, void* handle, void* dst, size_t bytes);
typedef int    (*RpylVfsCloseFn)(void* user, void* handle);

typedef struct RpylVfs {
    void* user;
    RpylVfsOpenFn open;
    RpylVfsReadFn read;
    RpylVfsCloseFn close;
} RpylVfs;

/* Install a VFS for rpyl_load_file(). If NULL, stdio fopen/fread is used. */
void rpyl_set_vfs(RpylContext* ctx, const RpylVfs* vfs);

/* -----------------------------
   Lifecycle
   ----------------------------- */

RpylContext* rpyl_create(void);

/* Create context whose AST arena's first block uses an external buffer.
   The buffer is NOT freed by rpyl_destroy(). */
RpylContext* rpyl_create_with_arena_buffer(void* buffer, size_t capacity);

void rpyl_destroy(RpylContext* ctx);

/* -----------------------------
   Script loading
   ----------------------------- */

int rpyl_load_file(RpylContext* ctx, const char* path);
int rpyl_load_buffer(RpylContext* ctx, const char* buffer, size_t len);

/* -----------------------------
   DSL config
   ----------------------------- */

void rpyl_set_label_keyword(RpylContext* ctx, const char* kw);
void rpyl_set_start_block(RpylContext* ctx, const char* block_name);

/* -----------------------------
   Commands
   ----------------------------- */

void rpyl_register_command(
    RpylContext* ctx,
    const char* name,
    RpylCommandFn fn
);

/* -----------------------------
   Execution (run-to-completion)
   ----------------------------- */

/* Return codes for rpyl_run / rpyl_run_compiled:
   - RPYL_EXEC_ERROR   (0)
   - RPYL_EXEC_DONE    (1)
   - RPYL_EXEC_YIELDED (2)
*/
#define RPYL_EXEC_ERROR   0
#define RPYL_EXEC_DONE    1
#define RPYL_EXEC_YIELDED 2

/* Execute a block (AST interpreter). */
int rpyl_run(RpylContext* ctx, const char* entry_block);

/* -----------------------------
   Coroutine-style execution
   ----------------------------- */

/* Begin execution of a block and keep state for stepping.
   If bytecode is available, stepping uses bytecode VM; otherwise AST.
   Returns 1 on success, 0 on failure.
*/
int rpyl_begin(RpylContext* ctx, const char* entry_block);

/* Execute until the script either finishes or yields.
   Returns one of RPYL_EXEC_*.
*/
int rpyl_step(RpylContext* ctx);

/* Query whether a coroutine is currently active (begun but not finished). */
int rpyl_is_running(RpylContext* ctx);

/* Generic yield requested from inside a command handler.
   - token is an engine-defined identifier.
   - On the next step boundary, rpyl_step() will return RPYL_EXEC_YIELDED.
*/
void rpyl_yield(RpylContext* ctx, unsigned long token);

/* If currently yielded, returns the yield token; otherwise 0. */
unsigned long rpyl_get_yield_token(RpylContext* ctx);

/* Signal that a token is ready; if it matches the current yield token,
   the next rpyl_step() will resume execution.
*/
void rpyl_signal(RpylContext* ctx, unsigned long token);

/* -----------------------------
   Utilities
   ----------------------------- */

const char* rpyl_get_define(RpylContext* ctx, const char* name);

/* Variables */
const char* rpyl_get_var(RpylContext* ctx, const char* name);
void rpyl_set_var(RpylContext* ctx, const char* name, const char* value);

/* ================================ */
/* Bytecode (optional improvements) */
/* ================================ */

/* Compile the currently loaded script (AST) into bytecode.
   Returns non-zero on success. */
int rpyl_compile(RpylContext* ctx);

/* Execute the compiled bytecode (if available). If not compiled, returns 0.
   entry_block behaves like rpyl_run(). */
int rpyl_run_compiled(RpylContext* ctx, const char* entry_block);

/* Get compiled bytecode pointer (owned by ctx). */
const struct RpylBytecode* rpyl_get_bytecode(RpylContext* ctx);

/* Save/load bytecode to/from a .rpb file. */
int rpyl_save_bytecode(RpylContext* ctx, const char* out_path);
int rpyl_load_bytecode(RpylContext* ctx, const char* in_path);

/* Transpile compiled bytecode into a C89 source file embedding the program.
   symbol_prefix is used to name exported functions/objects. */
int rpyl_transpile_c(RpylContext* ctx, const char* out_c_path, const char* symbol_prefix);

/* Clone/install externally provided bytecode into ctx (useful for embedded/transpiled builds). */
int rpyl_use_bytecode(RpylContext* ctx, const struct RpylBytecode* bc);

#ifdef __cplusplus
}
#endif

#endif
