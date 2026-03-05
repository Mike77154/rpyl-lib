#include "rpyl_parser.h"

#include "rpyl_config.h"
#include "rpyl_lexer.h"
#include "rpyl_token.h"
#include "rpyl_port.h"

#include <stdio.h>
#include <string.h>

static RpylToken tok;
static RpylStream* src;
static RpylArena* g_arena;

static void next_tok(void) {
    (void)rpyl_lex_stream(src, &tok);
}

static void safe_copy(char* dst, size_t dst_size, const char* src_str) {
    if (!dst || dst_size == 0) return;
    if (!src_str) {
        dst[0] = 0;
        return;
    }
    rpyl_strcpy_trunc(dst, dst_size, src_str);
}

static void skip_to_eol(void) {
    while (tok.type != TOK_NEWLINE && tok.type != TOK_EOF) {
        next_tok();
    }
    if (tok.type == TOK_NEWLINE) next_tok();
}

static AstNode* node_new(AstNodeType type) {
    return ast_new_ex(type, g_arena);
}

static AstNode* parse_call_with_name(const char* name_already_consumed) {
    AstNode* n;

    n = node_new(AST_CALL);
    if (!n) return NULL;

    safe_copy(n->name, sizeof(n->name), name_already_consumed);

    while (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING || tok.type == TOK_NUMBER || tok.type == TOK_EQUAL) {
        /* Allow optional '=' in statements like: set x = 10 */
        if (tok.type == TOK_EQUAL) {
            next_tok();
            continue;
        }

        if (n->arg_count < (int)(sizeof(n->args) / sizeof(n->args[0]))) {
            safe_copy(n->args[n->arg_count], sizeof(n->args[n->arg_count]), tok.text);
            n->arg_count++;
        }
        next_tok();
    }

    return n;
}

static AstNode* parse_call_with_name_and_preargs(const char* name_already_consumed, char pre_args[][RPYL_AST_MAX_ARG_TEXT], int pre_argc) {
    AstNode* n;
    int i;

    n = node_new(AST_CALL);
    if (!n) return NULL;

    safe_copy(n->name, sizeof(n->name), name_already_consumed);

    for (i = 0; i < pre_argc; i++) {
        if (n->arg_count < (int)(sizeof(n->args) / sizeof(n->args[0]))) {
            safe_copy(n->args[n->arg_count], sizeof(n->args[n->arg_count]), pre_args[i]);
            n->arg_count++;
        }
    }

    while (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING || tok.type == TOK_NUMBER || tok.type == TOK_EQUAL) {
        if (tok.type == TOK_EQUAL) {
            next_tok();
            continue;
        }

        if (n->arg_count < (int)(sizeof(n->args) / sizeof(n->args[0]))) {
            safe_copy(n->args[n->arg_count], sizeof(n->args[n->arg_count]), tok.text);
            n->arg_count++;
        }
        next_tok();
    }

    return n;
}

static void parse_block_name(char* out, size_t out_size) {
    size_t remain;

    out[0] = 0;

    /* Collect identifiers/strings until ':' so names can contain spaces.
       Example: Game Start:  (TOK_IDENTIFIER "Game" TOK_IDENTIFIER "Start" TOK_COLON)
    */
    while (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING) {
        if (out[0] != 0) {
            remain = out_size - strlen(out) - 1;
            if (remain > 0) strncat(out, " ", remain);
        }
        remain = out_size - strlen(out) - 1;
        if (remain > 0) strncat(out, tok.text, remain);
        next_tok();
        if (tok.type == TOK_COLON) break;
    }
}

static AstNode* parse_named_block(const char* name_single_token) {
    AstNode* block;

    block = node_new(AST_BLOCK);
    if (!block) return NULL;

    safe_copy(block->name, sizeof(block->name), name_single_token);

    if (tok.type != TOK_COLON) {
        skip_to_eol();
        return block;
    }

    next_tok(); /* ':' */

    /* Optional newline + indent */
    if (tok.type == TOK_NEWLINE) next_tok();
    if (tok.type == TOK_INDENT) next_tok();

    while (tok.type != TOK_DEDENT && tok.type != TOK_EOF) {
        if (tok.type == TOK_NEWLINE) {
            next_tok();
            continue;
        }

        if (tok.type == TOK_IDENTIFIER) {
            char first_name[RPYL_AST_MAX_NAME];
            char name_buf[RPYL_PARSER_MAX_BLOCK_NAME];
            char pre_args[RPYL_PARSER_MAX_PREARGS][RPYL_AST_MAX_ARG_TEXT];
            int pre_argc;

            AstNode* nested;
            AstNode* call;

            safe_copy(first_name, sizeof(first_name), tok.text);
            safe_copy(name_buf, sizeof(name_buf), tok.text);
            next_tok();

            /* Look ahead for multi-token block names (Identifier Identifier ... :) */
            pre_argc = 0;
            while (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING) {
                /* Save as potential call arg */
                if (pre_argc < (int)(sizeof(pre_args) / sizeof(pre_args[0]))) {
                    safe_copy(pre_args[pre_argc], sizeof(pre_args[pre_argc]), tok.text);
                    pre_argc++;
                }

                /* Append to potential block name */
                if (name_buf[0] != 0) {
                    size_t remain;
                    remain = sizeof(name_buf) - strlen(name_buf) - 1;
                    if (remain > 0) strncat(name_buf, " ", remain);
                }
                {
                    size_t remain;
                    remain = sizeof(name_buf) - strlen(name_buf) - 1;
                    if (remain > 0) strncat(name_buf, tok.text, remain);
                }

                next_tok();
                if (tok.type == TOK_COLON) break;
            }

            if (tok.type == TOK_COLON) {
                nested = parse_named_block(name_buf);
                if (nested) ast_add_child(block, nested);
            } else {
                if (pre_argc == 0) {
                    call = parse_call_with_name(first_name);
                } else {
                    call = parse_call_with_name_and_preargs(first_name, pre_args, pre_argc);
                }
                if (call) ast_add_child(block, call);
            }
            continue;
        }

        /* Skip anything else */
        next_tok();
    }

    if (tok.type == TOK_DEDENT) next_tok();
    return block;
}

static AstNode* parse_block(void) {
    char name_buf[RPYL_PARSER_MAX_BLOCK_NAME];
    AstNode* b;

    /* Multi-token block names (top-level). Example: Game Start: */
    parse_block_name(name_buf, sizeof(name_buf));
    if (name_buf[0] == 0) {
        /* Nothing sensible; skip the line. */
        skip_to_eol();
        b = node_new(AST_BLOCK);
        return b;
    }

    /* We are now positioned at TOK_COLON (or error). */
    return parse_named_block(name_buf);
}

static AstNode* parse_define(void) {
    AstNode* d;

    /* Grammar: define <name> [=] <value>
       value: identifier | string | number
     */
    d = node_new(AST_DEFINE);
    if (!d) return NULL;

    next_tok(); /* consume 'define' */

    if (tok.type != TOK_IDENTIFIER) {
        skip_to_eol();
        return d;
    }

    safe_copy(d->name, sizeof(d->name), tok.text);
    next_tok(); /* name */

    if (tok.type == TOK_EQUAL) {
        next_tok(); /* '=' */
    }

    if (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING || tok.type == TOK_NUMBER) {
        safe_copy(d->value, sizeof(d->value), tok.text);
        next_tok();
    }

    skip_to_eol();
    return d;
}

/* FILE* stream adapter ------------------------------------------------ */

typedef struct {
    FILE* f;
} RpylFileStream;

static int file_getc(void* user) {
    RpylFileStream* fs = (RpylFileStream*)user;
    if (!fs || !fs->f) return EOF;
    return fgetc(fs->f);
}

static int file_ungetc(int c, void* user) {
    RpylFileStream* fs = (RpylFileStream*)user;
    if (!fs || !fs->f) return EOF;
    return ungetc(c, fs->f);
}

/* Buffer stream adapter ---------------------------------------------- */

typedef struct {
    const unsigned char* data;
    size_t len;
    size_t pos;
    int has_push;
    int push;
} RpylBufferStream;

static int buf_getc(void* user) {
    RpylBufferStream* bs = (RpylBufferStream*)user;
    if (!bs) return EOF;
    if (bs->has_push) {
        bs->has_push = 0;
        return bs->push;
    }
    if (bs->pos >= bs->len) return EOF;
    return (int)bs->data[bs->pos++];
}

static int buf_ungetc(int c, void* user) {
    RpylBufferStream* bs = (RpylBufferStream*)user;
    if (!bs) return EOF;
    if (c == EOF) return EOF;
    if (bs->has_push) return EOF;
    bs->has_push = 1;
    bs->push = c;
    return c;
}

AstNode* rpyl_parse_stream_ex(RpylStream* s, RpylArena* arena) {
    AstNode* root;

    src = s;
    g_arena = arena;

    next_tok();

    root = node_new(AST_BLOCK);
    if (!root) return NULL;
    safe_copy(root->name, sizeof(root->name), "ROOT");

    while (tok.type != TOK_EOF) {
        if (tok.type == TOK_DEFINE) {
            AstNode* d;
            d = parse_define();
            if (d) ast_add_child(root, d);
        } else if (tok.type == TOK_LABEL) {
            /* label <name>: ... */
            next_tok();
            if (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING) {
                AstNode* b;
                b = parse_block();
                if (b) ast_add_child(root, b);
            } else {
                skip_to_eol();
            }
        } else if (tok.type == TOK_IDENTIFIER || tok.type == TOK_STRING) {
            AstNode* b;
            b = parse_block();
            if (b) ast_add_child(root, b);
        } else {
            next_tok();
        }
    }

    return root;
}

AstNode* rpyl_parse_stream(RpylStream* s) {
    return rpyl_parse_stream_ex(s, (RpylArena*)0);
}

AstNode* rpyl_parse_ex(FILE* f, RpylArena* arena) {
    RpylFileStream fs;
    RpylStream s;

    fs.f = f;
    s.user = &fs;
    s.getc_fn = file_getc;
    s.ungetc_fn = file_ungetc;

    return rpyl_parse_stream_ex(&s, arena);
}

AstNode* rpyl_parse(FILE* f) {
    return rpyl_parse_ex(f, (RpylArena*)0);
}

AstNode* rpyl_parse_buffer_ex(const char* buf, size_t len, RpylArena* arena) {
    RpylBufferStream bs;
    RpylStream s;

    bs.data = (const unsigned char*)buf;
    bs.len = len;
    bs.pos = 0;
    bs.has_push = 0;
    bs.push = 0;

    s.user = &bs;
    s.getc_fn = buf_getc;
    s.ungetc_fn = buf_ungetc;

    return rpyl_parse_stream_ex(&s, arena);
}

AstNode* rpyl_parse_buffer(const char* buf, size_t len) {
    return rpyl_parse_buffer_ex(buf, len, (RpylArena*)0);
}
