#ifndef RPYL_PARSER_H
#define RPYL_PARSER_H

#include <stdio.h>
#include <stddef.h>

#include "rpyl_stream.h"
#include "rpyl_ast.h"

/* Optional arena-based parse (C89-friendly fast allocations). */
AstNode* rpyl_parse_ex(FILE* f, RpylArena* arena);
AstNode* rpyl_parse(FILE* f);

/* Parse from an abstract stream (getc/ungetc). */
AstNode* rpyl_parse_stream_ex(RpylStream* s, RpylArena* arena);
AstNode* rpyl_parse_stream(RpylStream* s);

/* Parse from an in-memory buffer (not necessarily NUL-terminated). */
AstNode* rpyl_parse_buffer_ex(const char* buf, size_t len, RpylArena* arena);
AstNode* rpyl_parse_buffer(const char* buf, size_t len);

#endif
