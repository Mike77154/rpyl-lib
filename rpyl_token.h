#ifndef RPYL_TOKEN_H
#define RPYL_TOKEN_H

#include "rpyl_config.h"

typedef enum {
    TOK_EOF,
    TOK_NEWLINE,
    TOK_INDENT,
    TOK_DEDENT,

    TOK_IDENTIFIER,
    TOK_STRING,
    TOK_NUMBER,

    TOK_COLON,
    TOK_EQUAL,
    TOK_DEFINE,
    TOK_LABEL
} RpylTokenType;

typedef struct {
    RpylTokenType type;
    char text[RPYL_TOKEN_MAX_TEXT];
    int line;
} RpylToken;

#endif
