#include "rpyl_polysym.h"

#include "rpyl_config.h"
#include "rpyl_alloc.h"

#if !defined(_WIN32)
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#endif

#include "rpyl_port.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#define RPYL_MAX_SYMBOLS RPYL_POLYSYM_MAX_SYMBOLS
#define RPYL_MAX_ATTACHMENTS RPYL_POLYSYM_MAX_ATTACHMENTS

typedef struct {
    char name[RPYL_POLYSYM_MAX_NAME];
    RpylSymbolFn fn;
    void* userdata;
} SymbolEntry;

struct RpylSymbolHost {
    SymbolEntry syms[RPYL_MAX_SYMBOLS];
    int sym_count;

    char dispatch_cmd[RPYL_POLYSYM_MAX_CMD];
};

typedef struct {
    RpylContext* ctx;
    RpylSymbolHost* host;
} AttachEntry;

static AttachEntry attached[RPYL_MAX_ATTACHMENTS];

/* -------------------------- helpers -------------------------- */

static void safe_copy(char* dst, size_t dst_sz, const char* src) {
    rpyl_strcpy_trunc(dst, dst_sz, src ? src : "");
}

static RpylSymbolHost* attached_host_for(RpylContext* ctx) {
    int i;
    if (!ctx) return NULL;
    for (i = 0; i < RPYL_MAX_ATTACHMENTS; i++) {
        if (attached[i].ctx == ctx) return attached[i].host;
    }
    return NULL;
}

static int attach_set(RpylContext* ctx, RpylSymbolHost* host) {
    int i;
    if (!ctx) return 0;

    /* replace existing */
    for (i = 0; i < RPYL_MAX_ATTACHMENTS; i++) {
        if (attached[i].ctx == ctx) {
            attached[i].host = host;
            return 1;
        }
    }

    for (i = 0; i < RPYL_MAX_ATTACHMENTS; i++) {
        if (!attached[i].ctx) {
            attached[i].ctx = ctx;
            attached[i].host = host;
            return 1;
        }
    }

    return 0;
}

static void attach_clear(RpylContext* ctx) {
    int i;
    if (!ctx) return;
    for (i = 0; i < RPYL_MAX_ATTACHMENTS; i++) {
        if (attached[i].ctx == ctx) {
            attached[i].ctx = NULL;
            attached[i].host = NULL;
        }
    }
}

/* -------------------------- dispatcher command -------------------------- */

static void cmd_dispatch_sym(RpylContext* ctx, const char** args, int argc) {
    RpylSymbolHost* host;
    const char* sym;
    int i;

    if (!ctx || !args || argc < 1) {
        fprintf(stderr, "[RpylPolySym] __sym requires at least 1 arg\n");
        return;
    }

    host = rpyl_symbolhost_get(ctx);
    if (!host) {
        fprintf(stderr, "[RpylPolySym] No symbol host attached\n");
        return;
    }

    sym = args[0];
    for (i = 0; i < host->sym_count; i++) {
        if (strcmp(host->syms[i].name, sym) == 0) {
            if (host->syms[i].fn) {
                host->syms[i].fn(ctx, host->syms[i].userdata, sym, args + 1, argc - 1);
            }
            return;
        }
    }

    fprintf(stderr, "[RpylPolySym] Unknown symbol: %s\n", sym ? sym : "(null)");
}

/* -------------------------- public API -------------------------- */

RpylSymbolHost* rpyl_symbolhost_create(void) {
    RpylSymbolHost* host;
    host = (RpylSymbolHost*)rpyl_calloc(1, sizeof(RpylSymbolHost));
    if (!host) return NULL;
    host->sym_count = 0;
    safe_copy(host->dispatch_cmd, sizeof(host->dispatch_cmd), "__sym");
    return host;
}

void rpyl_symbolhost_destroy(RpylSymbolHost* host) {
    if (!host) return;
    rpyl_free(host);
}

int rpyl_symbolhost_export(
    RpylSymbolHost* host,
    const char* symbol,
    RpylSymbolFn fn,
    void* userdata
) {
    int i;
    if (!host || !symbol || !symbol[0] || !fn) return 0;

    for (i = 0; i < host->sym_count; i++) {
        if (strcmp(host->syms[i].name, symbol) == 0) {
            host->syms[i].fn = fn;
            host->syms[i].userdata = userdata;
            return 1;
        }
    }

    if (host->sym_count >= RPYL_MAX_SYMBOLS) return 0;

    safe_copy(host->syms[host->sym_count].name, sizeof(host->syms[host->sym_count].name), symbol);
    host->syms[host->sym_count].fn = fn;
    host->syms[host->sym_count].userdata = userdata;
    host->sym_count++;
    return 1;
}

int rpyl_symbolhost_remove(RpylSymbolHost* host, const char* symbol) {
    int i;
    if (!host || !symbol) return 0;

    for (i = 0; i < host->sym_count; i++) {
        if (strcmp(host->syms[i].name, symbol) == 0) {
            /* swap with last */
            host->syms[i] = host->syms[host->sym_count - 1];
            host->sym_count--;
            return 1;
        }
    }

    return 0;
}

void rpyl_symbolhost_set_dispatch_command(RpylSymbolHost* host, const char* cmd_name) {
    if (!host || !cmd_name || !cmd_name[0]) return;
    safe_copy(host->dispatch_cmd, sizeof(host->dispatch_cmd), cmd_name);
}

const char* rpyl_symbolhost_get_dispatch_command(RpylSymbolHost* host) {
    if (!host) return "__sym";
    return host->dispatch_cmd[0] ? host->dispatch_cmd : "__sym";
}

int rpyl_symbolhost_attach(RpylContext* ctx, RpylSymbolHost* host) {
    if (!ctx || !host) return 0;
    if (!attach_set(ctx, host)) return 0;

    /* register dispatcher command into ctx */
    rpyl_register_command(ctx, rpyl_symbolhost_get_dispatch_command(host), cmd_dispatch_sym);
    return 1;
}

void rpyl_symbolhost_detach(RpylContext* ctx) {
    attach_clear(ctx);
}

RpylSymbolHost* rpyl_symbolhost_get(RpylContext* ctx) {
    return attached_host_for(ctx);
}

/* -------------------------- $-line translation -------------------------- */

static int is_space_char(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static const char* lstrip(const char* s) {
    while (s && (*s == ' ' || *s == '\t')) s++;
    return s;
}

static int line_is_blank(const char* s) {
    const char* p;
    p = s;
    while (p && *p) {
        if (*p == '\n') break;
        if (!is_space_char(*p)) return 0;
        p++;
    }
    return 1;
}

static int count_indent(const char* s) {
    int n;
    n = 0;
    while (s && (*s == ' ' || *s == '\t')) {
        n++;
        s++;
    }
    return n;
}

/* Forward: extlang init header detection */
static int is_init_header_line(const char* line) {
    const char* t;

    if (!line) return 0;
    if (count_indent(line) != 0) return 0;

    t = lstrip(line);
    if (tolower((unsigned char)t[0])=='i' && tolower((unsigned char)t[1])=='n' && tolower((unsigned char)t[2])=='i' && tolower((unsigned char)t[3])=='t') {
        /* must have whitespace after init */
        if (t[4] == ' ' || t[4] == '\t') {
            /* require ':' later */
            if (strchr(t, ':') != NULL) return 1;
        }
    }

    return 0;
}

/* Token parsing helpers for $foo(...) translation */

static const char* skip_ws(const char* p) {
    while (*p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

static const char* parse_ident(const char* p, char* out, size_t out_sz) {
    size_t n;
    if (!p || !out || out_sz == 0) return NULL;

    n = 0;
    if (!(isalpha((unsigned char)*p) || *p == '_')) return NULL;

    while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
        if (n + 1 < out_sz) out[n++] = *p;
        p++;
    }
    out[n] = 0;
    return p;
}

static const char* parse_string_lit(const char* p, char* out, size_t out_sz) {
    size_t n;
    char q;

    if (!p || !out || out_sz == 0) return NULL;
    if (*p != '"' && *p != '\'') return NULL;

    q = *p++;
    n = 0;

    while (*p && *p != q) {
        if (*p == '\\' && p[1]) {
            /* preserve escapes in output */
            if (n + 2 < out_sz) {
                out[n++] = *p++;
                out[n++] = *p++;
            } else {
                p += 2;
            }
            continue;
        }

        if (n + 1 < out_sz) out[n++] = *p;
        p++;
    }

    if (*p != q) return NULL;
    p++; /* closing quote */

    out[n] = 0;
    return p;
}

static const char* parse_number_or_token(const char* p, char* out, size_t out_sz) {
    size_t n;
    if (!p || !out || out_sz == 0) return NULL;

    n = 0;
    if (*p == '-' || isdigit((unsigned char)*p)) {
        while (*p && (isdigit((unsigned char)*p) || *p == '.' || *p == '-')) {
            if (n + 1 < out_sz) out[n++] = *p;
            p++;
        }
        out[n] = 0;
        return p;
    }

    return NULL;
}

static const char* parse_varref(const char* p, char* out, size_t out_sz) {
    size_t n;
    if (!p || !out || out_sz == 0) return NULL;
    if (*p != '$') return NULL;
    p++;

    n = 0;
    if (!(isalpha((unsigned char)*p) || *p == '_')) return NULL;
    out[n++] = '$';

    while (*p && (isalnum((unsigned char)*p) || *p == '_')) {
        if (n + 1 < out_sz) out[n++] = *p;
        p++;
    }

    out[n] = 0;
    return p;
}

/* Writes one argument token to output buffer with a leading space.
   For string literals, we emit as quoted token so the main parser treats it as TOK_STRING.
*/
static int out_arg(char* out, size_t out_sz, const char* token, int is_string) {
    size_t remain;

    if (!out || !token) return 0;

    remain = out_sz - strlen(out) - 1;
    if (remain == 0) return 0;
    strncat(out, " ", remain);

    remain = out_sz - strlen(out) - 1;
    if (remain == 0) return 0;

    if (is_string) {
        /* wrap in quotes */
        strncat(out, "\"", remain);
        remain = out_sz - strlen(out) - 1;
        if (remain == 0) return 0;
        strncat(out, token, remain);
        remain = out_sz - strlen(out) - 1;
        if (remain == 0) return 0;
        strncat(out, "\"", remain);
    } else {
        strncat(out, token, remain);
    }

    return 1;
}

/* Parse $name(arg1, arg2, ...)
   Writes: <dispatch_cmd> <name> <arg1> <arg2> ...\n
   Returns 1 if parsed, 0 if not a $ call, -1 on syntax error.
*/
static int parse_dollar_call(
    const char* line,
    const char* dispatch_cmd,
    char* out_line,
    size_t out_line_sz,
    const char* path,
    int line_no
) {
    const char* p;
    char sym[RPYL_POLYSYM_MAX_NAME];

    if (!line || !dispatch_cmd || !out_line || out_line_sz == 0) return 0;

    p = lstrip(line);
    if (*p != '$') return 0;
    p++;
    p = skip_ws(p);

    p = parse_ident(p, sym, sizeof(sym));
    if (!p) {
        fprintf(stderr, "[RpylPolySym] Invalid $ call at %s:%d\n", path, line_no);
        return -1;
    }

    p = skip_ws(p);
    if (*p != '(') {
        fprintf(stderr, "[RpylPolySym] Expected '(' in $ call at %s:%d\n", path, line_no);
        return -1;
    }
    p++;

    out_line[0] = 0;
    safe_copy(out_line, out_line_sz, dispatch_cmd);
    if (!out_arg(out_line, out_line_sz, sym, 0)) return -1;

    while (1) {
        char tok[RPYL_POLYSYM_MAX_TOKEN];
        const char* np;

        p = skip_ws(p);
        if (*p == ')') {
            p++;
            break;
        }

        tok[0] = 0;

        /* string */
        np = parse_string_lit(p, tok, sizeof(tok));
        if (np) {
            if (!out_arg(out_line, out_line_sz, tok, 1)) return -1;
            p = np;
        } else {
            /* var ref */
            np = parse_varref(p, tok, sizeof(tok));
            if (np) {
                if (!out_arg(out_line, out_line_sz, tok, 0)) return -1;
                p = np;
            } else {
                /* number */
                np = parse_number_or_token(p, tok, sizeof(tok));
                if (np) {
                    if (!out_arg(out_line, out_line_sz, tok, 0)) return -1;
                    p = np;
                } else {
                    /* identifier */
                    np = parse_ident(p, tok, sizeof(tok));
                    if (!np) {
                        fprintf(stderr, "[RpylPolySym] Bad argument in $ call at %s:%d\n", path, line_no);
                        return -1;
                    }
                    if (!out_arg(out_line, out_line_sz, tok, 0)) return -1;
                    p = np;
                }
            }
        }

        p = skip_ws(p);
        if (*p == ',') {
            p++;
            continue;
        }
        if (*p == ')') {
            p++;
            break;
        }

        fprintf(stderr, "[RpylPolySym] Expected ',' or ')' in $ call at %s:%d\n", path, line_no);
        return -1;
    }

    /* Ensure newline */
    {
        size_t len;
        len = strlen(out_line);
        if (len + 1 < out_line_sz) {
            out_line[len] = '\n';
            out_line[len + 1] = 0;
        }
    }

    return 1;
}

static int translate_dollar_line(
    const char* in_line,
    const char* dispatch_cmd,
    char* out_line,
    size_t out_line_sz,
    const char* path,
    int line_no
) {
    int r;
    r = parse_dollar_call(in_line, dispatch_cmd, out_line, out_line_sz, path, line_no);
    if (r == 0) return 0;
    return r;
}

/* Creates a temporary file path. */
static int make_temp_path(char* out_path, size_t out_path_sz) {
    if (!out_path || out_path_sz < 16) return 0;

#if defined(_WIN32)
    {
        char* p;
        p = tmpnam(NULL);
        if (!p) return 0;
        safe_copy(out_path, out_path_sz, p);
        return 1;
    }
#else
    {
        char tpl[RPYL_POLYSYM_MAX_TPL];
        int fd;
        safe_copy(tpl, sizeof(tpl), "/tmp/rpylXXXXXX");
        fd = mkstemp(tpl);
        if (fd < 0) return 0;
        close(fd);
        safe_copy(out_path, out_path_sz, tpl);
        return 1;
    }
#endif
}

/* -------------------------- combined loader -------------------------- */

int rpyl_load_file_extlang_symbols(
    RpylContext* ctx,
    RpylLangHost* lang_host,
    RpylSymbolHost* sym_host,
    const char* path
) {
    FILE* in;
    char tmp_path[RPYL_POLYSYM_MAX_PATH];
    FILE* out;

    char line[RPYL_POLYSYM_MAX_LINE];
    char out_line[RPYL_POLYSYM_MAX_OUT_LINE];
    int line_no;

    int in_init;
    int init_header_indent;

    char re_line[RPYL_POLYSYM_MAX_LINE];
    int has_re_line;

    int ok;

    if (!ctx || !lang_host || !sym_host || !path) return 0;

    /* Ensure ctx has dispatcher binding before loading. */
    if (!rpyl_symbolhost_attach(ctx, sym_host)) return 0;

    in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "[RpylPolySym] Failed to open: %s\n", path);
        return 0;
    }

    if (!make_temp_path(tmp_path, sizeof(tmp_path))) {
        fprintf(stderr, "[RpylPolySym] Failed to create temp path\n");
        fclose(in);
        return 0;
    }

    out = fopen(tmp_path, "wb");
    if (!out) {
        fprintf(stderr, "[RpylPolySym] Failed to open temp file: %s\n", tmp_path);
        fclose(in);
        return 0;
    }

    line_no = 0;
    in_init = 0;
    init_header_indent = 0;

    re_line[0] = 0;
    has_re_line = 0;

    while (1) {
        if (!has_re_line) {
            if (!fgets(line, (int)sizeof(line), in)) break;
        } else {
            rpyl_strcpy_trunc(line, sizeof(line), re_line);
            has_re_line = 0;
        }

        line_no++;

        if (!in_init) {
            if (is_init_header_line(line)) {
                in_init = 1;
                init_header_indent = count_indent(line);
                /* copy header as-is (extlang will extract later) */
                fputs(line, out);
                continue;
            }

            /* translate $-lines outside init blocks */
            {
                int t;
                t = translate_dollar_line(
                    line,
                    rpyl_symbolhost_get_dispatch_command(sym_host),
                    out_line,
                    sizeof(out_line),
                    path,
                    line_no
                );

                if (t < 0) {
                    fclose(in);
                    fclose(out);
                    remove(tmp_path);
                    return 0;
                }
                if (t == 1) {
                    fputs(out_line, out);
                } else {
                    fputs(line, out);
                }
            }

            continue;
        }

        /* inside init block: copy raw lines until indent returns */
        if (!line_is_blank(line) && count_indent(line) <= init_header_indent) {
            in_init = 0;
            has_re_line = 1;
            rpyl_strcpy_trunc(re_line, sizeof(re_line), line);
            continue;
        }

        fputs(line, out);
    }

    fclose(in);
    fclose(out);

    /* Delegate init extraction/execution + normal parsing to rpyl_extlang. */
    ok = rpyl_load_file_extlang(ctx, lang_host, tmp_path);

    remove(tmp_path);
    return ok;
}
