//////////////////////////////////////////////////////////
// test_settings.c —— 配置读写与键名接线
// 被测：settings_pal.c 的 Tftpd32ReadSettings / Pal_SetSettingInt / Pal_GetSettingInt
// A1「Enable IPv4 接线」的回归：ini 中的 Enable IPv4/Enable IPv6 必须落入 sSettings
//////////////////////////////////////////////////////////
#include "headers.h"
#include "test.h"

#include <stdio.h>

static const char *kIni = "host_settings_test.ini";

static void write_ini (const char *body)
{
FILE *f = fopen (kIni, "w");
    if (f != NULL)
    {
        fputs (body, f);
        fclose (f);
    }
}

static void test_ipv4_key (void)
{
unsigned v = 0;
char path [PATH_MAX];

    TEST_CASE_BEGIN ("Enable IPv4 / Enable IPv6 键接线");
    write_ini ("Enable IPv4=0\nEnable IPv6=1\nTftpPort=6901\n");   /* TftpPort 即原版 Port 键 */
    strcpy (path, kIni);
    Pal_SetConfigPath (path);
    Tftpd32ReadSettings ();

    CHECK_EQ_INT (sSettings.bIPv4, 0);        /* A1：关闭后必须为 0 */
    CHECK_EQ_INT (sSettings.bIPv6, 1);
    CHECK_EQ_INT (sSettings.Port, 6901);      /* 同表其它键不受影响 */

    /* 反向：重新写入开启，再次装载 */
    write_ini ("Enable IPv4=1\nEnable IPv6=1\n");
    Tftpd32ReadSettings ();
    CHECK_EQ_INT (sSettings.bIPv4, 1);
    CHECK_EQ_INT (sSettings.bIPv6, 1);

    TEST_CASE_BEGIN ("Enable IPv4 键：napi 读写路径");
    CHECK_TRUE (Pal_SetSettingInt ("Enable IPv4", 0));      /* 与原版键名逐字一致 */
    CHECK_TRUE (Pal_GetSettingInt ("Enable IPv4", & v));
    CHECK_EQ_INT (v, 0);
    CHECK_TRUE (Pal_SetSettingInt ("Enable IPv4", 1));
    CHECK_TRUE (Pal_GetSettingInt ("Enable IPv4", & v));
    CHECK_EQ_INT (v, 1);
    CHECK_TRUE (! Pal_SetSettingInt ("Enable IPv4", 1) || sSettings.bIPv4 == 1);

    /* 键名写错时不应误命中（防止大小写/拼写漂移） */
    CHECK_TRUE (! Pal_SetSettingInt ("EnableIPv4", 0));

    /* 收尾：恢复默认并删除临时 ini */
    sSettings.bIPv4 = 1;
    Pal_SetConfigPath ("");
    remove (kIni);
}

void run_settings_tests (void)
{
    TEST_SUITE ("Settings keys");
    test_ipv4_key ();
}
