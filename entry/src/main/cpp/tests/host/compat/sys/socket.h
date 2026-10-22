#pragma once
/* 宿主（Windows/MSVC）上的 <sys/socket.h> 替身
   自带最小 POSIX 形状，不引入 Winsock：否则 Windows SDK 的
   HWND/HINSTANCE/SOCKET/pollfd 会与 pal_compat.h 的移植形状冲突。
   网络函数在宿主测试中不会被调用，实现在 host_stubs.c。 */
#include <stddef.h>
#include <stdint.h>

#define AF_UNSPEC       0
#define AF_INET         2
#define AF_INET6        23
#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOL_SOCKET      1
#define SO_REUSEADDR    2
#define IPPROTO_UDP     17
#define AI_PASSIVE      1
#define NI_MAXSERV      32
#define NI_MAXHOST     1025
#define INADDR_ANY      0x00000000u
#define INADDR_LOOPBACK 0x7F000001u
#define INADDR_NONE     0xFFFFFFFFu

struct in_addr
{
    uint32_t s_addr;
};

struct in6_addr
{
    unsigned char s6_addr[16];
};

struct sockaddr
{
    unsigned short sa_family;
    char           sa_data[14];
};

struct sockaddr_in
{
    short          sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

struct sockaddr_in6
{
    short           sin6_family;
    unsigned short  sin6_port;
    uint32_t        sin6_flowinfo;
    struct in6_addr sin6_addr;
    uint32_t        sin6_scope_id;
};

struct sockaddr_storage
{
    short ss_family;
    char  __ss_pad[126];
};

struct addrinfo
{
    int              ai_flags;
    int              ai_family;
    int              ai_socktype;
    int              ai_protocol;
    int              ai_addrlen;
    struct sockaddr *ai_addr;
    char            *ai_canonname;
    struct addrinfo *ai_next;
};

typedef unsigned int socklen_t;

#ifdef __cplusplus
extern "C" {
#endif

/* 字节序：宿主 x86 为小端，与网络序相反 */
unsigned int   htonl (unsigned int hostlong);
unsigned int   ntohl (unsigned int netlong);
unsigned short htons (unsigned short hostshort);
unsigned short ntohs (unsigned short netshort);

unsigned int inet_addr (const char *cp);
char        *inet_ntoa (struct in_addr in);

int  socket (int domain, int type, int protocol);
int  bind (int s, const struct sockaddr *addr, int addrlen);
int  connect (int s, const struct sockaddr *addr, int addrlen);
int  sendto (int s, const void *buf, size_t len, int flags,
             const struct sockaddr *to, int tolen);
int  setsockopt (int s, int level, int optname, const void *optval, int optlen);
int  getaddrinfo (const char *node, const char *service,
                  const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo (struct addrinfo *res);

#ifdef __cplusplus
}
#endif
