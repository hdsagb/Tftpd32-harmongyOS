#pragma once
/* 宿主（Windows/MSVC）上的 <ifaddrs.h> 替身：仅供编译，getifaddrs 实现在 host_stubs.c */
#include <sys/socket.h>

struct ifaddrs
{
    struct ifaddrs *ifa_next;
    char           *ifa_name;
    unsigned int    ifa_flags;
    struct sockaddr *ifa_addr;
    struct sockaddr *ifa_netmask;
    struct sockaddr *ifa_dstaddr;
    void           *ifa_data;
};

#ifdef __cplusplus
extern "C" {
#endif
int getifaddrs (struct ifaddrs **ifap);
void freeifaddrs (struct ifaddrs *ifa);
#ifdef __cplusplus
}
#endif
