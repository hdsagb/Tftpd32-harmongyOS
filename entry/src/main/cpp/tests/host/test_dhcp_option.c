//////////////////////////////////////////////////////////
// test_dhcp_option.c —— DHCP 自定义选项值 / 表达式翻译
// 被测：bootpd_util.c 的 TranslateParam2Value、TranslateExp
// 覆盖原版语法：a(IP 列表) s(字符串) I/i/N/n(整数) b(字节列表) x(十六进制) 与纯字符串回退
//////////////////////////////////////////////////////////
#include "headers.h"
#include "dhcp.h"
#include "test.h"

#include <stdio.h>
#include <stdlib.h>

static void test_ip_list (void)
{
char buf [256];
struct in_addr a, b, ip0;
int n;

    TEST_CASE_BEGIN ("a: IP 列表");
    memset (buf, 0, sizeof buf);
    memset (& ip0, 0, sizeof ip0);
    n = TranslateParam2Value (buf, (int) sizeof buf, "a 1.2.3.4 5.6.7.8", ip0, "", 0);
    CHECK_EQ_INT (n, 8);
    memcpy (& a, buf, 4);
    memcpy (& b, buf + 4, 4);
    CHECK_EQ_INT (a.s_addr, inet_addr ("1.2.3.4"));
    CHECK_EQ_INT (b.s_addr, inet_addr ("5.6.7.8"));
}

static void test_string (void)
{
char buf [256];
struct in_addr ip;

    TEST_CASE_BEGIN ("s: 字符串与 $IP$ 表达式");
    memset (buf, 0, sizeof buf);
    ip.s_addr = inet_addr ("192.168.1.7");

    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "s hello", ip, "", 0), 5);
    CHECK_STR (buf, "hello");

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "s $IP$", ip, "", 0), 11);
    CHECK_STR (buf, "192.168.1.7");
}

static void test_integers (void)
{
char buf [256];
unsigned short u16;
unsigned int   u32;
struct in_addr ip = { 0 };

    TEST_CASE_BEGIN ("I/i/N/n: 整数与网络序");
    ip.s_addr = inet_addr ("192.168.1.1");

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "I 300", ip, "", 0), 2);
    memcpy (& u16, buf, 2);
    CHECK_EQ_INT (u16, 300);

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "i 70000", ip, "", 0), 4);
    memcpy (& u32, buf, 4);
    CHECK_EQ_INT (u32, 70000);

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "N 300", ip, "", 0), 2);
    CHECK_EQ_INT ((unsigned char) buf [0], 0x01);
    CHECK_EQ_INT ((unsigned char) buf [1], 0x2C);

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "n 16909060", ip, "", 0), 4);
    CHECK_EQ_INT ((unsigned char) buf [0], 0x01);
    CHECK_EQ_INT ((unsigned char) buf [1], 0x02);
    CHECK_EQ_INT ((unsigned char) buf [2], 0x03);
    CHECK_EQ_INT ((unsigned char) buf [3], 0x04);
}

static void test_byte_list (void)
{
char buf [256];
struct in_addr ip = { 0 };

    TEST_CASE_BEGIN ("b: 字节列表");
    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "b 1 2 3", ip, "", 0), 3);
    CHECK_EQ_INT ((unsigned char) buf [0], 1);
    CHECK_EQ_INT ((unsigned char) buf [1], 2);
    CHECK_EQ_INT ((unsigned char) buf [2], 3);

    /* 空格/点/冒号分隔均应接受 */
    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "b 10.20:30", ip, "", 0), 3);
    CHECK_EQ_INT ((unsigned char) buf [0], 10);
    CHECK_EQ_INT ((unsigned char) buf [1], 20);
    CHECK_EQ_INT ((unsigned char) buf [2], 30);
}

static void test_hex_list (void)
{
char buf [256];
unsigned short u16;
unsigned int   u32;
struct in_addr ip = { 0 };

    TEST_CASE_BEGIN ("x: 十六进制字段（1/2/4 字节）");
    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "x 1 2", ip, "", 0), 2);
    CHECK_EQ_INT ((unsigned char) buf [0], 1);
    CHECK_EQ_INT ((unsigned char) buf [1], 2);

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "x ABCD", ip, "", 0), 2);
    memcpy (& u16, buf, 2);
    CHECK_EQ_INT (u16, 0xABCD);

    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "x 12345678", ip, "", 0), 4);
    memcpy (& u32, buf, 4);
    CHECK_EQ_INT (u32, 0x12345678);

    /* 16 位十六进制：原版 cvt 表按 min(q-p,8) 取长度，cvt[8]=4，
       故 16 位输入仍只输出 4 字节（低 32 位）。适配版只拷贝 4 字节，
       不会像原版 %lx 在 LP64 下那样越界写 8 字节。 */
    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "x ABCDEF0123456789", ip, "", 0), 4);
    memcpy (& u32, buf, 4);
    CHECK_EQ_INT (u32, 0x23456789);
}

static void test_fallback (void)
{
char buf [256];
struct in_addr ip = { 0 };

    TEST_CASE_BEGIN ("无前缀：原样保留");
    memset (buf, 0, sizeof buf);
    CHECK_EQ_INT (TranslateParam2Value (buf, (int) sizeof buf, "tftp-server", ip, "", 0), 11);
    CHECK_STR (buf, "tftp-server");
}

static void test_expressions (void)
{
char to [DHCP_FILE_LEN];
struct in_addr ip;
const char mac [6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

    TEST_CASE_BEGIN ("TranslateExp: $IP$ / $ARCH$ / $MAC$ / $BootFileName$");
    ip.s_addr = inet_addr ("10.0.0.9");

    memset (to, 0, sizeof to);
    CHECK_STR (TranslateExp ("$IP$", to, ip, mac, 0), "10.0.0.9");

    memset (to, 0, sizeof to);
    CHECK_STR (TranslateExp ("pxe-$ARCH$", to, ip, mac, 7), "pxe-x64");

    memset (to, 0, sizeof to);
    CHECK_STR (TranslateExp ("$MAC$", to, ip, mac, 0), "02.11.22.33.44.55");

    memset (to, 0, sizeof to);
    strcpy (to, "boot.img");
    CHECK_STR (TranslateExp ("$BootFileName$", to, ip, mac, 0), "boot.img");

    memset (to, 0, sizeof to);
    CHECK_STR (TranslateExp ("plain", to, ip, mac, 0), "plain");
}

void run_dhcp_option_tests (void)
{
    TEST_SUITE ("DHCP option value translation");
    test_ip_list ();
    test_string ();
    test_integers ();
    test_byte_list ();
    test_hex_list ();
    test_fallback ();
    test_expressions ();
}
