#include "rpyl.h"

#include "rpyl_config.h"
#include "rpyl_arena.h"
#include "rpyl_ast.h"
#include "rpyl_bytecode.h"
#include "rpyl_lexer.h"
#include "rpyl_parser.h"
#include "rpyl_port.h"
#include "rpyl_runtime.h"
#include "rpyl_transpile.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    char name[RPYL_CTX_MAX_CMD_NAME];
    RpylCommandFn fn;
} UserCommand;

struct RpylContext {
    RpylRuntime* rt;
    AstNode* root;

    /* --- Configuración del lenguaje --- */
    char start_block[RPYL_CTX_MAX_START_BLOCK];

    UserCommand cmds[RPYL_CTX_MAX_USER_COMMANDS];
    int cmd_count;

    /* --- IO --- */
    RpylVfs vfs;
    int has_vfs;

    /* --- Performance / toolchain --- */
    RpylArena* arena;          /* AST arena */
    RpylBytecode* bc;          /* compiled bytecode (optional) */
    int bc_valid;
};

static RpylCommandFn find_user_command(RpylContext* ctx, const char* name) {
    int i;
    if (!ctx || !name) return (RpylCommandFn)0;
    for (i = 0; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cmds[i].name, name) == 0) return ctx->cmds[i].fn;
    }
    return (RpylCommandFn)0;
}

/* Runtime callback signature (adapts runtime -> public command API). */
static void dispatch_command(
    RpylRuntime* rt,
    const char* name,
    const char** args,
    int argc
) {
    RpylContext* ctx;
    RpylCommandFn fn;

    ctx = (RpylContext*)rpyl_runtime_get_userdata(rt);
    fn = find_user_command(ctx, name);
    if (!fn) {
        fprintf(stderr, "[Rpyl] No handler bound for command: %s\n", name ? name : "(null)");
        return;
    }
    fn(ctx, args, argc);
}

/* -----------------------------
   VFS stream adapter
   ----------------------------- */

typedef struct {
    RpylVfs vfs;
    void* handle;

    unsigned char buf[RPYL_IO_VFS_CHUNK];
    size_t pos;
    size_t len;

    int has_push;
    int push;
} RpylVfsStream;

static int vfs_fill(RpylVfsStream* s) {
    if (!s || !s->vfs.read) return 0;
    s->pos = 0;
    s->len = s->vfs.read(s->vfs.user, s->handle, s->buf, sizeof(s->buf));
    return (s->len > 0) ? 1 : 0;
}

static int vfs_getc(void* user) {
    RpylVfsStream* s = (RpylVfsStream*)user;
    if (!s) return EOF;

    if (s->has_push) {
        s->has_push = 0;
        return s->push;
    }

    if (s->pos >= s->len) {
        if (!vfs_fill(s)) return EOF;
    }

    return (int)s->buf[s->pos++];
}

static int vfs_ungetc(int c, void* user) {
    RpylVfsStream* s = (RpylVfsStream*)user;
    if (!s) return EOF;
    if (c == EOF) return EOF;
    if (s->has_push) return EOF;
    s->has_push = 1;
    s->push = c;
    return c;
}

static RpylContext* rpyl_create_internal(void* arena_buffer, size_t arena_capacity) {
    RpylContext* ctx;

    ctx = (RpylContext*)rpyl_calloc(1, sizeof(RpylContext));
    if (!ctx) return NULL;

    ctx->rt = rpyl_runtime_create();
    if (!ctx->rt) {
        rpyl_free(ctx);
        return NULL;
    }

    if (arena_buffer && arena_capacity > 0) {
        ctx->arena = rpyl_arena_create_with_buffer(arena_buffer, arena_capacity);
    } else {
        ctx->arena = rpyl_arena_create(0);
    }

    if (!ctx->arena) {
        rpyl_runtime_destroy(ctx->rt);
        rpyl_free(ctx);
        return NULL;
    }

    ctx->bc = rpyl_bytecode_create();
    if (!ctx->bc) {
        rpyl_arena_destroy(ctx->arena);
        rpyl_runtime_destroy(ctx->rt);
        rpyl_free(ctx);
        return NULL;
    }

    rpyl_runtime_set_userdata(ctx->rt, ctx);
    ctx->root = NULL;
    ctx->cmd_count = 0;
    ctx->bc_valid = 0;
    ctx->has_vfs = 0;
    memset(&ctx->vfs, 0, sizeof(ctx->vfs));

    rpyl_strcpy_trunc(ctx->start_block, sizeof(ctx->start_block), "start");
    return ctx;
}

RpylContext* rpyl_create(void) {
    return rpyl_create_internal(NULL, 0);
}

RpylContext* rpyl_create_with_arena_buffer(void* buffer, size_t capacity) {
    return rpyl_create_internal(buffer, capacity);
}

void rpyl_destroy(RpylContext* ctx) {
    if (!ctx) return;

    if (ctx->root) {
        /* If nodes are arena-backed, ast_free is a no-op for those. */
        ast_free(ctx->root);
        ctx->root = NULL;
    }

    if (ctx->bc) {
        rpyl_bytecode_destroy(ctx->bc);
        ctx->bc = NULL;
        ctx->bc_valid = 0;
    }

    if (ctx->arena) {
        rpyl_arena_destroy(ctx->arena);
        ctx->arena = NULL;
    }

    if (ctx->rt) {
        rpyl_runtime_destroy(ctx->rt);
        ctx->rt = NULL;
    }

    rpyl_free(ctx);
}

void rpyl_set_vfs(RpylContext* ctx, const RpylVfs* vfs) {
    if (!ctx) return;

    if (!vfs || !vfs->open || !vfs->read) {
        ctx->has_vfs = 0;
        memset(&ctx->vfs, 0, sizeof(ctx->vfs));
        return;
    }

    ctx->vfs = *vfs;
    ctx->has_vfs = 1;
}

void rpyl_set_start_block(RpylContext* ctx, const char* block_name) {
    if (!ctx || !block_name || !block_name[0]) return;
    rpyl_strcpy_trunc(ctx->start_block, sizeof(ctx->start_block), block_name);
}

void rpyl_set_label_keyword(RpylContext* ctx, const char* kw) {
    (void)ctx;  /* por ahora el lexer es global */
    if (!kw || !kw[0]) return;
    rpyl_lexer_set_label_keyword(kw);
}

static void clear_loaded_program(RpylContext* ctx) {
    if (!ctx) return;

    /* New file/buffer => clear previous AST arena and bytecode. */
    if (ctx->arena) rpyl_arena_reset(ctx->arena);
    if (ctx->root) {
        ast_free(ctx->root);
        ctx->root = NULL;
    }
    if (ctx->bc) {
        rpyl_bytecode_clear(ctx->bc);
        ctx->bc_valid = 0;
    }
}

int rpyl_load_file(RpylContext* ctx, const char* path) {
    AstNode* root;

    if (!ctx || !path) return 0;

    clear_loaded_program(ctx);

    rpyl_lexer_reset();

    if (ctx->has_vfs) {
        RpylVfsStream vfs_s;
        RpylStream s;
        void* h;

        h = ctx->vfs.open(ctx->vfs.user, path);
        if (!h) {
            fprintf(stderr, "[Rpyl] VFS open failed: %s\n", path);
            return 0;
        }

        memset(&vfs_s, 0, sizeof(vfs_s));
        vfs_s.vfs = ctx->vfs;
        vfs_s.handle = h;
        vfs_s.pos = 0;
        vfs_s.len = 0;
        vfs_s.has_push = 0;
        vfs_s.push = 0;

        s.user = &vfs_s;
        s.getc_fn = vfs_getc;
        s.ungetc_fn = vfs_ungetc;

        root = rpyl_parse_stream_ex(&s, ctx->arena);

        if (ctx->vfs.close) {
            (void)ctx->vfs.close(ctx->vfs.user, h);
        }

        if (!root) {
            fprintf(stderr, "[Rpyl] Parse failed (VFS): %s\n", path);
            return 0;
        }

        ctx->root = root;

        /* New script: reset runtime execution state (entered blocks/once/vars) and reload defines. */
        rpyl_runtime_reset_state(ctx->rt);
        rpyl_runtime_load_defines(ctx->rt, ctx->root);
        return 1;
    }

    /* stdio path */
    {
        FILE* f;

        f = fopen(path, "rb");
        if (!f) {
            fprintf(stderr, "[Rpyl] Failed to open script: %s\n", path);
            return 0;
        }

        root = rpyl_parse_ex(f, ctx->arena);
        fclose(f);

        if (!root) {
            fprintf(stderr, "[Rpyl] Parse failed: %s\n", path);
            return 0;
        }

        ctx->root = root;

        /* New script: reset runtime execution state (entered blocks/once/vars) and reload defines. */
        rpyl_runtime_reset_state(ctx->rt);
        rpyl_runtime_load_defines(ctx->rt, ctx->root);
        return 1;
    }
}

int rpyl_load_buffer(RpylContext* ctx, const char* buffer, size_t len) {
    AstNode* root;

    if (!ctx || (!buffer && len > 0)) return 0;

    clear_loaded_program(ctx);
    rpyl_lexer_reset();

    root = rpyl_parse_buffer_ex(buffer ? buffer : "", len, ctx->arena);
    if (!root) {
        fprintf(stderr, "[Rpyl] Parse failed (buffer)\n");
        return 0;
    }

    ctx->root = root;

    rpyl_runtime_reset_state(ctx->rt);
    rpyl_runtime_load_defines(ctx->rt, ctx->root);
    return 1;
}

void rpyl_register_command(
    RpylContext* ctx,
    const char* name,
    RpylCommandFn fn
) {
    int i;

    if (!ctx || !name || !fn) return;

    /* Update existing binding if present. */
    for (i = 0; i < ctx->cmd_count; i++) {
        if (strcmp(ctx->cmds[i].name, name) == 0) {
            ctx->cmds[i].fn = fn;
            /* Ensure runtime has an entry too. */
            (void)rpyl_runtime_register(ctx->rt, name, dispatch_command);
            return;
        }
    }

    if (ctx->cmd_count >= RPYL_CTX_MAX_USER_COMMANDS) {
        fprintf(stderr, "[Rpyl] Too many commands registered (max %d)\n", (int)RPYL_CTX_MAX_USER_COMMANDS);
        return;
    }

    rpyl_strcpy_trunc(ctx->cmds[ctx->cmd_count].name, sizeof(ctx->cmds[ctx->cmd_count].name), name);
    ctx->cmds[ctx->cmd_count].fn = fn;
    ctx->cmd_count++;

    (void)rpyl_runtime_register(ctx->rt, name, dispatch_command);
}

int rpyl_run(RpylContext* ctx, const char* entry_block) {
    const char* entry;

    if (!ctx || !ctx->root) return RPYL_EXEC_ERROR;

    entry = entry_block ? entry_block : ctx->start_block;
    return rpyl_runtime_execute(ctx->rt, ctx->root, entry);
}

int rpyl_begin(RpylContext* ctx, const char* entry_block) {
    const char* entry;
    if (!ctx) return 0;

    entry = entry_block ? entry_block : ctx->start_block;

    if (ctx->bc_valid) {
        return rpyl_runtime_begin_bytecode(ctx->rt, ctx->bc, entry);
    }

    if (!ctx->root) return 0;
    return rpyl_runtime_begin_ast(ctx->rt, ctx->root, entry);
}

int rpyl_step(RpylContext* ctx) {
    if (!ctx) return RPYL_EXEC_ERROR;
    return rpyl_runtime_step(ctx->rt);
}

int rpyl_is_running(RpylContext* ctx) {
    if (!ctx) return 0;
    return rpyl_runtime_is_running(ctx->rt);
}

void rpyl_yield(RpylContext* ctx, unsigned long token) {
    if (!ctx) return;
    rpyl_runtime_request_yield(ctx->rt, token);
}

unsigned long rpyl_get_yield_token(RpylContext* ctx) {
    if (!ctx) return 0;
    return (unsigned long)rpyl_runtime_get_yield_token(ctx->rt);
}

void rpyl_signal(RpylContext* ctx, unsigned long token) {
    if (!ctx) return;
    rpyl_runtime_signal(ctx->rt, token);
}

const char* rpyl_get_define(RpylContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;
    return rpyl_runtime_get_define(ctx->rt, name);
}

const char* rpyl_get_var(RpylContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;
    return rpyl_runtime_get_var(ctx->rt, name);
}

void rpyl_set_var(RpylContext* ctx, const char* name, const char* value) {
    if (!ctx || !name) return;
    rpyl_runtime_set_var(ctx->rt, name, value ? value : "");
}

/* ================================ */
/* Bytecode API                      */
/* ================================ */

int rpyl_compile(RpylContext* ctx) {
    int ok;
    if (!ctx || !ctx->root || !ctx->bc) return 0;

    rpyl_bytecode_clear(ctx->bc);
    ok = rpyl_bytecode_compile_ast(ctx->bc, ctx->root);
    ctx->bc_valid = ok ? 1 : 0;

    /* Keep defines in sync for runtime substitution. */
    if (ok) {
        rpyl_runtime_load_defines(ctx->rt, ctx->root);
    }

    return ok;
}

int rpyl_run_compiled(RpylContext* ctx, const char* entry_block) {
    const char* entry;

    if (!ctx || !ctx->bc || !ctx->bc_valid) return RPYL_EXEC_ERROR;

    entry = entry_block ? entry_block : ctx->start_block;
    return rpyl_runtime_execute_bytecode(ctx->rt, ctx->bc, entry);
}

const RpylBytecode* rpyl_get_bytecode(RpylContext* ctx) {
    if (!ctx) return NULL;
    if (!ctx->bc_valid) return NULL;
    return (const RpylBytecode*)ctx->bc;
}

int rpyl_save_bytecode(RpylContext* ctx, const char* out_path) {
    if (!ctx || !out_path) return 0;
    if (!ctx->bc_valid) {
        if (!rpyl_compile(ctx)) return 0;
    }
    return rpyl_bytecode_save(ctx->bc, out_path);
}

int rpyl_load_bytecode(RpylContext* ctx, const char* in_path) {
    int ok;

    if (!ctx || !in_path || !ctx->bc) return 0;

    rpyl_bytecode_clear(ctx->bc);
    ok = rpyl_bytecode_load_into(ctx->bc, in_path);
    ctx->bc_valid = ok ? 1 : 0;

    /* This path does not provide an AST. */
    if (ok) {
        if (ctx->arena) rpyl_arena_reset(ctx->arena);
        if (ctx->root) {
            ast_free(ctx->root);
            ctx->root = NULL;
        }

        rpyl_runtime_reset_state(ctx->rt);
        rpyl_runtime_load_defines_from_bytecode(ctx->rt, ctx->bc);
    }

    return ok;
}

int rpyl_transpile_c(RpylContext* ctx, const char* out_c_path, const char* symbol_prefix) {
    if (!ctx || !out_c_path) return 0;
    if (!ctx->bc_valid) {
        if (!rpyl_compile(ctx)) return 0;
    }
    return rpyl_transpile_bytecode_to_c(ctx->bc, out_c_path, symbol_prefix);
}

int rpyl_use_bytecode(RpylContext* ctx, const RpylBytecode* bc) {
    size_t i;
    int ok;

    if (!ctx || !bc || !ctx->bc) return 0;

    rpyl_bytecode_clear(ctx->bc);

    /* clone strings */
    ok = 0;

    if (bc->string_count > 0) {
        ctx->bc->strings = (char**)rpyl_malloc(bc->string_count * sizeof(char*));
        if (!ctx->bc->strings) goto fail;
        ctx->bc->string_cap = bc->string_count;
        ctx->bc->string_count = 0;

        for (i = 0; i < bc->string_count; i++) {
            const char* s;
            size_t n;
            char* cp;

            s = (bc->strings && bc->strings[i]) ? bc->strings[i] : "";
            n = strlen(s);
            cp = (char*)rpyl_malloc(n + 1);
            if (!cp) goto fail;
            memcpy(cp, s, n + 1);
            ctx->bc->strings[ctx->bc->string_count++] = cp;
        }
    }

    /* clone code */
    if (bc->code_count > 0) {
        ctx->bc->code = (rpyl_u32*)rpyl_malloc(bc->code_count * sizeof(rpyl_u32));
        if (!ctx->bc->code) goto fail;
        memcpy(ctx->bc->code, bc->code, bc->code_count * sizeof(rpyl_u32));
        ctx->bc->code_count = bc->code_count;
        ctx->bc->code_cap = bc->code_count;
    }

    /* clone labels */
    if (bc->label_count > 0) {
        ctx->bc->labels = (RpylBcLabel*)rpyl_malloc(bc->label_count * sizeof(RpylBcLabel));
        if (!ctx->bc->labels) goto fail;
        memcpy(ctx->bc->labels, bc->labels, bc->label_count * sizeof(RpylBcLabel));
        ctx->bc->label_count = bc->label_count;
        ctx->bc->label_cap = bc->label_count;
    }

    /* clone defines */
    if (bc->define_count > 0) {
        ctx->bc->defines = (RpylBcDefine*)rpyl_malloc(bc->define_count * sizeof(RpylBcDefine));
        if (!ctx->bc->defines) goto fail;
        memcpy(ctx->bc->defines, bc->defines, bc->define_count * sizeof(RpylBcDefine));
        ctx->bc->define_count = bc->define_count;
        ctx->bc->define_cap = bc->define_count;
    }

    ctx->bc->version = bc->version;
    ctx->bc->next_once_id = 1;
    ctx->bc_valid = 1;

    /* Drop AST: we are now bytecode-backed. */
    if (ctx->arena) rpyl_arena_reset(ctx->arena);
    if (ctx->root) {
        ast_free(ctx->root);
        ctx->root = NULL;
    }

    rpyl_runtime_reset_state(ctx->rt);
    rpyl_runtime_load_defines_from_bytecode(ctx->rt, ctx->bc);
    ok = 1;
    return ok;

fail:
    rpyl_bytecode_clear(ctx->bc);
    ctx->bc_valid = 0;
    return 0;
}
