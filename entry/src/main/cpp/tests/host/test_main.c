//////////////////////////////////////////////////////////
// test_main.c —— 宿主单元测试入口（不依赖设备）
//////////////////////////////////////////////////////////
#include <stdio.h>

int g_pass = 0;
int g_fail = 0;

int run_dhcp_option_tests (void);
int run_ip_util_tests (void);
int run_lease_tests (void);
int run_settings_tests (void);

int main (void)
{
    fprintf (stderr, "==== Tftpd32 HarmonyOS host tests ====\n");

    run_dhcp_option_tests ();
    run_ip_util_tests ();
    run_lease_tests ();
    run_settings_tests ();

    fprintf (stderr, "==== %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
