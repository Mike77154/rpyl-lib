#ifndef RPYL_LEXER_H
#define RPYL_LEXER_H

#include <stdio.h>

#include "rpyl_stream.h"
#include "rpyl_token.h"

/* Reset global lexer state (line counter + indentation stack). */
void rpyl_lexer_reset(void);

/* Configurar keyword que el lexer tratará como TOK_LABEL (default: "label") */
void rpyl_lexer_set_label_keyword(const char* kw);

/* Lex one token from an abstract stream.
   Returns 1 if a token was produced, 0 when TOK_EOF is produced. */
int rpyl_lex_stream(RpylStream* s, RpylToken* out);

/* Backwards compatible FILE* wrapper.
   Returns 1 if a token was produced, 0 when TOK_EOF is produced. */
int rpyl_lex(FILE* f, RpylToken* out);

#endif
