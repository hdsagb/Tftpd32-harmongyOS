#pragma once
/* 宿主（Windows/MSVC）上的 <strings.h> 替身 */
#include <string.h>
#include <ctype.h>

#ifdef __cplusplus
extern "C" {
#endif

static __inline int strcasecmp (const char *a, const char *b)
{
    while (*a && *b)
    {
        int ca = tolower ((unsigned char) *a), cb = tolower ((unsigned char) *b);
        if (ca != cb)  return ca - cb;
        a++;  b++;
    }
    return tolower ((unsigned char) *a) - tolower ((unsigned char) *b);
}

static __inline int strncasecmp (const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n && a[i] && b[i]; i++)
    {
        int ca = tolower ((unsigned char) a[i]), cb = tolower ((unsigned char) b[i]);
        if (ca != cb)  return ca - cb;
    }
    if (i == n)  return 0;
    return tolower ((unsigned char) a[i]) - tolower ((unsigned char) b[i]);
}

#ifdef __cplusplus
}
#endif
