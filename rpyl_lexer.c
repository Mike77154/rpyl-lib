#include "rpyl_lexer.h"

#include "rpyl_config.h"
#include "rpyl_port.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static char g_label_kw[RPYL_LEXER_MAX_LABEL_KW] = "label";

static int indent_stack[RPYL_LEXER_MAX_INDENT];
static int indent_top = 0;

static int current_indent = 0;

static int line = 1;
static int pending_dedent = 0;
static int bol = 1;

void rpyl_lexer_set_label_keyword(const char* kw) {
    if (!kw || !kw[0]) return;
    rpyl_strcpy_trunc(g_label_kw, sizeof(g_label_kw), kw);
}

void rpyl_lexer_reset(void) {
    memset(indent_stack, 0, sizeof(indent_stack));
    indent_top = 0;
    current_indent = 0;
    line = 1;
    pending_dedent = 0;
    bol = 1;
}

static void push_indent(int n) {
    if (indent_top + 1 >= (int)(sizeof(indent_stack) / sizeof(indent_stack[0]))) {
        /* Prevent indent stack overflow; clamp. */
        return;
    }
    indent_stack[++indent_top] = n;
}

static int pop_indent(void) {
    return indent_stack[indent_top--];
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

/* Lexer core ---------------------------------------------------------- */

int rpyl_lex_stream(RpylStream* s, RpylToken* out) {
    int c;

    /* Prevent stale text when token types don't populate it. */
    if (out && RPYL_TOKEN_MAX_TEXT > 0) out->text[0] = 0;

    if (!s || !out) return 0;

    if (pending_dedent > 0) {
        pending_dedent--;
        out->type = TOK_DEDENT;
        out->line = line;
        return 1;
    }

    c = rpyl_stream_getc(s);
    if (c == EOF) {
        if (indent_top > 0) {
            pop_indent();
            out->type = TOK_DEDENT;
            out->line = line;
            return 1;
        }
        out->type = TOK_EOF;
        return 0;
    }

    if (bol) {
        int spaces = 0;
        while (c == ' ') {
            spaces++;
            c = rpyl_stream_getc(s);
        }

        /* Blank lines and comment-only lines should not affect indentation.
           This avoids spurious INDENT/DEDENT from leading spaces on comments. */
        if (c == '\n') {
            bol = 1;
            out->type = TOK_NEWLINE;
            out->line = line;
            line++;
            return 1;
        }

        if (c == '#') {
            while ((c = rpyl_stream_getc(s)) != '\n' && c != EOF) {
                /* skip */
            }
            if (c == EOF) {
                /* Treat as EOF (and flush remaining dedents if any). */
                if (indent_top > 0) {
                    pop_indent();
                    out->type = TOK_DEDENT;
                    out->line = line;
                    return 1;
                }
                out->type = TOK_EOF;
                return 0;
            }
            bol = 1;
            out->type = TOK_NEWLINE;
            out->line = line;
            line++;
            return 1;
        }

        bol = 0;

        if (spaces > current_indent) {
            push_indent(spaces);
            current_indent = spaces;
            out->type = TOK_INDENT;
            out->line = line;
            rpyl_stream_ungetc(s, c);
            return 1;
        }

        if (spaces < current_indent) {
            while (indent_top > 0 && indent_stack[indent_top] > spaces) {
                pop_indent();
                pending_dedent++;
            }
            current_indent = spaces;
            if (pending_dedent > 0) {
                pending_dedent--;
                out->type = TOK_DEDENT;
                out->line = line;
                rpyl_stream_ungetc(s, c);
                return 1;
            }
        }
    }

    if (c == '\n') {
        bol = 1;
        out->type = TOK_NEWLINE;
        out->line = line;
        line++;
        return 1;
    }

    if (isspace(c))
        return rpyl_lex_stream(s, out);

    /* Inline comments */
    if (c == '#') {
        while ((c = rpyl_stream_getc(s)) != '\n' && c != EOF) {
            /* skip */
        }
        if (c == EOF) {
            if (indent_top > 0) {
                pop_indent();
                out->type = TOK_DEDENT;
                out->line = line;
                return 1;
            }
            out->type = TOK_EOF;
            return 0;
        }
        bol = 1;
        out->type = TOK_NEWLINE;
        out->line = line;
        line++;
        return 1;
    }

    /* $var identifier (for simple variable substitution) */
    if (c == '$') {
        int i = 0;
        out->text[i++] = '$';
        c = rpyl_stream_getc(s);
        if (c == EOF) {
            out->text[1] = 0;
            out->type = TOK_IDENTIFIER;
            out->line = line;
            return 1;
        }

        if (!(isalpha(c) || c == '_')) {
            /* Not a valid $identifier. Push back and just return '$' as identifier. */
            rpyl_stream_ungetc(s, c);
            out->text[1] = 0;
            out->type = TOK_IDENTIFIER;
            out->line = line;
            return 1;
        }

        out->text[i++] = (char)c;
        while ((c = rpyl_stream_getc(s)), isalnum(c) || c == '_') {
            if (i < (int)sizeof(out->text) - 1)
                out->text[i++] = (char)c;
        }
        out->text[i] = 0;
        rpyl_stream_ungetc(s, c);
        out->type = TOK_IDENTIFIER;
        out->line = line;
        return 1;
    }

    if (isalpha(c) || c == '_' || c == '.') {
        int i = 0;
        out->text[i++] = (char)c;
        while ((c = rpyl_stream_getc(s)), isalnum(c) || c == '_' || c == '.') {
            if (i < (int)sizeof(out->text) - 1)
                out->text[i++] = (char)c;
        }
        out->text[i] = 0;
        rpyl_stream_ungetc(s, c);

        if (strcmp(out->text, "define") == 0)
            out->type = TOK_DEFINE;
        else if (strcmp(out->text, g_label_kw) == 0)
            out->type = TOK_LABEL;
        else
            out->type = TOK_IDENTIFIER;

        out->line = line;
        return 1;
    }

    if (c == '"') {
        int i = 0;
        while ((c = rpyl_stream_getc(s)) != EOF) {
            if (c == '"') break;

            /* Disallow raw newlines inside strings (but allow escaped ones like \n). */
            if (c == '\n') {
                rpyl_stream_ungetc(s, c);
                break;
            }

            if (c == '\\') {
                int e = rpyl_stream_getc(s);
                if (e == EOF) break;
                switch (e) {
                    case 'n': c = '\n'; break;
                    case 't': c = '\t'; break;
                    case 'r': c = '\r'; break;
                    case '\\': c = '\\'; break;
                    case '"': c = '"'; break;
                    default:
                        /* Unknown escape: keep the escaped character as-is. */
                        c = e;
                        break;
                }
            }

            if (i < (int)sizeof(out->text) - 1)
                out->text[i++] = (char)c;
        }
        out->text[i] = 0;
        out->type = TOK_STRING;
        out->line = line;
        return 1;
    }

    if (isdigit(c)) {
        int i = 0;
        out->text[i++] = (char)c;
        while ((c = rpyl_stream_getc(s)), isdigit(c)) {
            if (i < (int)sizeof(out->text) - 1)
                out->text[i++] = (char)c;
        }
        out->text[i] = 0;
        rpyl_stream_ungetc(s, c);
        out->type = TOK_NUMBER;
        out->line = line;
        return 1;
    }

    if (c == ':') {
        out->type = TOK_COLON;
        out->line = line;
        return 1;
    }

    if (c == '=') {
        out->type = TOK_EQUAL;
        out->line = line;
        return 1;
    }

    /* Skip unknown characters and keep lexing. */
    return rpyl_lex_stream(s, out);
}

int rpyl_lex(FILE* f, RpylToken* out) {
    RpylFileStream fs;
    RpylStream s;

    if (!f || !out) return 0;

    fs.f = f;
    s.user = &fs;
    s.getc_fn = file_getc;
    s.ungetc_fn = file_ungetc;

    return rpyl_lex_stream(&s, out);
}
