#pragma once
/* 宿主（Windows/MSVC）上的 <unistd.h> 替身：PE 的 CRT 以 _ 前缀提供同名函数 */
#include <io.h>
#include <process.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
typedef int ssize_t;
#define _SSIZE_T_DEFINED
#endif

#define close   _close
#define read    _read
#define write   _write
#define unlink  _unlink
#define getpid  _getpid

#ifdef __cplusplus
extern "C" {
#endif

/* 用 CRT 的 _sleep 实现，避免与 pal_compat.h 的 Sleep(DWORD) 声明冲突 */
static __inline unsigned int sleep (unsigned int sec)
{
    _sleep ((unsigned long) sec * 1000UL);
    return 0;
}

static __inline int usleep (unsigned int usec)
{
    _sleep (((unsigned long) usec + 999UL) / 1000UL);
    return 0;
}

#ifdef __cplusplus
}
#endif
