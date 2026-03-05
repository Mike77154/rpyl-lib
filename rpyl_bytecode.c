#include "rpyl_bytecode.h"

#include "rpyl_alloc.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RPYL_BC_MAGIC_0 'R'
#define RPYL_BC_MAGIC_1 'P'
#define RPYL_BC_MAGIC_2 'B'
#define RPYL_BC_MAGIC_3 '1'

/* ---------------- dynamic arrays ---------------- */

static int ensure_code_cap(RpylBytecode* bc, size_t need) {
    rpyl_u32* n;
    size_t cap;
    if (need <= bc->code_cap) return 1;
    cap = (bc->code_cap == 0) ? 256 : (bc->code_cap * 2);
    while (cap < need) cap *= 2;
    n = (rpyl_u32*)rpyl_realloc(bc->code, cap * sizeof(rpyl_u32));
    if (!n) return 0;
    bc->code = n;
    bc->code_cap = cap;
    return 1;
}

static int ensure_string_cap(RpylBytecode* bc, size_t need) {
    char** n;
    size_t cap;
    if (need <= bc->string_cap) return 1;
    cap = (bc->string_cap == 0) ? 64 : (bc->string_cap * 2);
    while (cap < need) cap *= 2;
    n = (char**)rpyl_realloc(bc->strings, cap * sizeof(char*));
    if (!n) return 0;
    bc->strings = n;
    bc->string_cap = cap;
    return 1;
}

static int ensure_label_cap(RpylBytecode* bc, size_t need) {
    RpylBcLabel* n;
    size_t cap;
    if (need <= bc->label_cap) return 1;
    cap = (bc->label_cap == 0) ? 16 : (bc->label_cap * 2);
    while (cap < need) cap *= 2;
    n = (RpylBcLabel*)rpyl_realloc(bc->labels, cap * sizeof(RpylBcLabel));
    if (!n) return 0;
    bc->labels = n;
    bc->label_cap = cap;
    return 1;
}

static int ensure_define_cap(RpylBytecode* bc, size_t need) {
    RpylBcDefine* n;
    size_t cap;
    if (need <= bc->define_cap) return 1;
    cap = (bc->define_cap == 0) ? 16 : (bc->define_cap * 2);
    while (cap < need) cap *= 2;
    n = (RpylBcDefine*)rpyl_realloc(bc->defines, cap * sizeof(RpylBcDefine));
    if (!n) return 0;
    bc->defines = n;
    bc->define_cap = cap;
    return 1;
}

static int code_push(RpylBytecode* bc, rpyl_u32 w) {
    if (!ensure_code_cap(bc, bc->code_count + 1)) return 0;
    bc->code[bc->code_count++] = w;
    return 1;
}

static int find_string(const RpylBytecode* bc, const char* s) {
    size_t i;
    if (!bc || !s) return -1;
    for (i = 0; i < bc->string_count; i++) {
        if (bc->strings[i] && strcmp(bc->strings[i], s) == 0) return (int)i;
    }
    return -1;
}

static int intern_string(RpylBytecode* bc, const char* s) {
    int idx;
    size_t n;
    char* cp;

    if (!bc) return -1;
    if (!s) s = "";

    idx = find_string(bc, s);
    if (idx >= 0) return idx;

    if (!ensure_string_cap(bc, bc->string_count + 1)) return -1;

    n = strlen(s);
    cp = (char*)rpyl_malloc(n + 1);
    if (!cp) return -1;
    memcpy(cp, s, n + 1);

    bc->strings[bc->string_count] = cp;
    idx = (int)bc->string_count;
    bc->string_count++;
    return idx;
}

static int define_find(RpylBytecode* bc, int name_sid) {
    size_t i;
    if (!bc) return -1;
    for (i = 0; i < bc->define_count; i++) {
        if ((int)bc->defines[i].name_sid == name_sid) return (int)i;
    }
    return -1;
}

static int define_add_or_set(RpylBytecode* bc, int name_sid, int value_sid) {
    int idx;
    if (!bc) return 0;
    idx = define_find(bc, name_sid);
    if (idx >= 0) {
        bc->defines[idx].value_sid = (rpyl_u32)value_sid;
        return 1;
    }
    if (!ensure_define_cap(bc, bc->define_count + 1)) return 0;
    bc->defines[bc->define_count].name_sid = (rpyl_u32)name_sid;
    bc->defines[bc->define_count].value_sid = (rpyl_u32)value_sid;
    bc->define_count++;
    return 1;
}

static int label_add(RpylBytecode* bc, int name_sid, rpyl_u32 ip, rpyl_u32 end_ip) {
    if (!bc) return 0;
    if (!ensure_label_cap(bc, bc->label_count + 1)) return 0;
    bc->labels[bc->label_count].name_sid = (rpyl_u32)name_sid;
    bc->labels[bc->label_count].ip = ip;
    bc->labels[bc->label_count].end_ip = end_ip;
    bc->label_count++;
    return 1;
}

/* ---------------- compiler ---------------- */

static int compile_node(RpylBytecode* bc, AstNode* node);

static int compile_call(RpylBytecode* bc, AstNode* node) {
    const char* name;

    if (!bc || !node) return 0;
    name = node->name;

    /* Statement prefix: once <cmd> ... */
    if (name && strcmp(name, "once") == 0) {
        if (node->arg_count >= 1) {
            int once_id;
            size_t skip_word_index;

            once_id = bc->next_once_id++;
            /* OP_ONCE_CHECK once_id skip_ip */
            if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_ONCE_CHECK)) return 0;
            if (!code_push(bc, (rpyl_u32)once_id)) return 0;

            skip_word_index = bc->code_count;
            if (!code_push(bc, 0)) return 0;

            /* inner command */
            {
                int inner_name_sid;
                int argc;
                int i;

                inner_name_sid = intern_string(bc, node->args[0]);
                if (inner_name_sid < 0) return 0;

                if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_CMD)) return 0;
                if (!code_push(bc, (rpyl_u32)inner_name_sid)) return 0;

                argc = node->arg_count - 1;
                if (argc < 0) argc = 0;
                if (!code_push(bc, (rpyl_u32)argc)) return 0;

                for (i = 0; i < argc; i++) {
                    int sid;
                    sid = intern_string(bc, node->args[i + 1]);
                    if (sid < 0) return 0;
                    if (!code_push(bc, (rpyl_u32)sid)) return 0;
                }
            }

            /* Patch skip ip to current */
            bc->code[skip_word_index] = (rpyl_u32)bc->code_count;
            return 1;
        }
        /* once with no inner cmd => no-op */
        return 1;
    }

    /* Statement prefix: on_enter <cmd> ... */
    if (name && strcmp(name, "on_enter") == 0) {
        if (node->arg_count >= 1) {
            size_t skip_word_index;

            if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_ON_ENTER_CHECK)) return 0;
            skip_word_index = bc->code_count;
            if (!code_push(bc, 0)) return 0;

            /* inner command */
            {
                int inner_name_sid;
                int argc;
                int i;

                inner_name_sid = intern_string(bc, node->args[0]);
                if (inner_name_sid < 0) return 0;

                if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_CMD)) return 0;
                if (!code_push(bc, (rpyl_u32)inner_name_sid)) return 0;

                argc = node->arg_count - 1;
                if (argc < 0) argc = 0;
                if (!code_push(bc, (rpyl_u32)argc)) return 0;

                for (i = 0; i < argc; i++) {
                    int sid;
                    sid = intern_string(bc, node->args[i + 1]);
                    if (sid < 0) return 0;
                    if (!code_push(bc, (rpyl_u32)sid)) return 0;
                }
            }

            bc->code[skip_word_index] = (rpyl_u32)bc->code_count;
            return 1;
        }
        /* on_enter with no inner cmd => no-op */
        return 1;
    }

    /* Builtins */
    if (name && (strcmp(name, "set") == 0 || strcmp(name, "let") == 0 || strcmp(name, "setg") == 0 || strcmp(name, "global") == 0)) {
        int flags;
        int var_sid;
        int argc;
        int i;

        if (node->arg_count < 2) return 1;

        flags = 0;
        if (strcmp(name, "setg") == 0 || strcmp(name, "global") == 0) flags |= RPYL_BC_SET_GLOBAL;

        var_sid = intern_string(bc, node->args[0]);
        if (var_sid < 0) return 0;

        argc = node->arg_count - 1;
        if (argc < 0) argc = 0;

        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_SET)) return 0;
        if (!code_push(bc, (rpyl_u32)flags)) return 0;
        if (!code_push(bc, (rpyl_u32)var_sid)) return 0;
        if (!code_push(bc, (rpyl_u32)argc)) return 0;

        for (i = 0; i < argc; i++) {
            int sid;
            sid = intern_string(bc, node->args[i + 1]);
            if (sid < 0) return 0;
            if (!code_push(bc, (rpyl_u32)sid)) return 0;
        }

        return 1;
    }

    if (name && strcmp(name, "call") == 0) {
        int argc;
        int i;
        if (node->arg_count < 1) return 1;
        argc = node->arg_count;
        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_CALL)) return 0;
        if (!code_push(bc, (rpyl_u32)argc)) return 0;
        for (i = 0; i < argc; i++) {
            int sid;
            sid = intern_string(bc, node->args[i]);
            if (sid < 0) return 0;
            if (!code_push(bc, (rpyl_u32)sid)) return 0;
        }
        return 1;
    }

    if (name && (strcmp(name, "jump") == 0 || strcmp(name, "goto") == 0)) {
        int argc;
        int i;
        if (node->arg_count < 1) return 1;
        argc = node->arg_count;
        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_JUMP)) return 0;
        if (!code_push(bc, (rpyl_u32)argc)) return 0;
        for (i = 0; i < argc; i++) {
            int sid;
            sid = intern_string(bc, node->args[i]);
            if (sid < 0) return 0;
            if (!code_push(bc, (rpyl_u32)sid)) return 0;
        }
        return 1;
    }

    if (name && strcmp(name, "return") == 0) {
        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_RETURN)) return 0;
        return 1;
    }

    /* Generic command */
    {
        int name_sid;
        int argc;
        int i;

        name_sid = intern_string(bc, name);
        if (name_sid < 0) return 0;

        argc = node->arg_count;
        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_CMD)) return 0;
        if (!code_push(bc, (rpyl_u32)name_sid)) return 0;
        if (!code_push(bc, (rpyl_u32)argc)) return 0;

        for (i = 0; i < argc; i++) {
            int sid;
            sid = intern_string(bc, node->args[i]);
            if (sid < 0) return 0;
            if (!code_push(bc, (rpyl_u32)sid)) return 0;
        }
    }

    return 1;
}

static int compile_block(RpylBytecode* bc, AstNode* node) {
    const char* name;
    AstNode* child;

    if (!bc || !node) return 0;

    name = node->name;

    /* Nested structured blocks */
    if (name && strcmp(name, "once") == 0) {
        int once_id;
        size_t skip_word_index;

        once_id = bc->next_once_id++;
        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_ONCE_CHECK)) return 0;
        if (!code_push(bc, (rpyl_u32)once_id)) return 0;
        skip_word_index = bc->code_count;
        if (!code_push(bc, 0)) return 0;

        child = node->children;
        while (child) {
            if (!compile_node(bc, child)) return 0;
            child = child->next;
        }

        bc->code[skip_word_index] = (rpyl_u32)bc->code_count;
        return 1;
    }

    if (name && strcmp(name, "on_enter") == 0) {
        size_t skip_word_index;

        if (!code_push(bc, (rpyl_u32)RPYL_BC_OP_ON_ENTER_CHECK)) return 0;
        skip_word_index = bc->code_count;
        if (!code_push(bc, 0)) return 0;

        child = node->children;
        while (child) {
            if (!compile_node(bc, child)) return 0;
            child = child->next;
        }

        bc->code[skip_word_index] = (rpyl_u32)bc->code_count;
        return 1;
    }

    /* Normal grouping block: inline */
    child = node->children;
    while (child) {
        if (!compile_node(bc, child)) return 0;
        child = child->next;
    }

    return 1;
}

static int compile_node(RpylBytecode* bc, AstNode* node) {
    if (!bc || !node) return 0;

    if (node->type == AST_CALL) {
        return compile_call(bc, node);
    }

    if (node->type == AST_BLOCK) {
        return compile_block(bc, node);
    }

    /* defines are handled separately */
    return 1;
}

RpylBytecode* rpyl_bytecode_create(void) {
    RpylBytecode* bc;
    bc = (RpylBytecode*)rpyl_calloc(1, sizeof(RpylBytecode));
    if (!bc) return NULL;
    bc->version = RPYL_BC_VERSION;
    bc->next_once_id = 1;
    return bc;
}

void rpyl_bytecode_clear(RpylBytecode* bc) {
    size_t i;
    if (!bc) return;

    if (bc->strings) {
        for (i = 0; i < bc->string_count; i++) {
            if (bc->strings[i]) rpyl_free(bc->strings[i]);
        }
        rpyl_free(bc->strings);
    }

    if (bc->code) rpyl_free(bc->code);
    if (bc->labels) rpyl_free(bc->labels);
    if (bc->defines) rpyl_free(bc->defines);

    memset(bc, 0, sizeof(*bc));
    bc->version = RPYL_BC_VERSION;
    bc->next_once_id = 1;
}

void rpyl_bytecode_destroy(RpylBytecode* bc) {
    if (!bc) return;
    rpyl_bytecode_clear(bc);
    rpyl_free(bc);
}

int rpyl_bytecode_compile_ast(RpylBytecode* out, AstNode* root) {
    AstNode* n;

    if (!out || !root) return 0;

    rpyl_bytecode_clear(out);
    out->version = RPYL_BC_VERSION;

    /* Collect defines */
    n = root->children;
    while (n) {
        if (n->type == AST_DEFINE) {
            int ns;
            int vs;
            ns = intern_string(out, n->name);
            vs = intern_string(out, n->value);
            if (ns < 0 || vs < 0) return 0;
            if (!define_add_or_set(out, ns, vs)) return 0;
        }
        n = n->next;
    }

    /* Compile each top-level block as a label */
    n = root->children;
    while (n) {
        if (n->type == AST_BLOCK) {
            int name_sid;
            rpyl_u32 start_ip;
            rpyl_u32 end_ip;
            AstNode* child;

            name_sid = intern_string(out, n->name);
            if (name_sid < 0) return 0;

            start_ip = (rpyl_u32)out->code_count;

            child = n->children;
            while (child) {
                if (!compile_node(out, child)) return 0;
                child = child->next;
            }

            if (!code_push(out, (rpyl_u32)RPYL_BC_OP_END)) return 0;

            end_ip = (rpyl_u32)out->code_count;

            if (!label_add(out, name_sid, start_ip, end_ip)) return 0;
        }
        n = n->next;
    }

    return 1;
}

/* ---------------- IO (portable LE u32) ---------------- */

static int write_u32(FILE* f, rpyl_u32 v) {
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFFu);
    b[1] = (unsigned char)((v >> 8) & 0xFFu);
    b[2] = (unsigned char)((v >> 16) & 0xFFu);
    b[3] = (unsigned char)((v >> 24) & 0xFFu);
    return (fwrite(b, 1, 4, f) == 4) ? 1 : 0;
}

static int read_u32(FILE* f, rpyl_u32* out) {
    unsigned char b[4];
    size_t n;
    rpyl_u32 v;

    if (!out) return 0;
    n = fread(b, 1, 4, f);
    if (n != 4) return 0;

    v = 0;
    v |= (rpyl_u32)b[0];
    v |= ((rpyl_u32)b[1]) << 8;
    v |= ((rpyl_u32)b[2]) << 16;
    v |= ((rpyl_u32)b[3]) << 24;
    *out = v;
    return 1;
}

int rpyl_bytecode_save(const RpylBytecode* bc, const char* path) {
    FILE* f;
    rpyl_u32 i;

    if (!bc || !path) return 0;

    f = fopen(path, "wb");
    if (!f) return 0;

    /* header */
    {
        unsigned char m[4];
        m[0] = (unsigned char)RPYL_BC_MAGIC_0;
        m[1] = (unsigned char)RPYL_BC_MAGIC_1;
        m[2] = (unsigned char)RPYL_BC_MAGIC_2;
        m[3] = (unsigned char)RPYL_BC_MAGIC_3;
        if (fwrite(m, 1, 4, f) != 4) {
            fclose(f);
            return 0;
        }
    }

    if (!write_u32(f, (rpyl_u32)bc->version)) { fclose(f); return 0; }

    if (!write_u32(f, (rpyl_u32)bc->string_count)) { fclose(f); return 0; }
    if (!write_u32(f, (rpyl_u32)bc->code_count)) { fclose(f); return 0; }
    if (!write_u32(f, (rpyl_u32)bc->label_count)) { fclose(f); return 0; }
    if (!write_u32(f, (rpyl_u32)bc->define_count)) { fclose(f); return 0; }

    /* strings */
    for (i = 0; i < (rpyl_u32)bc->string_count; i++) {
        const char* s;
        rpyl_u32 len;

        s = bc->strings[i] ? bc->strings[i] : "";
        len = (rpyl_u32)strlen(s);

        if (!write_u32(f, len)) { fclose(f); return 0; }
        if (len > 0) {
            if (fwrite(s, 1, (size_t)len, f) != (size_t)len) { fclose(f); return 0; }
        }
    }

    /* code */
    for (i = 0; i < (rpyl_u32)bc->code_count; i++) {
        if (!write_u32(f, (rpyl_u32)bc->code[i])) { fclose(f); return 0; }
    }

    /* labels */
    for (i = 0; i < (rpyl_u32)bc->label_count; i++) {
        if (!write_u32(f, bc->labels[i].name_sid)) { fclose(f); return 0; }
        if (!write_u32(f, bc->labels[i].ip)) { fclose(f); return 0; }
        if (!write_u32(f, bc->labels[i].end_ip)) { fclose(f); return 0; }
    }

    /* defines */
    for (i = 0; i < (rpyl_u32)bc->define_count; i++) {
        if (!write_u32(f, bc->defines[i].name_sid)) { fclose(f); return 0; }
        if (!write_u32(f, bc->defines[i].value_sid)) { fclose(f); return 0; }
    }

    fclose(f);
    return 1;
}

int rpyl_bytecode_load_into(RpylBytecode* bc, const char* path) {
    FILE* f;
    unsigned char m[4];
    rpyl_u32 version;
    rpyl_u32 sc;
    rpyl_u32 cc;
    rpyl_u32 lc;
    rpyl_u32 dc;
    rpyl_u32 i;

    if (!bc || !path) return 0;

    f = fopen(path, "rb");
    if (!f) return 0;

    if (fread(m, 1, 4, f) != 4) { fclose(f); return 0; }
    if (m[0] != (unsigned char)RPYL_BC_MAGIC_0 || m[1] != (unsigned char)RPYL_BC_MAGIC_1 || m[2] != (unsigned char)RPYL_BC_MAGIC_2 || m[3] != (unsigned char)RPYL_BC_MAGIC_3) {
        fclose(f);
        return 0;
    }

    if (!read_u32(f, &version)) { fclose(f); return 0; }
    if (version != (rpyl_u32)RPYL_BC_VERSION) {
        fclose(f);
        return 0;
    }

    if (!read_u32(f, &sc)) { fclose(f); return 0; }
    if (!read_u32(f, &cc)) { fclose(f); return 0; }
    if (!read_u32(f, &lc)) { fclose(f); return 0; }
    if (!read_u32(f, &dc)) { fclose(f); return 0; }

    rpyl_bytecode_clear(bc);
    bc->version = version;

    /* allocate tables */
    if (!ensure_string_cap(bc, (size_t)sc)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
    bc->string_count = 0;

    for (i = 0; i < sc; i++) {
        rpyl_u32 len;
        char* s;

        if (!read_u32(f, &len)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        s = (char*)rpyl_malloc((size_t)len + 1);
        if (!s) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        if (len > 0) {
            if (fread(s, 1, (size_t)len, f) != (size_t)len) {
                rpyl_free(s);
                fclose(f);
                rpyl_bytecode_clear(bc);
                return 0;
            }
        }
        s[len] = 0;
        bc->strings[bc->string_count++] = s;
    }

    if (!ensure_code_cap(bc, (size_t)cc)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
    bc->code_count = 0;
    for (i = 0; i < cc; i++) {
        rpyl_u32 w;
        if (!read_u32(f, &w)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        bc->code[bc->code_count++] = w;
    }

    if (!ensure_label_cap(bc, (size_t)lc)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
    bc->label_count = 0;
    for (i = 0; i < lc; i++) {
        RpylBcLabel L;
        if (!read_u32(f, &L.name_sid)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        if (!read_u32(f, &L.ip)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        if (!read_u32(f, &L.end_ip)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        bc->labels[bc->label_count++] = L;
    }

    if (!ensure_define_cap(bc, (size_t)dc)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
    bc->define_count = 0;
    for (i = 0; i < dc; i++) {
        RpylBcDefine D;
        if (!read_u32(f, &D.name_sid)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        if (!read_u32(f, &D.value_sid)) { fclose(f); rpyl_bytecode_clear(bc); return 0; }
        bc->defines[bc->define_count++] = D;
    }

    fclose(f);
    return 1;
}
