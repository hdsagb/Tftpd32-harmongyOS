// m2tool.c —— 设备端 M2/M3 协议测试工具
// 用法：m2tool syslog <ip> <port> <count> <len>
//       m2tool dns    <ip> <port> <qname> <qtype>
//       m2tool ntp    <ip> <port>
//       m2tool dhcp   <discover|request|release|decline> <ip> <port> <mac> [requested-ip]
//       例：m2tool dhcp discover 127.0.0.1 6700 02:11:22:33:44:55
//           m2tool dhcp request  127.0.0.1 6700 02:11:22:33:44:55 192.168.1.100
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <poll.h>

static int udp_sock (const char *ip, int port, struct sockaddr_in *to)
{
int s;
struct timeval tv = { 3, 0 };

    memset (to, 0, sizeof *to);
    to->sin_family = AF_INET;
    to->sin_port   = htons ((unsigned short) port);
    inet_pton (AF_INET, ip, & to->sin_addr);
    s = socket (AF_INET, SOCK_DGRAM, 0);
    setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, & tv, sizeof tv);
    return s;
}

static int do_syslog (const char *ip, int port, int count, int len)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), i;
char buf [2048];

    for (i = 1; i <= count; i++)
    {
        int n = snprintf (buf, sizeof buf, "<134>Mar  1 12:00:00 m2test: message #%d ", i);
        while (n < len && n < (int) sizeof buf - 1)  buf[n++] = 'x';
        buf[n++] = '\n';
        sendto (s, buf, n, 0, (struct sockaddr *) & to, sizeof to);
        usleep (50000);
    }
    printf ("sent %d syslog messages (%d bytes each)\n", count, len);
    close (s);
    return 0;
}

static int do_ntp (const char *ip, int port)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), n;
unsigned char pkt [48];
unsigned long sec;

    memset (pkt, 0, sizeof pkt);
    pkt[0] = 0x1B;                       /* LI=0 VN=3 mode=3(client) */
    pkt[40] = 0xAB;  pkt[41] = 0xCD;     /* 随意的时间戳，用于 loop 检测 */
    sendto (s, pkt, 48, 0, (struct sockaddr *) & to, sizeof to);
    n = (int) recv (s, pkt, sizeof pkt, 0);
    if (n < 48)  { printf ("timeout / short reply %d\n", n);  close (s);  return 1; }

    sec = ((unsigned long) pkt[40] << 24) | ((unsigned long) pkt[41] << 16)
        | ((unsigned long) pkt[42] << 8) | pkt[43];
    printf ("reply %d bytes mode=%d stratum=%d xmt=%lu (unix %lu)\n",
            n, pkt[0] & 7, pkt[1], sec, sec - 2208988800UL);
    close (s);
    return 0;
}

static int do_dns (const char *ip, int port, const char *name, int qtype)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), n, i, pos;
unsigned char q [512], r [512];
const char *p = name;

    memset (q, 0, sizeof q);
    q[0] = 0x12;  q[1] = 0x34;  q[2] = 0x01;  q[3] = 0x00;
    q[5] = 1;                                    /* qdcount = 1 */
    pos = 12;
    while (*p)
    {
        const char *dot = strchr (p, '.');
        int l = dot ? (int)(dot - p) : (int) strlen (p);
        q[pos++] = (unsigned char) l;
        memcpy (q + pos, p, l);  pos += l;
        p = dot ? dot + 1 : p + l;
    }
    q[pos++] = 0;
    q[pos++] = (unsigned char)(qtype >> 8);  q[pos++] = (unsigned char)(qtype & 0xFF);
    q[pos++] = 0;  q[pos++] = 1;

    sendto (s, q, pos, 0, (struct sockaddr *) & to, sizeof to);
    n = (int) recv (s, r, sizeof r, 0);
    if (n < 12)  { printf ("timeout / short reply %d\n", n);  close (s);  return 1; }

    printf ("reply %d bytes id=%02x%02x rcode=%d an=%d\n",
            n, r[0], r[1], r[3] & 0x0F, (r[6] << 8) | r[7]);

    pos = 12;
    while (pos < n && r[pos] != 0)  pos += r[pos] + 1;
    pos += 5;                                     /* 0 + qtype + qclass */

    for (i = 0; i < ((r[6] << 8) | r[7]) && pos + 12 <= n; i++)
    {
        if ((r[pos] & 0xC0) == 0xC0)  pos += 2;
        else { while (pos < n && r[pos] != 0)  pos += r[pos] + 1;  pos++; }
        if (pos + 10 > n)  break;
        {
            int type = (r[pos] << 8) | r[pos + 1];
            /* RDATA 紧跟 10 字节资源头（type2/class2/ttl4/rdlen2），
               原先误用 +10/+12，导致 rdlen 读到 rdata 首字节、rdata 越界 */
            int rdlen = (r[pos + 8] << 8) | r[pos + 9];
            char txt [64] = "";

            if (type == 1 && rdlen == 4)
                snprintf (txt, sizeof txt, "%d.%d.%d.%d",
                          r[pos + 10], r[pos + 11], r[pos + 12], r[pos + 13]);
            else if (type == 28 && rdlen == 16)
                inet_ntop (AF_INET6, r + pos + 10, txt, sizeof txt);
            printf ("  type=%d ttl=%d rdata=%s\n", type,
                    (r[pos + 4] << 24) | (r[pos + 5] << 16) | (r[pos + 6] << 8) | r[pos + 7], txt);
            pos += 12 + rdlen;
        }
    }
    close (s);
    return 0;
}

static int do_bad (const char *ip, int port, int len)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), n, i;
unsigned char buf [512];
struct pollfd pf;

    if (len < 1)  len = 1;
    if (len > (int) sizeof buf)  len = (int) sizeof buf;
    for (i = 0; i < len; i++)  buf[i] = (unsigned char) (i * 37 + 0xFF);
    sendto (s, buf, len, 0, (struct sockaddr *) & to, sizeof to);
    pf.fd = s;  pf.events = POLLIN;  pf.revents = 0;
    if (poll (& pf, 1, 1500) <= 0)
    {
        printf ("sent %d malformed bytes, reply=none\n", len);
        close (s);
        return 0;
    }
    n = (int) recv (s, buf, sizeof buf, 0);
    printf ("sent %d malformed bytes, reply=%d\n", len, n);
    close (s);
    return 0;
}

/* ---------- DHCP（RFC 2131）报文级测试 ----------
   只构造最小的 BOOTP/DHCP 报文，用于 DORA 各阶段的服务器行为验证。
   flags=0 → 请求服务器按单播应答（回环自测 / 与原版 netsh 抓包对拍均可）。 */
static int parse_mac (const char *s, unsigned char *mac)
{
    int v[6], i;
    if (sscanf (s, "%x:%x:%x:%x:%x:%x", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5]) != 6)
        return -1;
    for (i = 0; i < 6; i++)  mac[i] = (unsigned char) v[i];
    return 0;
}

static unsigned char *put_opt (unsigned char *p, int code, int len, const void *val)
{
    *p++ = (unsigned char) code;
    *p++ = (unsigned char) len;
    if (len > 0)  { memcpy (p, val, (size_t) len);  p += len; }
    return p;
}

static int do_dhcp (const char *action, const char *ip, int port,
                    const char *macs, const char *reqIp)
{
struct sockaddr_in to;
int s = udp_sock (ip, port, & to), n;
unsigned char pkt [576], r [576];
unsigned char mac [6];
unsigned char *p;
unsigned int yi, si, xid = 0x5A5A0000u | (unsigned int) (getpid () & 0xFFFF);
int msgType;
struct timeval tv = { 3, 0 };

    if (parse_mac (macs, mac) != 0)  { printf ("bad mac\n");  close (s);  return 2; }
    if (strcmp (action, "discover") == 0)       msgType = 1;
    else if (strcmp (action, "request") == 0)   msgType = 3;
    else if (strcmp (action, "release") == 0)   msgType = 7;
    else if (strcmp (action, "decline") == 0)   msgType = 4;
    else { printf ("bad dhcp action\n");  close (s);  return 2; }

    memset (pkt, 0, sizeof pkt);
    pkt[0] = 1;            /* BOOTREQUEST */
    pkt[1] = 1;            /* HTYPE_ETHER */
    pkt[2] = 6;            /* HLEN */
    pkt[4] = (unsigned char) (xid >> 24);  pkt[5] = (unsigned char) (xid >> 16);
    pkt[6] = (unsigned char) (xid >> 8);   pkt[7] = (unsigned char) xid;
    memcpy (pkt + 28, mac, 6);                    /* chaddr */
    p = pkt + 236;
    *p++ = 0x63;  *p++ = 0x82;  *p++ = 0x53;  *p++ = 0x63;   /* magic cookie */
    p = put_opt (p, 53, 1, & (unsigned char) { (unsigned char) msgType });
    {
        unsigned char prl [4] = { 1, 3, 6, 15 };
        p = put_opt (p, 55, 4, prl);
    }
    if (reqIp != NULL && reqIp[0] != 0 && (msgType == 3 || msgType == 4))
    {
        unsigned int a = inet_addr (reqIp);
        p = put_opt (p, 50, 4, & a);
        /* 注意：DECLINE 也不能填 yiaddr —— 服务器见到 yiaddr 非 0 会直接丢弃报文 */
    }
    *p++ = 255;
    n = (int) (p - pkt);

    if (msgType == 7)      /* RELEASE 按 RFC 直接单播到服务器，不期待应答 */
    {
        sendto (s, pkt, n, 0, (struct sockaddr *) & to, sizeof to);
        printf ("sent DHCPRELEASE for %s mac %s\n", reqIp ? reqIp : "-", macs);
        close (s);
        return 0;
    }

    sendto (s, pkt, n, 0, (struct sockaddr *) & to, sizeof to);
    {
        /* 部分 musl/鸿蒙实现上 SO_RCVTIMEO 对 UDP 不生效 → 用 poll 保证 3s 超时 */
        struct pollfd pf;
        pf.fd = s;  pf.events = POLLIN;  pf.revents = 0;
        if (poll (& pf, 1, 3000) <= 0)
        { printf ("timeout / no reply\n");  close (s);  return 1; }
    }
    setsockopt (s, SOL_SOCKET, SO_RCVTIMEO, & tv, sizeof tv);
    n = (int) recv (s, r, sizeof r, 0);
    if (n < 240)  { printf ("timeout / short reply %d\n", n);  close (s);  return 1; }

    memcpy (& yi, r + 16, 4);
    memcpy (& si, r + 20, 4);
    {
        /* inet_ntoa 返回静态缓冲，同一次 printf 里调用两次会被后一次覆盖 → 先各自转成字符串 */
        char szYi [32], szSi [32];
        snprintf (szYi, sizeof szYi, "%s", inet_ntoa (*(struct in_addr *) & yi));
        snprintf (szSi, sizeof szSi, "%s", inet_ntoa (*(struct in_addr *) & si));
        printf ("reply %d bytes op=%d yiaddr=%s siaddr=%s file=%s\n",
                n, r[0], szYi, szSi, r + 108);
    }
    {
        unsigned char *q = r + 240, *end = r + n;
        while (q + 2 <= end && *q != 255 && *q != 0)
        {
            int code = q[0], len = q[1];
            char txt [128] = "";
            if (q + 2 + len > end)  break;
            if (code == 53)  snprintf (txt, sizeof txt, "msgtype=%d", q[2]);
            else if (code == 1 && len == 4)
                snprintf (txt, sizeof txt, "%s", inet_ntoa (*(struct in_addr *) (q + 2)));
            else if ((code == 3 || code == 6 || code == 42) && len >= 4)
                snprintf (txt, sizeof txt, "%s", inet_ntoa (*(struct in_addr *) (q + 2)));
            else if (code == 51 && len == 4)
            {
                unsigned int lt;  memcpy (& lt, q + 2, 4);
                snprintf (txt, sizeof txt, "lease=%us", ntohl (lt));
            }
            else if (code == 54 && len == 4)
                snprintf (txt, sizeof txt, "%s", inet_ntoa (*(struct in_addr *) (q + 2)));
            else if (code == 15 || code == 66 || code == 12)
                snprintf (txt, sizeof txt, "%.*s", len, (char *) q + 2);
            printf ("  opt %3d len %d %s\n", code, len, txt);
            q += 2 + len;
        }
    }
    close (s);
    return 0;
}

int main (int argc, char **argv)
{
    setvbuf (stdout, NULL, _IONBF, 0);   /* hdc shell 下 stdout 非 tty，默认全缓冲会丢失超时信息 */
    if (argc >= 6 && strcmp (argv[1], "dhcp") == 0)
        return do_dhcp (argv[2], argv[3], atoi (argv[4]), argv[5],
                        argc >= 7 ? argv[6] : NULL);
    if (argc < 4)  { printf ("usage: m2tool syslog|dns|ntp|bad|dhcp ...\n");  return 2; }
    if (strcmp (argv[1], "syslog") == 0 && argc >= 6)
        return do_syslog (argv[2], atoi (argv[3]), atoi (argv[4]), atoi (argv[5]));
    if (strcmp (argv[1], "ntp") == 0)
        return do_ntp (argv[2], atoi (argv[3]));
    if (strcmp (argv[1], "dns") == 0 && argc >= 6)
        return do_dns (argv[2], atoi (argv[3]), argv[4], atoi (argv[5]));
    if (strcmp (argv[1], "bad") == 0 && argc >= 5)
        return do_bad (argv[2], atoi (argv[3]), atoi (argv[4]));
    printf ("bad arguments\n");
    return 2;
}
