/* Derived from Tftpd32/Tftpd64 (Copyright (C) Philippe Jounin, EUPL-1.2),
 * modified for HarmonyOS. This file is distributed under GPLv2 as part
 * of the Tftpd32 for HarmonyOS project. */
//////////////////////////////////////////////////////////
// dns_hosts.c —— 本地静态解析表
// 文件：<BaseDirectory>/hosts.txt
// 行格式：主机名  IPv4  IPv6      （任一列可用 "-" 表示缺省）
// 例：myhost 192.168.1.100 -
//////////////////////////////////////////////////////////
#include "headers.h"

BOOL PalDnsHostsLookup (const char *szName, unsigned int *plIpv4, IN6_ADDR *pIpv6,
                        int *pnNbV6, char *szCname)
{
char  szPath [_MAX_PATH];
char  szLine [512];
FILE *f;
int   nFound = FALSE;

    * plIpv4   = INADDR_NONE;
    * pnNbV6   = 0;
    szCname[0] = 0;

    snprintf (szPath, sizeof szPath, "%s/hosts.txt", sSettings.szWorkingDirectory);
    f = fopen (szPath, "r");
    if (f == NULL)  return FALSE;

    while (fgets (szLine, sizeof szLine, f) != NULL)
    {
        char szHost [128] = "", szV4 [64] = "", szV6 [128] = "";
        char *p = szLine;

        while (*p == ' ' || *p == '\t')  p++;
        if (*p == '#' || *p == ';' || *p == '\n' || *p == 0)  continue;
        if (sscanf (p, "%127s %63s %127s", szHost, szV4, szV6) < 2)  continue;
        if (lstrcmpi (szHost, (char *) szName) != 0)  continue;

        if (szV4[0] != '-' && szV4[0] != 0)
        {
            struct in_addr s4;
            if (inet_pton (AF_INET, szV4, & s4) == 1)
                * plIpv4 = s4.s_addr;       /* s_addr 即网络序 4 字节 */
        }
        if (szV6[0] != 0 && szV6[0] != '-' && * pnNbV6 < 10)
        {
            if (inet_pton (AF_INET6, szV6, & pIpv6[* pnNbV6]) == 1)
                (* pnNbV6) ++;
        }
        nFound = TRUE;
        break;
    }
    fclose (f);
    return nFound;
}
