#ifndef RPYL_PORT_H
#define RPYL_PORT_H

/*
    rpyl_port.h

    C89 portability helpers.

    Goals:
    - Avoid snprintf/vsnprintf (not ISO C89).
    - Provide safe string copy utilities that always NUL-terminate.
    - Keep dependencies minimal.
*/

#include <stddef.h> /* size_t */
#include <string.h> /* strlen, memcpy */

#ifdef __cplusplus
extern "C" {
#endif

/* Copy src into dst, truncating to fit, always NUL-terminating. */
static void rpyl_strcpy_trunc(char* dst, size_t dst_sz, const char* src) {
    size_t n;
    if (!dst || dst_sz == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    n = strlen(src);
    if (n >= dst_sz) n = dst_sz - 1;
    if (n > 0) memcpy(dst, src, n);
    dst[n] = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* RPYL_PORT_H */
