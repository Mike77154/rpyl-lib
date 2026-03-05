#ifndef RPYL_STREAM_H
#define RPYL_STREAM_H

/*
    rpyl_stream.h

    Minimal character stream abstraction for lexer/parser.

    Supports getc/ungetc semantics needed by the lexer.
*/

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpylStream {
    void* user;
    int (*getc_fn)(void* user);          /* returns [0..255] or EOF (-1) */
    int (*ungetc_fn)(int c, void* user); /* returns c on success */
} RpylStream;

#define rpyl_stream_getc(S)   (((S) && (S)->getc_fn) ? (S)->getc_fn((S)->user) : (-1))
#define rpyl_stream_ungetc(S, C) (((S) && (S)->ungetc_fn) ? (S)->ungetc_fn((C), (S)->user) : (-1))

#ifdef __cplusplus
}
#endif

#endif /* RPYL_STREAM_H */
