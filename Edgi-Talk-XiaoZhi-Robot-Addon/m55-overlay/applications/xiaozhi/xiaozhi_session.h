#ifndef XIAOZHI_SESSION_H
#define XIAOZHI_SESSION_H
#include <stddef.h>
#include <string.h>

#define XZ_SESSION_ID_CAPACITY 64

/* Never reply under a truncated/stale session ID. */
static inline int xz_copy_session_id(char *dest, size_t capacity, const char *source)
{
    if (!dest || !capacity) return 0;
    dest[0] = '\0';
    if (!source || !source[0] || strlen(source) >= capacity) return 0;
    memcpy(dest, source, strlen(source) + 1);
    return 1;
}
#endif
