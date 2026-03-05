#ifndef RPYL_POLYSYM_H
#define RPYL_POLYSYM_H

/*
    RPYL Extension Module: Polyglot Symbols ($ foo(1,2,3))

    Goal
    ----
    Add Ren'Py-like "$" call statements that dispatch into *external language plugins*,
    WITHOUT modifying the existing RPYL core.

    - No language is hardcoded.
    - Languages are already plugged via rpyl_extlang (init <lang>: blocks).
    - This module adds a symbol table + a "$" preprocessor that translates:

        $ foo(1, 2, "hi", $bar)

      into a normal RPYL command line:

        __sym foo 1 2 "hi" $bar

      so the existing parser/runtime can handle it.

    - The __sym command then looks up symbol "foo" in a host-managed symbol table
      and invokes the callback that the *language plugin* exported during init.

    Important Notes
    ---------------
    * This module does NOT parse or validate the code inside init <lang>: blocks.
      That code is treated as raw text for the plugin.

    * The core RPYL runtime currently caps command argv to 8 items.
      Because __sym's first argument is the symbol name, that leaves up to 7 user args.
*/

#include "rpyl.h"
#include "rpyl_extlang.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RpylSymbolHost RpylSymbolHost;

/*
    Invoked when the script executes "$ foo(...)".

    - args/argc are the *resolved* tokens after RPYL's $var/define substitution.
    - All values are passed as strings.

    Return non-zero on success.
*/
typedef int (*RpylSymbolFn)(
    RpylContext* ctx,
    void* userdata,
    const char* symbol,
    const char** args,
    int argc
);

/* Create/destroy the symbol registry. */
RpylSymbolHost* rpyl_symbolhost_create(void);
void rpyl_symbolhost_destroy(RpylSymbolHost* host);

/* Optional: override the internal dispatcher command name (default: "__sym"). */
void rpyl_symbolhost_set_dispatch_command(RpylSymbolHost* host, const char* command_name);
const char* rpyl_symbolhost_get_dispatch_command(RpylSymbolHost* host);

/* Export (register) a symbol. If it already exists, it is replaced. */
int rpyl_symbolhost_export(
    RpylSymbolHost* host,
    const char* symbol,
    RpylSymbolFn fn,
    void* userdata
);

/* Remove a symbol by name. Returns non-zero if removed. */
int rpyl_symbolhost_remove(RpylSymbolHost* host, const char* symbol);

/*
    Attach a symbol host to a context.

    This also registers the dispatcher command (default __sym) into ctx.
    The module stores this attachment in an internal table.
*/
int rpyl_symbolhost_attach(RpylContext* ctx, RpylSymbolHost* host);
void rpyl_symbolhost_detach(RpylContext* ctx);

/* Get the symbol host attached to ctx (or NULL). */
RpylSymbolHost* rpyl_symbolhost_get(RpylContext* ctx);

/*
    Combined loader:

    - Preprocesses the script to translate "$ foo(1,2,3)" lines into "__sym ...".
      (Outside init <lang>: blocks; init blocks are kept raw.)
    - Then delegates to rpyl_load_file_extlang() to extract/execute init blocks.

    Return non-zero on success.
*/
int rpyl_load_file_extlang_symbols(
    RpylContext* ctx,
    RpylLangHost* lang_host,
    RpylSymbolHost* sym_host,
    const char* path
);

#ifdef __cplusplus
}
#endif

#endif /* RPYL_POLYSYM_H */
