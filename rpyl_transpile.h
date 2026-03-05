#ifndef RPYL_TRANSPILE_H
#define RPYL_TRANSPILE_H

#include "rpyl_bytecode.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Write a C89 source file that embeds the given bytecode as static arrays.

   The output defines:
     - const RpylBytecode* <symbol_prefix>_get_bytecode(void);

   Notes:
     - The returned pointer must be treated as read-only.
     - If symbol_prefix is NULL or empty, "rpyl_program" is used.
*/
int rpyl_transpile_bytecode_to_c(
    const RpylBytecode* bc,
    const char* out_c_path,
    const char* symbol_prefix
);

#ifdef __cplusplus
}
#endif

#endif /* RPYL_TRANSPILE_H */
