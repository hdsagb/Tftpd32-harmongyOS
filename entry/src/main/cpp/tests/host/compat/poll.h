#pragma once
/* 宿主（Windows/MSVC）上的 <poll.h> 替身：声明足够编译，实现在 host_stubs.c
   宏与结构体均加保护，避免与 Windows SDK 自带定义冲突 */
#ifndef POLLIN
#define POLLIN   0x0001
#endif
#ifndef POLLOUT
#define POLLOUT  0x0004
#endif
#ifndef POLLERR
#define POLLERR  0x0008
#endif

#ifndef _HOST_POLLFD_DEFINED
#define _HOST_POLLFD_DEFINED
struct pollfd
{
    int   fd;
    short events;
    short revents;
};
#endif

#ifdef __cplusplus
extern "C" {
#endif
int poll (struct pollfd *fds, unsigned long nfds, int timeout);

#ifdef __cplusplus
}
#endif
