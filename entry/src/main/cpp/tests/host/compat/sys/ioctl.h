#pragma once
/* 宿主（Windows/MSVC）上的 <sys/ioctl.h> 替身：统一走 ioctlsocket */
#include <sys/socket.h>
#ifdef __cplusplus
extern "C" {
#endif
int ioctl (int fd, long cmd, ...);
#ifdef __cplusplus
}
#endif
