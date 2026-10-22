//////////////////////////////////////////////////////////
// test_ip_util.c —— IP 工具函数
// 被测：ip_pal.c 的 AddrInc（地址递增与回绕）、haddrtoa/atohaddr（MAC 文本转换）
//////////////////////////////////////////////////////////
#include "headers.h"
#include "test.h"

static unsigned int host_of (const char *szIp)
{ struct in_addr a;  a.s_addr = inet_addr (szIp);  return ntohl (a.s_addr); }

static unsigned int addr_inc (const char *szIp)
{ struct in_addr a;  a.s_addr = inet_addr (szIp);  return AddrInc (a); }

static void test_addr_inc (void)
{
    TEST_CASE_BEGIN ("AddrInc: 常规递增");
    CHECK_EQ_INT (addr_inc ("192.168.1.5"),   host_of ("192.168.1.6"));
    CHECK_EQ_INT (addr_inc ("192.168.1.253"), host_of ("192.168.1.254"));

    TEST_CASE_BEGIN ("AddrInc: 跳过 .0/.1（.254 -> +3，.255 -> +2）");
    CHECK_EQ_INT (addr_inc ("192.168.1.254"), host_of ("192.168.2.1"));
    CHECK_EQ_INT (addr_inc ("192.168.1.255"), host_of ("192.168.2.1"));

    TEST_CASE_BEGIN ("AddrInc: 32 位回绕（255.255.255.255 + 2 -> 1）");
    /* .255 分支返回 addr+2，unsigned int 截断为 1（与原版 32 位 unsigned long 行为一致） */
    CHECK_EQ_INT (addr_inc ("255.255.255.255"), 1u);
}

static void test_haddr (void)
{
char sz [32];
unsigned char mac [6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
unsigned char back [6];

    TEST_CASE_BEGIN ("haddrtoa / atohaddr");
    CHECK_STR (haddrtoa (mac, 6, '.'), "02.11.22.33.44.55");
    CHECK_STR (haddrtoa (mac, 6, ':'), "02:11:22:33:44:55");

    strcpy (sz, "02:11:22:33:44:55");
    memset (back, 0, sizeof back);
    atohaddr ((const unsigned char *) sz, back, 6);
    CHECK_EQ_MEM (back, mac, 6);
}

void run_ip_util_tests (void)
{
    TEST_SUITE ("IP utilities");
    test_addr_inc ();
    test_haddr ();
}
