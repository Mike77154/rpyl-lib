#include "rpyl_extlang.h"

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


typedef struct {
    char lang[RPYL_EXTLANG_MAX_LANG];
    RpylInitLangFn fn;
    void* userdata;
} LangEntry;

typedef struct {
    char lang[RPYL_EXTLANG_MAX_LANG];
    int priority;
    int seq;            /* appearance order in file */
    int start_line;     /* 1-based */
    char* code;         /* malloc'd */
} InitBlock;

struct RpylLangHost {
    LangEntry plugins[RPYL_EXTLANG_MAX_PLUGINS];
    int plugin_count;

    InitBlock inits[RPYL_EXTLANG_MAX_INITS];
    int init_count;
};

/* -------------------------- small helpers -------------------------- */

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

static char* xstrdup(const char* s) {
    size_t n;
    char* r;
    if (!s) return NULL;
    n = strlen(s);
    r = (char*)rpyl_malloc(n + 1);
    if (!r) return NULL;
    memcpy(r, s, n + 1);
    return r;
}

static void safe_copy(char* dst, size_t dst_size, const char* src) {
    rpyl_strcpy_trunc(dst, dst_size, src ? src : "");
}

static void initblock_free(InitBlock* b) {
    if (!b) return;
    if (b->code) {
        rpyl_free(b->code);
        b->code = NULL;
    }
    b->lang[0] = 0;
    b->priority = 0;
    b->seq = 0;
    b->start_line = 0;
}

static LangEntry* find_plugin(RpylLangHost* host, const char* lang) {
    int i;
    if (!host || !lang) return NULL;
    for (i = 0; i < host->plugin_count; i++) {
        if (strcmp(host->plugins[i].lang, lang) == 0) return &host->plugins[i];
    }
    return NULL;
}

/* Parse headers like:
      init python:
      init -1 lua:
   Returns 1 on match.
*/
static int parse_init_header(const char* line_trimmed, char* out_lang, size_t out_lang_sz, int* out_priority) {
    const char* p;
    int pri;
    const char* pri_start;

    int has_pri;

    char lang_buf[RPYL_EXTLANG_MAX_LANG];
    size_t li;
    size_t k;

    if (!line_trimmed) return 0;

    p = line_trimmed;
    if (!(tolower((unsigned char)p[0])=='i' && tolower((unsigned char)p[1])=='n' && tolower((unsigned char)p[2])=='i' && tolower((unsigned char)p[3])=='t')) return 0;
    p += 4;

    if (*p != ' ' && *p != '\t') {
        /* Require whitespace after 'init' to avoid matching identifiers like 'initialize'. */
        return 0;
    }

    /* skip ws */
    p = lstrip(p);

    /* optional integer priority */
    pri = 0;
    has_pri = 0;
    pri_start = p;

    if (*p == '-' || isdigit((unsigned char)*p)) {
        char* endptr;
        long v;
        const char* after;

        endptr = NULL;
        v = strtol(p, &endptr, 10);
        if (endptr && endptr != p) {
            /* must be separated by whitespace */
            after = endptr;
            if (*after == ' ' || *after == '\t') {
                pri = (int)v;
                has_pri = 1;
                p = lstrip(after);
            } else {
                /* Was something like init 10python: -> not valid */
                p = pri_start;
            }
        }
    }

    (void)has_pri;

    /* language name */
    if (!*p) return 0;

    li = 0;
    while (*p && *p != ':' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        if (li + 1 < sizeof(lang_buf)) {
            lang_buf[li++] = *p;
        }
        p++;
    }
    lang_buf[li] = 0;

    /* Normalize language name to lowercase for matching. */
    for (k = 0; lang_buf[k]; k++) {
        lang_buf[k] = (char)tolower((unsigned char)lang_buf[k]);
    }

    /* skip ws before ':' */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != ':') return 0;

    if (lang_buf[0] == 0) return 0;

    safe_copy(out_lang, out_lang_sz, lang_buf);
    if (out_priority) *out_priority = pri;
    return 1;
}

/* Compare init blocks in Ren'Py-like order: priority low->high, then seq (file order). */
static int initblock_cmp(const void* a, const void* b) {
    const InitBlock* A;
    const InitBlock* B;
    A = (const InitBlock*)a;
    B = (const InitBlock*)b;
    if (A->priority < B->priority) return -1;
    if (A->priority > B->priority) return 1;
    if (A->seq < B->seq) return -1;
    if (A->seq > B->seq) return 1;
    return 0;
}

/* Build code string by stripping the minimum indentation from raw block lines. */
static char* build_stripped_code(char** raw_lines, int raw_count, int* out_min_indent) {
    int min_indent;
    int min_set;
    int i;

    size_t total;
    char* out;
    size_t pos;

    if (!raw_lines || raw_count <= 0) {
        if (out_min_indent) *out_min_indent = 0;
        return xstrdup("");
    }

    min_indent = 0;
    min_set = 0;

    for (i = 0; i < raw_count; i++) {
        const char* ln;
        int ind;
        ln = raw_lines[i] ? raw_lines[i] : "";
        if (line_is_blank(ln)) continue;
        ind = count_indent(ln);
        if (!min_set || ind < min_indent) {
            min_indent = ind;
            min_set = 1;
        }
    }

    if (!min_set) min_indent = 0;
    if (out_min_indent) *out_min_indent = min_indent;

    /* compute output size */
    total = 0;
    for (i = 0; i < raw_count; i++) {
        const char* ln;
        ln = raw_lines[i] ? raw_lines[i] : "";
        if (line_is_blank(ln)) {
            total += 1; /* '\n' */
        } else {
            size_t len;
            size_t strip;
            len = strlen(ln);
            strip = (len < (size_t)min_indent) ? len : (size_t)min_indent;
            total += (len - strip);
        }
    }

    out = (char*)rpyl_malloc(total + 1);
    if (!out) return NULL;

    pos = 0;
    for (i = 0; i < raw_count; i++) {
        const char* ln;
        ln = raw_lines[i] ? raw_lines[i] : "";
        if (line_is_blank(ln)) {
            out[pos++] = '\n';
            continue;
        }
        {
            size_t len;
            size_t strip;
            len = strlen(ln);
            strip = (len < (size_t)min_indent) ? len : (size_t)min_indent;
            memcpy(out + pos, ln + strip, len - strip);
            pos += (len - strip);
        }
    }
    out[pos] = 0;
    return out;
}

/* Creates a temporary file path.
   NOTE: This is intentionally simple and portable; embedders can wrap/replace it.
*/
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
        char tpl[RPYL_EXTLANG_MAX_TPL];
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

/* -------------------------- public API -------------------------- */

RpylLangHost* rpyl_langhost_create(void) {
    RpylLangHost* host;
    host = (RpylLangHost*)rpyl_calloc(1, sizeof(RpylLangHost));
    if (!host) return NULL;
    host->plugin_count = 0;
    host->init_count = 0;
    return host;
}

void rpyl_langhost_destroy(RpylLangHost* host) {
    int i;
    if (!host) return;
    for (i = 0; i < host->init_count; i++) {
        initblock_free(&host->inits[i]);
    }
    rpyl_free(host);
}

int rpyl_langhost_register(
    RpylLangHost* host,
    const char* lang,
    RpylInitLangFn fn,
    void* userdata
) {
    char norm[RPYL_EXTLANG_MAX_LANG];
    size_t k;
    int i;

    if (!host || !lang || !lang[0] || !fn) return 0;

    safe_copy(norm, sizeof(norm), lang);
    for (k = 0; norm[k]; k++) {
        norm[k] = (char)tolower((unsigned char)norm[k]);
    }

    /* Update existing */
    for (i = 0; i < host->plugin_count; i++) {
        if (strcmp(host->plugins[i].lang, norm) == 0) {
            host->plugins[i].fn = fn;
            host->plugins[i].userdata = userdata;
            return 1;
        }
    }

    if (host->plugin_count >= RPYL_EXTLANG_MAX_PLUGINS) return 0;

    safe_copy(host->plugins[host->plugin_count].lang, sizeof(host->plugins[host->plugin_count].lang), norm);
    host->plugins[host->plugin_count].fn = fn;
    host->plugins[host->plugin_count].userdata = userdata;
    host->plugin_count++;
    return 1;
}

int rpyl_load_file_extlang(
    RpylContext* ctx,
    RpylLangHost* host,
    const char* path
) {
    int i;

    FILE* in;
    char tmp_path[RPYL_EXTLANG_MAX_PATH];
    FILE* out;

    char line[RPYL_EXTLANG_MAX_LINE];
    int line_no;

    int in_init;
    int init_header_indent;
    char init_lang[RPYL_EXTLANG_MAX_LANG];
    int init_priority;
    int init_start_line;
    int init_seq;

    char** raw_lines;
    int raw_count;
    int raw_cap;

    char re_line[RPYL_EXTLANG_MAX_LINE];
    int has_re_line;

    if (!ctx || !host || !path) return 0;

    /* Clear any previous init blocks kept in host (host is reusable). */
    for (i = 0; i < host->init_count; i++) {
        initblock_free(&host->inits[i]);
    }
    host->init_count = 0;

    in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "[RpylExtLang] Failed to open: %s\n", path);
        return 0;
    }

    if (!make_temp_path(tmp_path, sizeof(tmp_path))) {
        fprintf(stderr, "[RpylExtLang] Failed to create temp path\n");
        fclose(in);
        return 0;
    }

    out = fopen(tmp_path, "wb");
    if (!out) {
        fprintf(stderr, "[RpylExtLang] Failed to open temp file: %s\n", tmp_path);
        fclose(in);
        return 0;
    }

    line_no = 0;

    in_init = 0;
    init_header_indent = 0;
    init_lang[0] = 0;
    init_priority = 0;
    init_start_line = 0;
    init_seq = 0;

    raw_lines = NULL;
    raw_count = 0;
    raw_cap = 0;

    re_line[0] = 0;
    has_re_line = 0;

    while (1) {
        const char* trimmed;

        if (!has_re_line) {
            if (!fgets(line, (int)sizeof(line), in)) break;
        } else {
            rpyl_strcpy_trunc(line, sizeof(line), re_line);
            has_re_line = 0;
        }

        line_no++;
        trimmed = lstrip(line);

        if (!in_init) {
            /* Detect only top-level init blocks (indent == 0). */
            if (count_indent(line) == 0) {
                char lang_buf[RPYL_EXTLANG_MAX_LANG];
                int pri_buf;
                pri_buf = 0;
                if (parse_init_header(trimmed, lang_buf, sizeof(lang_buf), &pri_buf)) {
                    in_init = 1;
                    init_header_indent = count_indent(line);
                    safe_copy(init_lang, sizeof(init_lang), lang_buf);
                    init_priority = pri_buf;
                    init_start_line = line_no;
                    init_seq = host->init_count; /* stable */

                    /* reset raw */
                    for (i = 0; i < raw_count; i++) {
                        if (raw_lines && raw_lines[i]) rpyl_free(raw_lines[i]);
                    }
                    rpyl_free(raw_lines);
                    raw_lines = NULL;
                    raw_count = 0;
                    raw_cap = 0;

                    /* Do not write the header to output. */
                    continue;
                }
            }

            /* Normal line: copy to temp script. */
            fputs(line, out);
            continue;
        }

        /* We are inside init block: collect raw lines until indentation returns to header level. */
        if (!line_is_blank(line) && count_indent(line) <= init_header_indent) {
            /* End of init block. Reprocess this line as normal. */
            has_re_line = 1;
            rpyl_strcpy_trunc(re_line, sizeof(re_line), line);

            /* Finalize init block */
            if (host->init_count >= RPYL_EXTLANG_MAX_INITS) {
                fprintf(stderr, "[RpylExtLang] Too many init blocks (max %d)\n", RPYL_EXTLANG_MAX_INITS);
                fclose(in);
                fclose(out);
                remove(tmp_path);
                return 0;
            }

            {
                int min_indent;
                char* code;
                InitBlock* b;

                min_indent = 0;
                code = build_stripped_code(raw_lines, raw_count, &min_indent);
                if (!code) code = xstrdup("");

                b = &host->inits[host->init_count++];
                memset(b, 0, sizeof(*b));
                safe_copy(b->lang, sizeof(b->lang), init_lang);
                b->priority = init_priority;
                b->seq = init_seq;
                b->start_line = init_start_line;
                b->code = code;

                (void)min_indent;
            }

            /* Reset state */
            in_init = 0;
            init_lang[0] = 0;
            init_priority = 0;
            init_start_line = 0;

            /* Free raw lines for next block */
            for (i = 0; i < raw_count; i++) {
                if (raw_lines && raw_lines[i]) rpyl_free(raw_lines[i]);
            }
            rpyl_free(raw_lines);
            raw_lines = NULL;
            raw_count = 0;
            raw_cap = 0;

            continue;
        }

        /* Still inside init: keep line */
        if (raw_count + 1 > raw_cap) {
            int new_cap;
            char** nl;

            new_cap = (raw_cap == 0) ? 16 : (raw_cap * 2);
            nl = (char**)rpyl_realloc(raw_lines, (size_t)new_cap * sizeof(char*));
            if (!nl) {
                fprintf(stderr, "[RpylExtLang] OOM while capturing init block\n");
                fclose(in);
                fclose(out);
                remove(tmp_path);
                return 0;
            }
            raw_lines = nl;
            raw_cap = new_cap;
        }
        raw_lines[raw_count++] = xstrdup(line);
    }

    /* EOF while inside init block: finalize it. */
    if (in_init) {
        if (host->init_count >= RPYL_EXTLANG_MAX_INITS) {
            fprintf(stderr, "[RpylExtLang] Too many init blocks (max %d)\n", RPYL_EXTLANG_MAX_INITS);
            fclose(in);
            fclose(out);
            remove(tmp_path);
            return 0;
        }

        {
            int min_indent;
            char* code;
            InitBlock* b;

            min_indent = 0;
            code = build_stripped_code(raw_lines, raw_count, &min_indent);
            if (!code) code = xstrdup("");

            b = &host->inits[host->init_count++];
            memset(b, 0, sizeof(*b));
            safe_copy(b->lang, sizeof(b->lang), init_lang);
            b->priority = init_priority;
            b->seq = init_seq;
            b->start_line = init_start_line;
            b->code = code;

            (void)min_indent;
        }
    }

    for (i = 0; i < raw_count; i++) {
        if (raw_lines && raw_lines[i]) rpyl_free(raw_lines[i]);
    }
    rpyl_free(raw_lines);

    fclose(in);
    fclose(out);

    /* Load the sanitized script via the original core API. */
    if (!rpyl_load_file(ctx, tmp_path)) {
        fprintf(stderr, "[RpylExtLang] Failed to load sanitized script\n");
        remove(tmp_path);
        return 0;
    }

    /* Execute init blocks in Ren'Py-like order: low->high priority, then file order. */
    if (host->init_count > 1) {
        qsort(host->inits, (size_t)host->init_count, sizeof(host->inits[0]), initblock_cmp);
    }

    for (i = 0; i < host->init_count; i++) {
        InitBlock* b;
        LangEntry* plug;
        int ok;

        b = &host->inits[i];
        plug = find_plugin(host, b->lang);
        if (!plug || !plug->fn) {
            fprintf(stderr, "[RpylExtLang] No handler registered for language '%s' (at %s:%d)\n",
                b->lang, path, b->start_line);
            remove(tmp_path);
            return 0;
        }

        ok = plug->fn(ctx, plug->userdata, b->lang, b->priority, b->code ? b->code : "", path, b->start_line);
        if (!ok) {
            fprintf(stderr, "[RpylExtLang] Handler for '%s' failed (at %s:%d)\n",
                b->lang, path, b->start_line);
            remove(tmp_path);
            return 0;
        }
    }

    /* Cleanup temp file */
    remove(tmp_path);
    return 1;
}
