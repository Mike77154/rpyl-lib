#include "rpyl_runtime.h"

#include "rpyl_bytecode.h"
#include "rpyl_port.h"
#include "rpyl_alloc.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* ===================== */
/* DATA                  */
/* ===================== */

typedef struct {
    char name[RPYL_RUNTIME_MAX_NAME];
    char value[RPYL_RUNTIME_MAX_VALUE];
} RpylDefine;

typedef struct {
    char name[RPYL_RUNTIME_MAX_NAME];
    RpylCmdFn fn;
} RpylCommand;

typedef struct {
    char name[RPYL_RUNTIME_MAX_NAME];
    char value[RPYL_RUNTIME_MAX_VALUE];
} RpylVar;

typedef struct {
    RpylVar vars[RPYL_RUNTIME_MAX_VARS_PER_SCOPE];
    int count;
} RpylScope;

typedef enum {
    FRAME_INLINE = 0, /* entering a nested AST_BLOCK (group) */
    FRAME_CALL   = 1  /* script-level call label */
} FrameKind;

typedef struct {
    FrameKind kind;
    AstNode* saved_block;
    AstNode* saved_next_stmt;
    int saved_entry_is_first;
} ControlFrame;

typedef struct {
    int saved_label_index;
    rpyl_u32 return_ip;
    int saved_entry_is_first;
} RpylBcFrame;

typedef enum {
    EXEC_NONE = 0,
    EXEC_AST  = 1,
    EXEC_BC   = 2
} ExecMode;

struct RpylRuntime {
    RpylDefine defines[RPYL_RUNTIME_MAX_DEFINES];
    int define_count;
    AstNode* defines_root;

    RpylCommand commands[RPYL_RUNTIME_MAX_COMMANDS];
    int command_count;

    /* Variables + scopes */
    RpylScope scopes[RPYL_RUNTIME_MAX_SCOPES];
    int scope_top; /* 0 = global */

    /* once cache (stores AST node pointers that already ran) */
    const AstNode* once_nodes[RPYL_RUNTIME_MAX_ONCE_NODES];
    int once_count;

    /* once cache for bytecode mode (stores integer IDs) */
    int once_ids[RPYL_RUNTIME_MAX_ONCE_NODES];
    int once_id_count;

    /* on_enter: which top-level blocks were entered at least once */
    char entered_blocks[RPYL_RUNTIME_MAX_ENTERED_BLOCKS][RPYL_RUNTIME_MAX_NAME];
    int entered_count;

    /* yield/pause */
    int yield_active;
    unsigned long yield_token;

    int signal_pending;
    unsigned long signal_token;

    /* coroutine execution state */
    ExecMode exec_mode;
    int exec_running;

    /* AST exec */
    AstNode* exec_root;
    AstNode* exec_current_block;
    AstNode* exec_stmt;
    int exec_entry_is_first;
    ControlFrame exec_stack[RPYL_RUNTIME_MAX_STACK];
    int exec_sp;

    /* Bytecode exec */
    const RpylBytecode* exec_bc;
    int bc_label_index;
    rpyl_u32 bc_ip;
    rpyl_u32 bc_end_ip;
    int bc_entry_is_first;
    RpylBcFrame bc_stack[RPYL_RUNTIME_MAX_STACK];
    int bc_sp;

    void* userdata;
};

/* ===================== */
/* UTILIDADES            */
/* ===================== */

static AstNode* find_block(AstNode* root, const char* name) {
    AstNode* n;
    if (!root || !name) return NULL;
    n = root->children;
    while (n) {
        if (n->type == AST_BLOCK && strcmp(n->name, name) == 0)
            return n;
        n = n->next;
    }
    return NULL;
}

static RpylCmdFn find_command(RpylRuntime* rt, const char* name) {
    int i;
    if (!rt || !name) return NULL;
    for (i = 0; i < rt->command_count; i++) {
        if (strcmp(rt->commands[i].name, name) == 0)
            return rt->commands[i].fn;
    }
    return NULL;
}

static int once_has(RpylRuntime* rt, const AstNode* node) {
    int i;
    for (i = 0; i < rt->once_count; i++) {
        if (rt->once_nodes[i] == node) return 1;
    }
    return 0;
}

static int once_has_id(RpylRuntime* rt, int id) {
    int i;
    if (!rt) return 0;
    for (i = 0; i < rt->once_id_count; i++) {
        if (rt->once_ids[i] == id) return 1;
    }
    return 0;
}

static void once_mark(RpylRuntime* rt, const AstNode* node) {
    if (!rt || !node) return;
    if (once_has(rt, node)) return;
    if (rt->once_count >= (int)(sizeof(rt->once_nodes) / sizeof(rt->once_nodes[0]))) return;
    rt->once_nodes[rt->once_count++] = node;
}

static void once_mark_id(RpylRuntime* rt, int id) {
    if (!rt) return;
    if (once_has_id(rt, id)) return;
    if (rt->once_id_count >= (int)(sizeof(rt->once_ids) / sizeof(rt->once_ids[0]))) return;
    rt->once_ids[rt->once_id_count++] = id;
}

static int entered_has(RpylRuntime* rt, const char* name) {
    int i;
    for (i = 0; i < rt->entered_count; i++) {
        if (strcmp(rt->entered_blocks[i], name) == 0) return 1;
    }
    return 0;
}

static int mark_entered_block(RpylRuntime* rt, const char* block_name) {
    if (!rt || !block_name) return 0;
    if (!entered_has(rt, block_name)) {
        if (rt->entered_count < (int)(sizeof(rt->entered_blocks) / sizeof(rt->entered_blocks[0]))) {
            rpyl_strcpy_trunc(rt->entered_blocks[rt->entered_count], sizeof(rt->entered_blocks[rt->entered_count]), block_name);
            rt->entered_count++;
            return 1; /* first time */
        }
        return 1; /* treat as first time even if cache is full */
    }
    return 0;
}

/* --------------------------------------------------------------------- */
/* Scopes/vars                                                           */
/* --------------------------------------------------------------------- */

static void scope_init(RpylScope* s) {
    if (!s) return;
    s->count = 0;
}

static void reset_scopes(RpylRuntime* rt) {
    int i;
    if (!rt) return;
    for (i = 0; i < (int)(sizeof(rt->scopes) / sizeof(rt->scopes[0])); i++) {
        scope_init(&rt->scopes[i]);
    }
    rt->scope_top = 0;
}

static void push_scope(RpylRuntime* rt) {
    if (!rt) return;
    if (rt->scope_top + 1 >= (int)(sizeof(rt->scopes) / sizeof(rt->scopes[0]))) {
        /* stack overflow; clamp */
        return;
    }
    rt->scope_top++;
    scope_init(&rt->scopes[rt->scope_top]);
}

static void pop_scope(RpylRuntime* rt) {
    if (!rt) return;
    if (rt->scope_top <= 0) return;
    scope_init(&rt->scopes[rt->scope_top]);
    rt->scope_top--;
}

static void unwind_to_global_scope(RpylRuntime* rt) {
    if (!rt) return;
    while (rt->scope_top > 0) {
        pop_scope(rt);
    }
}

static const char* scope_get(RpylScope* s, const char* name) {
    int i;
    if (!s || !name) return NULL;
    for (i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            return s->vars[i].value;
        }
    }
    return NULL;
}

static void scope_set(RpylScope* s, const char* name, const char* value) {
    int i;
    if (!s || !name) return;

    for (i = 0; i < s->count; i++) {
        if (strcmp(s->vars[i].name, name) == 0) {
            rpyl_strcpy_trunc(s->vars[i].value, sizeof(s->vars[i].value), value ? value : "");
            return;
        }
    }

    if (s->count >= (int)(sizeof(s->vars) / sizeof(s->vars[0]))) {
        return;
    }

    rpyl_strcpy_trunc(s->vars[s->count].name, sizeof(s->vars[s->count].name), name);
    rpyl_strcpy_trunc(s->vars[s->count].value, sizeof(s->vars[s->count].value), value ? value : "");
    s->count++;
}

static const char* get_var_internal(RpylRuntime* rt, const char* name) {
    int i;
    const char* v;

    if (!rt || !name) return NULL;
    if (rt->scope_top < 0) reset_scopes(rt);

    for (i = rt->scope_top; i >= 0; i--) {
        v = scope_get(&rt->scopes[i], name);
        if (v) return v;
    }

    return NULL;
}

const char* rpyl_runtime_get_var(RpylRuntime* rt, const char* name) {
    const char* v;
    const char* p;

    if (!rt || !name) return NULL;

    p = name;
    if (p[0] == '$') p = p + 1;

    v = get_var_internal(rt, p);
    if (v) return v;

    /* fallback: defines */
    return rpyl_runtime_get_define(rt, p);
}

void rpyl_runtime_set_var(RpylRuntime* rt, const char* name, const char* value) {
    const char* p;
    if (!rt || !name) return;

    p = name;
    if (p[0] == '$') p = p + 1;

    if (rt->scope_top < 0) reset_scopes(rt);
    scope_set(&rt->scopes[rt->scope_top], p, value ? value : "");
}

/* --------------------------------------------------------------------- */
/* Helpers                                                               */
/* --------------------------------------------------------------------- */

static void build_joined_name(RpylRuntime* rt, const char** parts, int count, char* out, size_t out_size) {
    int i;
    size_t len;
    (void)rt;

    if (!out || out_size == 0) return;

    out[0] = 0;
    len = 0;

    for (i = 0; i < count; i++) {
        const char* s = parts[i] ? parts[i] : "";
        size_t sl = strlen(s);

        if (i > 0) {
            if (len + 1 >= out_size) break;
            out[len++] = ' ';
            out[len] = 0;
        }

        if (sl == 0) continue;

        if (len + sl >= out_size) {
            size_t copy = out_size - len - 1;
            if (copy > 0) {
                memcpy(out + len, s, copy);
                len += copy;
                out[len] = 0;
            }
            break;
        }

        memcpy(out + len, s, sl);
        len += sl;
        out[len] = 0;
    }
}

static unsigned long parse_ul_token(const char* s) {
    unsigned long v;
    const unsigned char* p;

    if (!s) return 0;
    p = (const unsigned char*)s;
    if (!*p) return 0;

    /* allow leading spaces */
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) return 0;

    v = 0;
    if (!isdigit(*p)) return 0;

    while (*p && isdigit(*p)) {
        v = v * 10UL + (unsigned long)(*p - '0');
        p++;
    }
    return v;
}

static void dispatch_external(RpylRuntime* rt, const char* name, const char** args, int argc) {
    RpylCmdFn fn;

    if (!rt || !name) return;

    /* Built-in yield/pause command, available even without external binding.
       Usage:
         yield            -> token 0
         yield 123        -> token 123
         pause 45         -> token 45
    */
    if (strcmp(name, "yield") == 0 || strcmp(name, "pause") == 0) {
        unsigned long token = 0;
        if (argc >= 1) token = parse_ul_token(args[0]);
        rpyl_runtime_request_yield(rt, token);
        return;
    }

    fn = find_command(rt, name);
    if (!fn) {
        fprintf(stderr, "[Rpyl] Command not found: %s\n", name);
        return;
    }

    fn(rt, name, args, argc);
}

/* ===================== */
/* PUBLIC API            */
/* ===================== */

RpylRuntime* rpyl_runtime_create(void) {
    RpylRuntime* rt;

    rt = (RpylRuntime*)rpyl_calloc(1, sizeof(RpylRuntime));
    if (!rt) return NULL;

    rt->define_count = 0;
    rt->defines_root = NULL;

    rt->command_count = 0;

    reset_scopes(rt);

    rt->once_count = 0;
    rt->once_id_count = 0;

    rt->entered_count = 0;

    rt->yield_active = 0;
    rt->yield_token = 0;
    rt->signal_pending = 0;
    rt->signal_token = 0;

    rt->exec_mode = EXEC_NONE;
    rt->exec_running = 0;

    rt->exec_root = NULL;
    rt->exec_current_block = NULL;
    rt->exec_stmt = NULL;
    rt->exec_entry_is_first = 0;
    rt->exec_sp = 0;

    rt->exec_bc = NULL;
    rt->bc_label_index = -1;
    rt->bc_ip = 0;
    rt->bc_end_ip = 0;
    rt->bc_entry_is_first = 0;
    rt->bc_sp = 0;

    rt->userdata = NULL;

    return rt;
}

void rpyl_runtime_destroy(RpylRuntime* rt) {
    if (!rt) return;
    rpyl_free(rt);
}

void rpyl_runtime_set_userdata(RpylRuntime* rt, void* userdata) {
    if (!rt) return;
    rt->userdata = userdata;
}

void* rpyl_runtime_get_userdata(RpylRuntime* rt) {
    if (!rt) return NULL;
    return rt->userdata;
}

int rpyl_runtime_register(RpylRuntime* rt, const char* command, RpylCmdFn fn) {
    int i;

    if (!rt || !command || !fn) return 0;

    /* Update existing. */
    for (i = 0; i < rt->command_count; i++) {
        if (strcmp(rt->commands[i].name, command) == 0) {
            rt->commands[i].fn = fn;
            return 1;
        }
    }

    if (rt->command_count >= (int)(sizeof(rt->commands) / sizeof(rt->commands[0]))) {
        fprintf(stderr, "[Rpyl] Too many runtime commands (max %d)\n", (int)(sizeof(rt->commands) / sizeof(rt->commands[0])));
        return 0;
    }

    rpyl_strcpy_trunc(rt->commands[rt->command_count].name, sizeof(rt->commands[rt->command_count].name), command);
    rt->commands[rt->command_count].fn = fn;
    rt->command_count++;

    return 1;
}

const char* rpyl_runtime_get_define(RpylRuntime* rt, const char* name) {
    int i;
    if (!rt || !name) return NULL;

    for (i = 0; i < rt->define_count; i++) {
        if (strcmp(rt->defines[i].name, name) == 0)
            return rt->defines[i].value;
    }

    return NULL;
}

void rpyl_runtime_load_defines(RpylRuntime* rt, AstNode* root) {
    AstNode* n;

    if (!rt || !root) return;

    rt->define_count = 0;
    rt->defines_root = root;

    n = root->children;
    while (n) {
        if (n->type == AST_DEFINE) {
            if (rt->define_count < (int)(sizeof(rt->defines) / sizeof(rt->defines[0]))) {
                rpyl_strcpy_trunc(rt->defines[rt->define_count].name, sizeof(rt->defines[rt->define_count].name), n->name);
                rpyl_strcpy_trunc(rt->defines[rt->define_count].value, sizeof(rt->defines[rt->define_count].value), n->value);
                rt->define_count++;
            }
        }
        n = n->next;
    }
}

void rpyl_runtime_reset_state(RpylRuntime* rt) {
    if (!rt) return;

    reset_scopes(rt);

    rt->once_count = 0;
    rt->once_id_count = 0;

    rt->entered_count = 0;

    /* stop any active coroutine */
    rt->exec_mode = EXEC_NONE;
    rt->exec_running = 0;

    rt->exec_root = NULL;
    rt->exec_current_block = NULL;
    rt->exec_stmt = NULL;
    rt->exec_entry_is_first = 0;
    rt->exec_sp = 0;

    rt->exec_bc = NULL;
    rt->bc_label_index = -1;
    rt->bc_ip = 0;
    rt->bc_end_ip = 0;
    rt->bc_entry_is_first = 0;
    rt->bc_sp = 0;

    rt->yield_active = 0;
    rt->yield_token = 0;
    rt->signal_pending = 0;
    rt->signal_token = 0;
}

/* -----------------------------
   Yield API
   ----------------------------- */

void rpyl_runtime_request_yield(RpylRuntime* rt, unsigned long token) {
    if (!rt) return;
    rt->yield_active = 1;
    rt->yield_token = token;
}

unsigned long rpyl_runtime_get_yield_token(RpylRuntime* rt) {
    if (!rt) return 0;
    if (!rt->yield_active) return 0;
    return rt->yield_token;
}

void rpyl_runtime_signal(RpylRuntime* rt, unsigned long token) {
    if (!rt) return;
    rt->signal_pending = 1;
    rt->signal_token = token;
}

/* --------------------------------------------------------------------- */
/* exec_command (AST)                                                    */
/* --------------------------------------------------------------------- */

static int do_return(RpylRuntime* rt, ControlFrame* stack, int* sp,
                     AstNode** current_block, AstNode** stmt, int* entry_is_first) {
    ControlFrame fr;

    if (*sp <= 0) {
        /* return at top-level: end execution */
        *stmt = NULL;
        return 0;
    }

    fr = stack[--(*sp)];
    if (fr.kind == FRAME_CALL) {
        /* return from call label */
        pop_scope(rt);
    }

    *current_block = fr.saved_block;
    *entry_is_first = fr.saved_entry_is_first;
    *stmt = fr.saved_next_stmt;
    return 1;
}

static int exec_command(
    RpylRuntime* rt,
    AstNode* root,
    ControlFrame* stack,
    int* sp,
    AstNode** current_block,
    AstNode** stmt,
    int* entry_is_first,
    AstNode* node,
    const char* name,
    const char** args,
    int argc,
    AstNode* next_stmt
) {
    (void)node;

    /* set <name> [=] <value...> */
    if (strcmp(name, "set") == 0) {
        char vbuf[RPYL_RUNTIME_MAX_JOINED];
        const char* varname;

        if (argc < 2) {
            *stmt = next_stmt;
            return 1;
        }

        varname = args[0];
        if (varname[0] == '$') varname = varname + 1;
        build_joined_name(rt, args + 1, argc - 1, vbuf, sizeof(vbuf));
        scope_set(&rt->scopes[rt->scope_top], varname, vbuf);

        *stmt = next_stmt;
        return 1;
    }

    /* setg <name> [=] <value...>  (global) */
    if (strcmp(name, "setg") == 0 || strcmp(name, "global") == 0) {
        char vbuf[RPYL_RUNTIME_MAX_JOINED];
        const char* varname;
        if (argc < 2) {
            *stmt = next_stmt;
            return 1;
        }

        varname = args[0];
        if (varname[0] == '$') varname = varname + 1;
        build_joined_name(rt, args + 1, argc - 1, vbuf, sizeof(vbuf));
        scope_set(&rt->scopes[0], varname, vbuf);

        *stmt = next_stmt;
        return 1;
    }

    /* call <label...> */
    if (strcmp(name, "call") == 0) {
        char label[RPYL_RUNTIME_MAX_NAME];
        AstNode* target;

        if (argc < 1) {
            printf("[Rpyl] call: missing target label\n");
            *stmt = next_stmt;
            return 1;
        }

        build_joined_name(rt, args, argc, label, sizeof(label));
        target = find_block(root, label);
        if (!target) {
            printf("[Rpyl] call: label not found: %s\n", label);
            *stmt = next_stmt;
            return 1;
        }

        if (*sp >= RPYL_RUNTIME_MAX_STACK) {
            printf("[Rpyl] call stack overflow\n");
            *stmt = next_stmt;
            return 1;
        }

        /* Push call frame */
        stack[*sp].kind = FRAME_CALL;
        stack[*sp].saved_block = *current_block;
        stack[*sp].saved_next_stmt = next_stmt;
        stack[*sp].saved_entry_is_first = *entry_is_first;
        (*sp)++;

        /* New scope for the call */
        push_scope(rt);

        *current_block = target;
        *entry_is_first = mark_entered_block(rt, target->name);
        *stmt = target->children;
        return 1;
    }

    /* jump <label...> */
    if (strcmp(name, "jump") == 0 || strcmp(name, "goto") == 0) {
        char label[RPYL_RUNTIME_MAX_NAME];
        AstNode* target;

        if (argc < 1) {
            printf("[Rpyl] jump: missing target label\n");
            *stmt = next_stmt;
            return 1;
        }

        build_joined_name(rt, args, argc, label, sizeof(label));
        target = find_block(root, label);
        if (!target) {
            printf("[Rpyl] jump: label not found: %s\n", label);
            *stmt = next_stmt;
            return 1;
        }

        /* Clear all frames (non-returning transfer) */
        *sp = 0;

        /* Drop locals, keep globals */
        unwind_to_global_scope(rt);

        *current_block = target;
        *entry_is_first = mark_entered_block(rt, target->name);
        *stmt = target->children;
        return 1;
    }

    /* return */
    if (strcmp(name, "return") == 0) {
        return do_return(rt, stack, sp, current_block, stmt, entry_is_first);
    }

    /* Normal engine command */
    dispatch_external(rt, name, args, argc);
    *stmt = next_stmt;
    return 1;
}

/* --------------------------------------------------------------------- */
/* Run-to-completion (AST)                                               */
/* --------------------------------------------------------------------- */

int rpyl_runtime_execute(
    RpylRuntime* rt,
    AstNode* root,
    const char* block_name
) {
    ControlFrame stack[RPYL_RUNTIME_MAX_STACK];
    int sp = 0;

    AstNode* current_block;
    AstNode* stmt;
    int entry_is_first = 0;

    if (!rt || !root || !block_name) return 0;

    /* Clear previous yield/signal. */
    rt->yield_active = 0;
    rt->yield_token = 0;
    rt->signal_pending = 0;
    rt->signal_token = 0;

    /* Keep defines in sync with the AST root. */
    if (rt->defines_root != root) {
        rpyl_runtime_load_defines(rt, root);
    }

    current_block = find_block(root, block_name);
    if (!current_block) {
        printf("[Rpyl] Block not found: %s\n", block_name);
        return 0;
    }

    entry_is_first = mark_entered_block(rt, current_block->name);
    stmt = current_block->children;

    while (1) {
        if (rt->yield_active) {
            return 2;
        }

        if (!stmt) {
            if (sp <= 0) break;
            /* Pop one frame */
            {
                ControlFrame fr = stack[--sp];
                if (fr.kind == FRAME_CALL) {
                    /* auto-return at end of called label */
                    pop_scope(rt);
                }
                current_block = fr.saved_block;
                entry_is_first = fr.saved_entry_is_first;
                stmt = fr.saved_next_stmt;
                continue;
            }
        }

        {
            AstNode* node = stmt;
            AstNode* next_stmt = node->next;

            if (node->type == AST_CALL) {
                const char* args[RPYL_RUNTIME_MAX_ARGS];
                int argc = node->arg_count;
                int i;
                if (argc > (int)(sizeof(args) / sizeof(args[0]))) argc = (int)(sizeof(args) / sizeof(args[0]));
                for (i = 0; i < argc; i++) args[i] = node->args[i];

                (void)exec_command(rt, root, stack, &sp,
                                   &current_block, &stmt, &entry_is_first,
                                   node, node->name, args, argc, next_stmt);
                continue;
            }

            if (node->type == AST_BLOCK) {
                int do_exec = 1;

                if (strcmp(node->name, "on_enter") == 0) {
                    do_exec = entry_is_first ? 1 : 0;
                } else if (strcmp(node->name, "once") == 0) {
                    if (once_has(rt, node)) do_exec = 0;
                    else {
                        once_mark(rt, node);
                        do_exec = 1;
                    }
                }

                if (!do_exec || !node->children) {
                    stmt = next_stmt;
                    continue;
                }

                if (sp >= (int)(sizeof(stack) / sizeof(stack[0]))) {
                    printf("[Rpyl] inline stack overflow\n");
                    stmt = next_stmt;
                    continue;
                }

                /* Enter nested block: push inline frame */
                stack[sp].kind = FRAME_INLINE;
                stack[sp].saved_block = current_block;
                stack[sp].saved_next_stmt = next_stmt;
                stack[sp].saved_entry_is_first = entry_is_first;
                sp++;

                stmt = node->children;
                continue;
            }

            /* Unknown node: skip */
            stmt = next_stmt;
        }
    }

    return 1;
}

/* ====================================================================== */
/* Bytecode VM (run-to-completion)                                        */
/* ====================================================================== */

static int bc_label_find(const RpylBytecode* bc, const char* name) {
    size_t i;
    if (!bc || !name) return -1;
    for (i = 0; i < bc->label_count; i++) {
        rpyl_u32 sid;
        const char* ln;
        sid = bc->labels[i].name_sid;
        if ((size_t)sid >= bc->string_count) continue;
        ln = bc->strings[sid];
        if (ln && strcmp(ln, name) == 0) return (int)i;
    }
    return -1;
}

void rpyl_runtime_load_defines_from_bytecode(RpylRuntime* rt, const struct RpylBytecode* bc_) {
    const RpylBytecode* bc;
    size_t i;

    if (!rt) return;
    bc = (const RpylBytecode*)bc_;

    rt->define_count = 0;
    rt->defines_root = NULL;

    if (!bc) return;

    for (i = 0; i < bc->define_count && rt->define_count < (int)(sizeof(rt->defines) / sizeof(rt->defines[0])); i++) {
        rpyl_u32 ns;
        rpyl_u32 vs;
        const char* n;
        const char* v;

        ns = bc->defines[i].name_sid;
        vs = bc->defines[i].value_sid;
        if ((size_t)ns >= bc->string_count) continue;
        if ((size_t)vs >= bc->string_count) continue;

        n = bc->strings[ns];
        v = bc->strings[vs];
        if (!n) n = "";
        if (!v) v = "";

        rpyl_strcpy_trunc(rt->defines[rt->define_count].name, sizeof(rt->defines[rt->define_count].name), n);
        rpyl_strcpy_trunc(rt->defines[rt->define_count].value, sizeof(rt->defines[rt->define_count].value), v);
        rt->define_count++;
    }
}

int rpyl_runtime_execute_bytecode(
    RpylRuntime* rt,
    const struct RpylBytecode* bc_,
    const char* entry_label
) {
    const RpylBytecode* bc;
    int label_index;
    rpyl_u32 ip;
    rpyl_u32 end_ip;
    int entry_is_first;
    RpylBcFrame stack[RPYL_RUNTIME_MAX_STACK];
    int sp;

    if (!rt || !bc_ || !entry_label) return 0;

    /* Clear previous yield/signal. */
    rt->yield_active = 0;
    rt->yield_token = 0;
    rt->signal_pending = 0;
    rt->signal_token = 0;

    bc = (const RpylBytecode*)bc_;
    if (!bc->code || !bc->strings || !bc->labels) {
        fprintf(stderr, "[Rpyl] Bytecode missing tables\n");
        return 0;
    }

    label_index = bc_label_find(bc, entry_label);
    if (label_index < 0) {
        fprintf(stderr, "[Rpyl] Bytecode entry label not found: %s\n", entry_label);
        return 0;
    }

    ip = bc->labels[label_index].ip;
    end_ip = bc->labels[label_index].end_ip;
    if (ip > (rpyl_u32)bc->code_count || end_ip > (rpyl_u32)bc->code_count || ip > end_ip) {
        fprintf(stderr, "[Rpyl] Bytecode label range invalid: %s\n", entry_label);
        return 0;
    }

    entry_is_first = mark_entered_block(rt, entry_label);
    sp = 0;

    while (1) {
        if (rt->yield_active) {
            return 2;
        }

        /* End-of-label => implicit return/halt */
        if (ip >= end_ip) {
            if (sp > 0) {
                pop_scope(rt);
                sp--;
                label_index = stack[sp].saved_label_index;
                entry_is_first = stack[sp].saved_entry_is_first;
                ip = stack[sp].return_ip;
                end_ip = bc->labels[label_index].end_ip;
                continue;
            }
            break;
        }

        {
            rpyl_u32 op;
            op = bc->code[ip++];

            if (op == (rpyl_u32)RPYL_BC_OP_NOP) {
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_END) {
                ip = end_ip;
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_ONCE_CHECK) {
                rpyl_u32 once_id;
                rpyl_u32 skip_ip;
                if (ip + 2 > (rpyl_u32)bc->code_count) return 0;
                once_id = bc->code[ip++];
                skip_ip = bc->code[ip++];
                if (once_has_id(rt, (int)once_id)) {
                    ip = skip_ip;
                } else {
                    once_mark_id(rt, (int)once_id);
                }
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_ON_ENTER_CHECK) {
                rpyl_u32 skip_ip;
                if (ip + 1 > (rpyl_u32)bc->code_count) return 0;
                skip_ip = bc->code[ip++];
                if (!entry_is_first) ip = skip_ip;
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_RETURN) {
                if (sp > 0) {
                    pop_scope(rt);
                    sp--;
                    label_index = stack[sp].saved_label_index;
                    entry_is_first = stack[sp].saved_entry_is_first;
                    ip = stack[sp].return_ip;
                    end_ip = bc->labels[label_index].end_ip;
                } else {
                    ip = end_ip;
                }
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_CMD) {
                rpyl_u32 name_sid;
                rpyl_u32 argc;
                const char* argv[RPYL_RUNTIME_MAX_ARGS];
                int i;

                if (ip + 2 > (rpyl_u32)bc->code_count) return 0;
                name_sid = bc->code[ip++];
                argc = bc->code[ip++];
                if ((size_t)name_sid >= bc->string_count) return 0;

                if (argc > (rpyl_u32)(sizeof(argv) / sizeof(argv[0]))) argc = (rpyl_u32)(sizeof(argv) / sizeof(argv[0]));
                for (i = 0; i < (int)argc; i++) {
                    rpyl_u32 sid;
                    if (ip >= (rpyl_u32)bc->code_count) return 0;
                    sid = bc->code[ip++];
                    if ((size_t)sid >= bc->string_count) argv[i] = "";
                    else argv[i] = bc->strings[sid] ? bc->strings[sid] : "";
                }

                dispatch_external(rt, bc->strings[name_sid], argv, (int)argc);
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_SET) {
                rpyl_u32 flags;
                rpyl_u32 var_sid;
                rpyl_u32 argc;
                const char* argv[RPYL_RUNTIME_MAX_ARGS];
                char joined[RPYL_RUNTIME_MAX_JOINED];
                const char* var_name;
                int i;
                int global;

                if (ip + 3 > (rpyl_u32)bc->code_count) return 0;
                flags = bc->code[ip++];
                var_sid = bc->code[ip++];
                argc = bc->code[ip++];

                if ((size_t)var_sid >= bc->string_count) return 0;
                var_name = bc->strings[var_sid] ? bc->strings[var_sid] : "";
                if (var_name[0] == '$') var_name++;

                if (argc > (rpyl_u32)(sizeof(argv) / sizeof(argv[0]))) argc = (rpyl_u32)(sizeof(argv) / sizeof(argv[0]));
                for (i = 0; i < (int)argc; i++) {
                    rpyl_u32 sid;
                    if (ip >= (rpyl_u32)bc->code_count) return 0;
                    sid = bc->code[ip++];
                    if ((size_t)sid >= bc->string_count) argv[i] = "";
                    else argv[i] = bc->strings[sid] ? bc->strings[sid] : "";
                }

                build_joined_name(rt, argv, (int)argc, joined, sizeof(joined));
                global = (flags & (rpyl_u32)RPYL_BC_SET_GLOBAL) ? 1 : 0;

                if (rt->scope_top < 0) reset_scopes(rt);
                if (global) {
                    scope_set(&rt->scopes[0], var_name, joined);
                } else {
                    scope_set(&rt->scopes[rt->scope_top], var_name, joined);
                }
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_CALL || op == (rpyl_u32)RPYL_BC_OP_JUMP) {
                rpyl_u32 argc;
                const char* argv[RPYL_RUNTIME_MAX_ARGS];
                char target_name[RPYL_RUNTIME_MAX_JOINED];
                int i;
                int tgt_idx;

                if (ip + 1 > (rpyl_u32)bc->code_count) return 0;
                argc = bc->code[ip++];
                if (argc > (rpyl_u32)(sizeof(argv) / sizeof(argv[0]))) argc = (rpyl_u32)(sizeof(argv) / sizeof(argv[0]));
                for (i = 0; i < (int)argc; i++) {
                    rpyl_u32 sid;
                    if (ip >= (rpyl_u32)bc->code_count) return 0;
                    sid = bc->code[ip++];
                    if ((size_t)sid >= bc->string_count) argv[i] = "";
                    else argv[i] = bc->strings[sid] ? bc->strings[sid] : "";
                }

                build_joined_name(rt, argv, (int)argc, target_name, sizeof(target_name));
                tgt_idx = bc_label_find(bc, target_name);
                if (tgt_idx < 0) {
                    fprintf(stderr, "[Rpyl] Unknown label: %s\n", target_name);
                    continue;
                }

                if (op == (rpyl_u32)RPYL_BC_OP_CALL) {
                    if (sp >= (int)(sizeof(stack) / sizeof(stack[0]))) {
                        fprintf(stderr, "[Rpyl] call stack overflow\n");
                        continue;
                    }
                    stack[sp].saved_label_index = label_index;
                    stack[sp].return_ip = ip;
                    stack[sp].saved_entry_is_first = entry_is_first;
                    sp++;

                    push_scope(rt);

                    label_index = tgt_idx;
                    ip = bc->labels[label_index].ip;
                    end_ip = bc->labels[label_index].end_ip;
                    entry_is_first = mark_entered_block(rt, target_name);
                } else {
                    sp = 0;
                    unwind_to_global_scope(rt);

                    label_index = tgt_idx;
                    ip = bc->labels[label_index].ip;
                    end_ip = bc->labels[label_index].end_ip;
                    entry_is_first = mark_entered_block(rt, target_name);
                }

                continue;
            }

            fprintf(stderr, "[Rpyl] Unknown bytecode opcode: %lu\n", (unsigned long)op);
            return 0;
        }
    }

    return 1;
}

/* ====================================================================== */
/* Coroutine-style stepping                                               */
/* ====================================================================== */

int rpyl_runtime_is_running(RpylRuntime* rt) {
    if (!rt) return 0;
    return rt->exec_running ? 1 : 0;
}

int rpyl_runtime_begin_ast(RpylRuntime* rt, AstNode* root, const char* entry_block) {
    AstNode* current_block;

    if (!rt || !root || !entry_block) return 0;

    /* Clear yield/signal. */
    rt->yield_active = 0;
    rt->yield_token = 0;
    rt->signal_pending = 0;
    rt->signal_token = 0;

    /* Keep defines in sync with the AST root. */
    if (rt->defines_root != root) {
        rpyl_runtime_load_defines(rt, root);
    }

    current_block = find_block(root, entry_block);
    if (!current_block) {
        fprintf(stderr, "[Rpyl] Block not found: %s\n", entry_block);
        return 0;
    }

    rt->exec_mode = EXEC_AST;
    rt->exec_running = 1;

    rt->exec_root = root;
    rt->exec_current_block = current_block;
    rt->exec_entry_is_first = mark_entered_block(rt, current_block->name);
    rt->exec_stmt = current_block->children;

    rt->exec_sp = 0;

    return 1;
}

int rpyl_runtime_begin_bytecode(RpylRuntime* rt, const struct RpylBytecode* bc_, const char* entry_label) {
    const RpylBytecode* bc;
    int label_index;
    rpyl_u32 ip;
    rpyl_u32 end_ip;

    if (!rt || !bc_ || !entry_label) return 0;

    /* Clear yield/signal. */
    rt->yield_active = 0;
    rt->yield_token = 0;
    rt->signal_pending = 0;
    rt->signal_token = 0;

    bc = (const RpylBytecode*)bc_;
    if (!bc->code || !bc->strings || !bc->labels) {
        fprintf(stderr, "[Rpyl] Bytecode missing tables\n");
        return 0;
    }

    label_index = bc_label_find(bc, entry_label);
    if (label_index < 0) {
        fprintf(stderr, "[Rpyl] Bytecode entry label not found: %s\n", entry_label);
        return 0;
    }

    ip = bc->labels[label_index].ip;
    end_ip = bc->labels[label_index].end_ip;
    if (ip > (rpyl_u32)bc->code_count || end_ip > (rpyl_u32)bc->code_count || ip > end_ip) {
        fprintf(stderr, "[Rpyl] Bytecode label range invalid: %s\n", entry_label);
        return 0;
    }

    rt->exec_mode = EXEC_BC;
    rt->exec_running = 1;

    rt->exec_bc = bc;
    rt->bc_label_index = label_index;
    rt->bc_ip = ip;
    rt->bc_end_ip = end_ip;
    rt->bc_entry_is_first = mark_entered_block(rt, entry_label);
    rt->bc_sp = 0;

    return 1;
}

static int yield_gate(RpylRuntime* rt) {
    if (!rt) return 0;

    if (!rt->yield_active) return 0;

    /* If we have a matching signal, clear and continue. */
    if (rt->signal_pending && rt->signal_token == rt->yield_token) {
        rt->yield_active = 0;
        rt->yield_token = 0;
        rt->signal_pending = 0;
        rt->signal_token = 0;
        return 0;
    }

    return 1; /* still yielded */
}

static int step_ast(RpylRuntime* rt) {
    while (1) {
        if (rt->yield_active) return 2;

        if (!rt->exec_stmt) {
            if (rt->exec_sp <= 0) {
                rt->exec_running = 0;
                rt->exec_mode = EXEC_NONE;
                return 1;
            }
            {
                ControlFrame fr = rt->exec_stack[--rt->exec_sp];
                if (fr.kind == FRAME_CALL) {
                    pop_scope(rt);
                }
                rt->exec_current_block = fr.saved_block;
                rt->exec_entry_is_first = fr.saved_entry_is_first;
                rt->exec_stmt = fr.saved_next_stmt;
                continue;
            }
        }

        {
            AstNode* node = rt->exec_stmt;
            AstNode* next_stmt = node->next;

            if (node->type == AST_CALL) {
                const char* args[RPYL_RUNTIME_MAX_ARGS];
                int argc = node->arg_count;
                int i;

                if (argc > (int)(sizeof(args) / sizeof(args[0]))) argc = (int)(sizeof(args) / sizeof(args[0]));
                for (i = 0; i < argc; i++) args[i] = node->args[i];

                (void)exec_command(rt, rt->exec_root, rt->exec_stack, &rt->exec_sp,
                                   &rt->exec_current_block, &rt->exec_stmt, &rt->exec_entry_is_first,
                                   node, node->name, args, argc, next_stmt);
                continue;
            }

            if (node->type == AST_BLOCK) {
                int do_exec = 1;

                if (strcmp(node->name, "on_enter") == 0) {
                    do_exec = rt->exec_entry_is_first ? 1 : 0;
                } else if (strcmp(node->name, "once") == 0) {
                    if (once_has(rt, node)) do_exec = 0;
                    else {
                        once_mark(rt, node);
                        do_exec = 1;
                    }
                }

                if (!do_exec || !node->children) {
                    rt->exec_stmt = next_stmt;
                    continue;
                }

                if (rt->exec_sp >= RPYL_RUNTIME_MAX_STACK) {
                    fprintf(stderr, "[Rpyl] inline stack overflow\n");
                    rt->exec_stmt = next_stmt;
                    continue;
                }

                rt->exec_stack[rt->exec_sp].kind = FRAME_INLINE;
                rt->exec_stack[rt->exec_sp].saved_block = rt->exec_current_block;
                rt->exec_stack[rt->exec_sp].saved_next_stmt = next_stmt;
                rt->exec_stack[rt->exec_sp].saved_entry_is_first = rt->exec_entry_is_first;
                rt->exec_sp++;

                rt->exec_stmt = node->children;
                continue;
            }

            /* Unknown node */
            rt->exec_stmt = next_stmt;
        }
    }
}

static int step_bc(RpylRuntime* rt) {
    const RpylBytecode* bc;

    bc = rt->exec_bc;
    if (!bc) {
        rt->exec_running = 0;
        rt->exec_mode = EXEC_NONE;
        return 0;
    }

    while (1) {
        if (rt->yield_active) return 2;

        if (rt->bc_ip >= rt->bc_end_ip) {
            if (rt->bc_sp > 0) {
                pop_scope(rt);
                rt->bc_sp--;
                rt->bc_label_index = rt->bc_stack[rt->bc_sp].saved_label_index;
                rt->bc_entry_is_first = rt->bc_stack[rt->bc_sp].saved_entry_is_first;
                rt->bc_ip = rt->bc_stack[rt->bc_sp].return_ip;
                rt->bc_end_ip = bc->labels[rt->bc_label_index].end_ip;
                continue;
            }

            rt->exec_running = 0;
            rt->exec_mode = EXEC_NONE;
            return 1;
        }

        {
            rpyl_u32 op;
            op = bc->code[rt->bc_ip++];

            if (op == (rpyl_u32)RPYL_BC_OP_NOP) continue;

            if (op == (rpyl_u32)RPYL_BC_OP_END) {
                rt->bc_ip = rt->bc_end_ip;
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_ONCE_CHECK) {
                rpyl_u32 once_id;
                rpyl_u32 skip_ip;
                if (rt->bc_ip + 2 > (rpyl_u32)bc->code_count) return 0;
                once_id = bc->code[rt->bc_ip++];
                skip_ip = bc->code[rt->bc_ip++];
                if (once_has_id(rt, (int)once_id)) rt->bc_ip = skip_ip;
                else once_mark_id(rt, (int)once_id);
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_ON_ENTER_CHECK) {
                rpyl_u32 skip_ip;
                if (rt->bc_ip + 1 > (rpyl_u32)bc->code_count) return 0;
                skip_ip = bc->code[rt->bc_ip++];
                if (!rt->bc_entry_is_first) rt->bc_ip = skip_ip;
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_RETURN) {
                if (rt->bc_sp > 0) {
                    pop_scope(rt);
                    rt->bc_sp--;
                    rt->bc_label_index = rt->bc_stack[rt->bc_sp].saved_label_index;
                    rt->bc_entry_is_first = rt->bc_stack[rt->bc_sp].saved_entry_is_first;
                    rt->bc_ip = rt->bc_stack[rt->bc_sp].return_ip;
                    rt->bc_end_ip = bc->labels[rt->bc_label_index].end_ip;
                } else {
                    rt->bc_ip = rt->bc_end_ip;
                }
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_CMD) {
                rpyl_u32 name_sid;
                rpyl_u32 argc;
                const char* argv[RPYL_RUNTIME_MAX_ARGS];
                int i;

                if (rt->bc_ip + 2 > (rpyl_u32)bc->code_count) return 0;
                name_sid = bc->code[rt->bc_ip++];
                argc = bc->code[rt->bc_ip++];
                if ((size_t)name_sid >= bc->string_count) return 0;

                if (argc > (rpyl_u32)(sizeof(argv) / sizeof(argv[0]))) argc = (rpyl_u32)(sizeof(argv) / sizeof(argv[0]));
                for (i = 0; i < (int)argc; i++) {
                    rpyl_u32 sid;
                    if (rt->bc_ip >= (rpyl_u32)bc->code_count) return 0;
                    sid = bc->code[rt->bc_ip++];
                    if ((size_t)sid >= bc->string_count) argv[i] = "";
                    else argv[i] = bc->strings[sid] ? bc->strings[sid] : "";
                }

                dispatch_external(rt, bc->strings[name_sid], argv, (int)argc);
                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_SET) {
                rpyl_u32 flags;
                rpyl_u32 var_sid;
                rpyl_u32 argc;
                const char* argv[RPYL_RUNTIME_MAX_ARGS];
                char joined[RPYL_RUNTIME_MAX_JOINED];
                const char* var_name;
                int i;
                int global;

                if (rt->bc_ip + 3 > (rpyl_u32)bc->code_count) return 0;
                flags = bc->code[rt->bc_ip++];
                var_sid = bc->code[rt->bc_ip++];
                argc = bc->code[rt->bc_ip++];

                if ((size_t)var_sid >= bc->string_count) return 0;
                var_name = bc->strings[var_sid] ? bc->strings[var_sid] : "";
                if (var_name[0] == '$') var_name++;

                if (argc > (rpyl_u32)(sizeof(argv) / sizeof(argv[0]))) argc = (rpyl_u32)(sizeof(argv) / sizeof(argv[0]));
                for (i = 0; i < (int)argc; i++) {
                    rpyl_u32 sid;
                    if (rt->bc_ip >= (rpyl_u32)bc->code_count) return 0;
                    sid = bc->code[rt->bc_ip++];
                    if ((size_t)sid >= bc->string_count) argv[i] = "";
                    else argv[i] = bc->strings[sid] ? bc->strings[sid] : "";
                }

                build_joined_name(rt, argv, (int)argc, joined, sizeof(joined));
                global = (flags & (rpyl_u32)RPYL_BC_SET_GLOBAL) ? 1 : 0;

                if (rt->scope_top < 0) reset_scopes(rt);
                if (global) scope_set(&rt->scopes[0], var_name, joined);
                else scope_set(&rt->scopes[rt->scope_top], var_name, joined);

                continue;
            }

            if (op == (rpyl_u32)RPYL_BC_OP_CALL || op == (rpyl_u32)RPYL_BC_OP_JUMP) {
                rpyl_u32 argc;
                const char* argv[RPYL_RUNTIME_MAX_ARGS];
                char target_name[RPYL_RUNTIME_MAX_JOINED];
                int i;
                int tgt_idx;

                if (rt->bc_ip + 1 > (rpyl_u32)bc->code_count) return 0;
                argc = bc->code[rt->bc_ip++];

                if (argc > (rpyl_u32)(sizeof(argv) / sizeof(argv[0]))) argc = (rpyl_u32)(sizeof(argv) / sizeof(argv[0]));
                for (i = 0; i < (int)argc; i++) {
                    rpyl_u32 sid;
                    if (rt->bc_ip >= (rpyl_u32)bc->code_count) return 0;
                    sid = bc->code[rt->bc_ip++];
                    if ((size_t)sid >= bc->string_count) argv[i] = "";
                    else argv[i] = bc->strings[sid] ? bc->strings[sid] : "";
                }

                build_joined_name(rt, argv, (int)argc, target_name, sizeof(target_name));
                tgt_idx = bc_label_find(bc, target_name);
                if (tgt_idx < 0) {
                    fprintf(stderr, "[Rpyl] Unknown label: %s\n", target_name);
                    continue;
                }

                if (op == (rpyl_u32)RPYL_BC_OP_CALL) {
                    if (rt->bc_sp >= (int)(sizeof(rt->bc_stack) / sizeof(rt->bc_stack[0]))) {
                        fprintf(stderr, "[Rpyl] call stack overflow\n");
                        continue;
                    }

                    rt->bc_stack[rt->bc_sp].saved_label_index = rt->bc_label_index;
                    rt->bc_stack[rt->bc_sp].return_ip = rt->bc_ip;
                    rt->bc_stack[rt->bc_sp].saved_entry_is_first = rt->bc_entry_is_first;
                    rt->bc_sp++;

                    push_scope(rt);

                    rt->bc_label_index = tgt_idx;
                    rt->bc_ip = bc->labels[rt->bc_label_index].ip;
                    rt->bc_end_ip = bc->labels[rt->bc_label_index].end_ip;
                    rt->bc_entry_is_first = mark_entered_block(rt, target_name);
                } else {
                    rt->bc_sp = 0;
                    unwind_to_global_scope(rt);

                    rt->bc_label_index = tgt_idx;
                    rt->bc_ip = bc->labels[rt->bc_label_index].ip;
                    rt->bc_end_ip = bc->labels[rt->bc_label_index].end_ip;
                    rt->bc_entry_is_first = mark_entered_block(rt, target_name);
                }

                continue;
            }

            fprintf(stderr, "[Rpyl] Unknown bytecode opcode: %lu\n", (unsigned long)op);
            return 0;
        }
    }
}

int rpyl_runtime_step(RpylRuntime* rt) {
    int rc;

    if (!rt) return 0;

    if (rt->exec_mode == EXEC_NONE) {
        return 0;
    }

    if (!rt->exec_running) {
        return 1;
    }

    /* If we are currently yielded, only resume when signaled. */
    if (yield_gate(rt)) {
        return 2;
    }

    if (rt->exec_mode == EXEC_AST) {
        rc = step_ast(rt);
        return rc;
    }

    if (rt->exec_mode == EXEC_BC) {
        rc = step_bc(rt);
        return rc;
    }

    return 0;
}
