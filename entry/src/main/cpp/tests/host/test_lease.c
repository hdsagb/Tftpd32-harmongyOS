//////////////////////////////////////////////////////////
// test_lease.c —— DHCP 租约池
// 被测：bootpd_util.c 的 ReorderLeases / DHCPSearchByIP / DHCPSearchByMacAddress
// 说明：宿主上 sSettings.bPersLeases=0，租约不会真正落盘（SaveKey 桩）
//////////////////////////////////////////////////////////
#include "headers.h"
#include "test.h"

#include <stdlib.h>
#include <time.h>

static struct LL_IP *make_lease (const char *szIp, const unsigned char *mac)
{
struct LL_IP *p = (struct LL_IP *) malloc (sizeof * p);
    memset (p, 0, sizeof * p);
    p->dwIP.s_addr = inet_addr (szIp);
    memcpy (p->sMacAddr, mac, 6);
    return p;
}

static void test_lease_search (void)
{
static const unsigned char macA [6] = { 0x02, 0, 0, 0, 0, 0x0A };
static const unsigned char macB [6] = { 0x02, 0, 0, 0, 0, 0x0B };
static const unsigned char macC [6] = { 0x02, 0, 0, 0, 0, 0x0C };
struct in_addr addr;
struct LL_IP   *p;
BOOL  expired = FALSE;

    TEST_CASE_BEGIN ("租约池：排序 + 按 IP/MAC 查找");
    tFirstIP = (struct LL_IP **) calloc (3, sizeof * tFirstIP);
    tMAC     = (struct LL_IP **) calloc (3, sizeof * tMAC);
    tFirstIP [0] = make_lease ("192.168.1.30", macC);   /* 故意乱序 */
    tFirstIP [1] = make_lease ("192.168.1.10", macA);
    tFirstIP [2] = make_lease ("192.168.1.20", macB);
    nAllocatedIP = 3;
    memcpy (tMAC, tFirstIP, 3 * sizeof * tFirstIP);

    /* 与 LoadLeases 相同的排序与编号流程 */
    qsort (tMAC, 3, sizeof * tMAC, MACCompare);
    qsort (tFirstIP, 3, sizeof * tFirstIP, QsortCompare);
    ReorderLeases ();
    CHECK_EQ_INT (tFirstIP [0]->dwAllocNum, 0);
    CHECK_EQ_INT (tFirstIP [1]->dwAllocNum, 1);
    CHECK_EQ_INT (tFirstIP [2]->dwAllocNum, 2);
    CHECK_EQ_INT (ntohl (tFirstIP [1]->dwIP.s_addr), ntohl (inet_addr ("192.168.1.20")));

    addr.s_addr = inet_addr ("192.168.1.20");
    p = DHCPSearchByIP (& addr, & expired);
    CHECK_TRUE (p != NULL);
    CHECK_EQ_INT (p ? p->dwAllocNum : -1, 1);
    CHECK_TRUE (p == DHCPSearchByMacAddress (macB, 6));

    addr.s_addr = inet_addr ("192.168.1.99");            /* 池内不存在 */
    CHECK_TRUE (DHCPSearchByIP (& addr, & expired) == NULL);

    TEST_CASE_BEGIN ("租约池：过期判定");
    addr.s_addr = inet_addr ("192.168.1.10");
    p = DHCPSearchByIP (& addr, & expired);
    CHECK_TRUE (p != NULL);
    CHECK_TRUE (expired == TRUE);                        /* tRenewed = 0 → 已过期 */

    p->tRenewed = time (NULL);
    sParamDHCP.nLease = 120;                             /* 分钟 */
    p = DHCPSearchByIP (& addr, & expired);
    CHECK_TRUE (p != NULL);
    CHECK_TRUE (expired == FALSE);                       /* 刚刚续约 → 未过期 */

    FreeLeases (TRUE);
    CHECK_TRUE (tFirstIP == NULL && tMAC == NULL);
    nAllocatedIP = 0;
}

void run_lease_tests (void)
{
    TEST_SUITE ("DHCP lease pool");
    test_lease_search ();
}
