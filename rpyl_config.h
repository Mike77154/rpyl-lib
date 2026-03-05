#ifndef RPYL_CONFIG_H
#define RPYL_CONFIG_H

/*
    rpyl_config.h

    Compile-time configuration knobs.

    - You may override any of these by defining them BEFORE including any rpyl_*.h,
      or by passing -DNAME=value to your compiler.

    The defaults are chosen to match the original library behavior.
*/

/* ---------------- Lexer ---------------- */
#ifndef RPYL_LEXER_MAX_INDENT
#define RPYL_LEXER_MAX_INDENT 32
#endif

#ifndef RPYL_LEXER_MAX_LABEL_KW
#define RPYL_LEXER_MAX_LABEL_KW 32
#endif

/* ---------------- Tokens ---------------- */
#ifndef RPYL_TOKEN_MAX_TEXT
#define RPYL_TOKEN_MAX_TEXT 128
#endif

/* ---------------- AST ---------------- */
#ifndef RPYL_AST_MAX_NAME
#define RPYL_AST_MAX_NAME 128
#endif

#ifndef RPYL_AST_MAX_VALUE
#define RPYL_AST_MAX_VALUE 128
#endif

#ifndef RPYL_AST_MAX_ARGS
#define RPYL_AST_MAX_ARGS 8
#endif

#ifndef RPYL_AST_MAX_ARG_TEXT
#define RPYL_AST_MAX_ARG_TEXT 128
#endif

/* ---------------- Parser helpers ---------------- */
#ifndef RPYL_PARSER_MAX_PREARGS
#define RPYL_PARSER_MAX_PREARGS 8
#endif

#ifndef RPYL_PARSER_MAX_BLOCK_NAME
#define RPYL_PARSER_MAX_BLOCK_NAME 128
#endif

/* ---------------- Public context ---------------- */
#ifndef RPYL_CTX_MAX_USER_COMMANDS
#define RPYL_CTX_MAX_USER_COMMANDS 64
#endif

#ifndef RPYL_CTX_MAX_CMD_NAME
#define RPYL_CTX_MAX_CMD_NAME 128
#endif

#ifndef RPYL_CTX_MAX_START_BLOCK
#define RPYL_CTX_MAX_START_BLOCK 128
#endif

/* ---------------- Runtime ---------------- */
#ifndef RPYL_RUNTIME_MAX_COMMANDS
#define RPYL_RUNTIME_MAX_COMMANDS 64
#endif

#ifndef RPYL_RUNTIME_MAX_DEFINES
#define RPYL_RUNTIME_MAX_DEFINES 256
#endif

#ifndef RPYL_RUNTIME_MAX_STACK
#define RPYL_RUNTIME_MAX_STACK 64
#endif

#ifndef RPYL_RUNTIME_MAX_SCOPES
#define RPYL_RUNTIME_MAX_SCOPES 32
#endif

#ifndef RPYL_RUNTIME_MAX_VARS_PER_SCOPE
#define RPYL_RUNTIME_MAX_VARS_PER_SCOPE 64
#endif

#ifndef RPYL_RUNTIME_MAX_ONCE_NODES
#define RPYL_RUNTIME_MAX_ONCE_NODES 256
#endif

#ifndef RPYL_RUNTIME_MAX_ENTERED_BLOCKS
#define RPYL_RUNTIME_MAX_ENTERED_BLOCKS 128
#endif

#ifndef RPYL_RUNTIME_MAX_NAME
#define RPYL_RUNTIME_MAX_NAME 128
#endif

#ifndef RPYL_RUNTIME_MAX_VALUE
#define RPYL_RUNTIME_MAX_VALUE 256
#endif

#ifndef RPYL_RUNTIME_MAX_ARGS
#define RPYL_RUNTIME_MAX_ARGS 8
#endif

#ifndef RPYL_RUNTIME_MAX_JOINED
#define RPYL_RUNTIME_MAX_JOINED 256
#endif

/* ---------------- Extlang (optional) ---------------- */
#ifndef RPYL_EXTLANG_MAX_PLUGINS
#define RPYL_EXTLANG_MAX_PLUGINS 32
#endif

#ifndef RPYL_EXTLANG_MAX_INITS
#define RPYL_EXTLANG_MAX_INITS 256
#endif

/* ---------------- PolySym ---------------- */
#ifndef RPYL_POLYSYM_MAX_SYMBOLS
#define RPYL_POLYSYM_MAX_SYMBOLS 128
#endif

#ifndef RPYL_POLYSYM_MAX_ATTACHMENTS
#define RPYL_POLYSYM_MAX_ATTACHMENTS 32
#endif

#ifndef RPYL_POLYSYM_MAX_NAME
#define RPYL_POLYSYM_MAX_NAME 64
#endif

#ifndef RPYL_POLYSYM_MAX_CMD
#define RPYL_POLYSYM_MAX_CMD 64
#endif

/* ---------------- Transpiler ---------------- */
#ifndef RPYL_TRANSPILE_MAX_PREFIX
#define RPYL_TRANSPILE_MAX_PREFIX 128
#endif

/* ---------------- Extlang buffers ---------------- */
#ifndef RPYL_EXTLANG_MAX_LANG
#define RPYL_EXTLANG_MAX_LANG 64
#endif

#ifndef RPYL_EXTLANG_MAX_PATH
#define RPYL_EXTLANG_MAX_PATH 512
#endif

#ifndef RPYL_EXTLANG_MAX_LINE
#define RPYL_EXTLANG_MAX_LINE 4096
#endif

#ifndef RPYL_EXTLANG_MAX_TPL
#define RPYL_EXTLANG_MAX_TPL 32
#endif

/* ---------------- PolySym buffers ---------------- */
#ifndef RPYL_POLYSYM_MAX_TOKEN
#define RPYL_POLYSYM_MAX_TOKEN 256
#endif

#ifndef RPYL_POLYSYM_MAX_PATH
#define RPYL_POLYSYM_MAX_PATH 512
#endif

#ifndef RPYL_POLYSYM_MAX_LINE
#define RPYL_POLYSYM_MAX_LINE 4096
#endif

#ifndef RPYL_POLYSYM_MAX_OUT_LINE
#define RPYL_POLYSYM_MAX_OUT_LINE 8192
#endif

#ifndef RPYL_POLYSYM_MAX_TPL
#define RPYL_POLYSYM_MAX_TPL 32
#endif

/* ---------------- IO ---------------- */
#ifndef RPYL_IO_VFS_CHUNK
#define RPYL_IO_VFS_CHUNK 4096
#endif

#endif /* RPYL_CONFIG_H */
