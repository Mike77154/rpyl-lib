#ifndef RPYL_EXTLANG_H
#define RPYL_EXTLANG_H

/*
    RPYL Extension Module: init <lang>:

    This module adds a Ren'Py-like "init" phase for *arbitrary* languages WITHOUT
    modifying the existing RPYL core.

    It works by preprocessing a script file, extracting top-level blocks like:

        init python:
            def foo():
                print("hello")

        init -1 lua:
            function foo() print('hi') end

    The extracted code blocks are dispatched to host-registered handlers based on
    the language name ("python", "lua", "ruby", "darkbasicpro", ...).

    After preprocessing, the remaining script (with init blocks removed) is
    loaded normally via rpyl_load_file().

    Init blocks are executed once, during load, in priority order from lowest to
    highest (like Ren'Py). Within the same file and priority, blocks run
    top-to-bottom.
*/

#include "rpyl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpylLangHost RpylLangHost;

/*
    Called once per extracted init block.

    - ctx:        the RPYL context that will run the script.
    - userdata:   pointer provided at registration time.
    - lang:       language name from the init header (e.g. "python").
    - priority:   parsed priority (defaults to 0).
    - code:       raw code block with the first indentation level stripped.
    - filename:   script file path (as passed to rpyl_load_file_extlang).
    - start_line: 1-based line number where the init header begins.

    Return non-zero to indicate success.
*/
typedef int (*RpylInitLangFn)(
    RpylContext* ctx,
    void* userdata,
    const char* lang,
    int priority,
    const char* code,
    const char* filename,
    int start_line
);

/* Create/destroy the language host registry. */
RpylLangHost* rpyl_langhost_create(void);
void rpyl_langhost_destroy(RpylLangHost* host);

/*
    Register a handler for a language.

    Example:
        rpyl_langhost_register(host, "lua", on_lua_init, lua_state);

    Returns non-zero on success.
*/
int rpyl_langhost_register(
    RpylLangHost* host,
    const char* lang,
    RpylInitLangFn fn,
    void* userdata
);

/*
    Load a script file with support for init <lang>: blocks.

    Steps:
      1) Preprocess file: extract init blocks, write a temporary script without them.
      2) rpyl_load_file(ctx, temp_script)
      3) Execute extracted init blocks via registered language handlers.
      4) Delete temporary script.

    Returns non-zero on success.
*/
int rpyl_load_file_extlang(
    RpylContext* ctx,
    RpylLangHost* host,
    const char* path
);

#ifdef __cplusplus
}
#endif

#endif /* RPYL_EXTLANG_H */
