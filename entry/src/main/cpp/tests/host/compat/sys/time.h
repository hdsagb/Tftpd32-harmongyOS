#pragma once
/* 宿主（Windows/MSVC）上的 <sys/time.h> 替身：timeval/timezone + gettimeofday
   不引入 <windows.h>（它会带进 winsock.h 的 SOCKET/HWND 等定义，与 pal_compat.h 冲突） */
#include <stddef.h>
#include <time.h>

struct timeval
{
    long tv_sec;
    long tv_usec;
};

struct timezone
{
    int tz_minuteswest;
    int tz_dsttime;
};

#ifdef __cplusplus
extern "C" {
#endif

static __inline int gettimeofday (struct timeval *tv, struct timezone *tz)
{
    (void) tz;
    if (tv != NULL)
    {
        tv->tv_sec  = (long) time (NULL);
        tv->tv_usec = 0;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
