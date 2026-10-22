#pragma once
/* 仅宿主测试使用的预置头（MSVC 上通过 /FI 强制包含）
   目的：补齐被测源码在 POSIX 环境下存在、而 Windows CRT 缺失的宏与类型；
   不改变任何被测逻辑。 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <direct.h>

/* musl 的 <sys/types.h> 提供 u_intN_t，MSVC 没有 */
#ifndef _HOST_U_INT_TYPES_DEFINED
#define _HOST_U_INT_TYPES_DEFINED
typedef uint8_t  u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK  0x0004
#endif
#ifndef O_CLOEXEC
#define O_CLOEXEC   0
#endif
#ifndef SHUT_RD
#define SHUT_RD     0
#endif
#ifndef SHUT_WR
#define SHUT_WR     1
#endif
#ifndef SHUT_RDWR
#define SHUT_RDWR   2
#endif
#ifndef S_IRUSR
#define S_IRUSR     _S_IREAD
#endif
#ifndef S_IWUSR
#define S_IWUSR     _S_IWRITE
#endif
#ifndef PATH_MAX
#define PATH_MAX    260
#endif

/* settings_pal.c 用到的 POSIX 文件系统接口：映射到 CRT 等价物 */
#ifndef S_ISDIR
#define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
#endif
#define mkdir(p, m) _mkdir(p)
#define getcwd      _getcwd
#define stat        _stat

static __inline char *realpath (const char *path, char *resolved)
{ return _fullpath (resolved, path, 1024); }

#ifdef _MSC_VER
/* MSVC 的 CRT 把 BSD 名字放在 <corecrt_math.h> 之外，被测代码未使用，无需补充 */
#pragma warning(disable: 4996)   /* strcpy/sscanf 等：与原工程一致，不做安全函数替换 */
#pragma warning(disable: 4005)   /* pal_compat.h 有意以 Windows 同名宏覆盖 CRT 定义 */
#endif
