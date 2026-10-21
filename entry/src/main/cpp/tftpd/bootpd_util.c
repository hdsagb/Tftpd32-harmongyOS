//////////////////////////////////////////////////////
//
// Projet TFTPD32.  January 2006 Ph.jounin
// Projet DHCPD32.  January 2006 Ph.jounin
// File bootpd_util.c:    Manage BOOTP/DHCP protocols
//
// source released under European Union Public License
//
// 鸿蒙适配说明（与 reference/tftpd64/src/_services/bootpd_util.c 的差异）：
//   1) 去掉 Windows IPHlpApi / iphlpapi.h：ArpDeleteHost 降级为空实现，
//      FindAdapterIP（Win32 网卡索引）不移植，改由 ip_pal.c 的 getifaddrs 实现
//   2) 注册表读写改由 registry_pal.c（键名与原版一致）
//   3) LP64：TranslateParam2Value 的 IP/整数选项改为 32 位定宽（原 unsigned long 为 8 字节）
//   4) DHCPSendFrom / DHCPMultiSend（需读 Windows 路由表）移到 bootpd.c 用单 socket 广播实现
//////////////////////////////////////////////////////

#include "headers.h"

#include <stdio.h>          // sscanf is used
#include "dhcp.h"           // DHCP_FILE_LEN 等

#include "threading.h"
#include "bootpd_functions.h"
#include "linked_list.h"        /* WaitForMsgQueueToFinish / LL_ID_SETTINGS */

/* DHCP 数据库全局（原版定义在 dhcp_gui / tftpd32.c） */
struct LL_IP **tFirstIP = NULL;    // sorted array of pointers on struct LL_IP
struct LL_IP **tMAC      = NULL;   // sorted pointers to elements in tFirstIP, indexed by MAC
int            nAllocatedIP = 0;
struct S_DHCP_Param sParamDHCP;
struct S_DHCP_Param sGuiParamDHCP;

// IANA PXE Architecture Name (conflicting with RFC 4578)
const struct S_PXE_Option93_Architecture sPXE_Architecture [] =
{
        //Type   Architecture Name
        //----   ---------------- -
        0,    "x86",
        1,    "NEC/PC98",       // deprecated
        2,    "Itanium",
        3,    "DEC Alpha",      // deprecated
        4,    "x86",            // deprecated
        5,    "Intel Lean Client", // deprecated
        6,    "x86",
        7,    "x64",
        8,    "Xscale",         // deprecated
        9,    "x64",            // may be either x86 or x64
       10,    "arm32",
       11,    "arm64",
};


// 原版通过 GetIpNetTable/DeleteIpNetEntry 刷新 Windows ARP 表；
// 鸿蒙应用无 NET_ADMIN 权限，无法操作邻居表 → 空实现（见适配方案 §5 平台偏差）
int ArpDeleteHost (struct in_addr addr)
{ (void) addr;  return 0; }


//////////////////////////////////////////////////////////////////////////////////////////////
// AddOption Translation
// Translates the option values into a value
// Use the SnmpCmd syntax ("a ip @" "s string", "i integer", "x string", "u unsigned")
//////////////////////////////////////////////////////////////////////////////////////////////
int TranslateParam2Value (char *buffer, int len, const char *opt_val, struct in_addr ip,
                          const char *tMac, unsigned short iLastArch)
{
char sz[256];
const char *p, *q;
int  nIPAddr, nLen, c;
// the number of bytes used by n hexa digit 0xABCDE which has 5 digits will form a 4-byte number)
static const char cvt[] = { 0, 1, 1, 2, 2, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8 };

   if (opt_val[1] == ' ')
   {
       p = opt_val+2;	// p points on data
	   switch (opt_val [0])
	   {
			case 'a' : // list of IP address
				for (nIPAddr=0 ;  *p!=0 && nIPAddr*4<len-4; nIPAddr++ )
				{
				  * ((unsigned int *) buffer + nIPAddr) = inet_addr (p);
				   while ( *p!=0 &&  (isdigit(*p) || *p=='.') )  p++;  // go to end of address
				   while ( *p!=0 && *p==' ' ) p++;	// skip spaces
				}
				return nIPAddr * 4;
			case 's' : // string
					lstrcpyn (sz, p, len);
					TranslateExp (sz, buffer, ip, tMac, iLastArch);
					buffer [len-1] = 0;
					return lstrlen (buffer);
			case 'I' :	// integer
			    * (unsigned short *) buffer = atoi (p);
				return sizeof (unsigned short);
			case 'i' :	// integer
			    * (unsigned int *) buffer = atoi (p);
				return 4;
			case 'N' :	// integer network order
			    * (unsigned short *) buffer = htons (atoi (p));
				return sizeof (unsigned short);
			case 'n' :	// integer long network order
			    * (unsigned int *) buffer = htonl (atoi (p));
				return 4;
			case 'b' :	// list of 1 (b)yte digits
				for (nLen=0 ;  *p!=0 && nLen<len-1; nLen++ )
				{
					sscanf(p, "%u", & c);
					buffer [nLen] = (char) c;
				    while ( *p!=0 && isdigit(*p) ) p++;	// to next digit
 				    while ( *p!=0 && (*p==' ' || *p=='.' || *p==':') ) p++;	// skip spaces
				}
				return nLen;
			case 'x' :	// list of he(x) digits
				for (nLen=0 ;  *p!=0 && nLen<len-8; p = q )
				{
					unsigned int  u32;
					unsigned long long u64;
					for ( q = p; isxdigit(*q) ; q++ );  // search next field
					switch (  cvt [ min (q-p, 8) ]  )
					{
						case 1 : sscanf(p, "%x", & c);  buffer [nLen] = (char) c;  break;
						case 2 : sscanf(p, "%hx", (unsigned short *) (buffer+nLen)); break;
						/* 原版 %lx 会写 8 字节（LP64），改为先读 32 位再拷贝 */
						case 4 : sscanf(p, "%x",  & u32);  memcpy (buffer+nLen, & u32, 4);  break;
						case 8 : sscanf(p, "%llx", & u64); memcpy (buffer+nLen, & u64, 8);  break;
					}
				   nLen += cvt [min (q-p, 8)] ;
				   while ( *q!=0 && (*q==' ' || *q=='.' || *q==':') ) q++;	// skip spaces
				}
				return nLen;
	   } // switch opt_val[0]
   }
   // non trouve --> conserver la chaine
	lstrcpyn (sz, opt_val, len);
	TranslateExp (sz, buffer, ip, tMac, iLastArch);
	( (char *) buffer) [len-1] = 0;
	return lstrlen (buffer);
} // TranslateParam2Value




///////////////////////////////////////////
//  DHCP configuration management
///////////////////////////////////////////

// The data to translate the registry entries into the sParamDHCP struct
// DHCP parameters in configuration file or registry
static struct
{
   char *szEntry;
   void *pValue;
   int   nType;
   int   nBufSize;
}
tDHCPd32Entry[] =
{
    KEY_DHCP_POOL,        sParamDHCP.szAddr ,          REG_SZ,    sizeof sParamDHCP.szAddr,
    KEY_DHCP_POOLSIZE,  & sParamDHCP.nPoolSize,        REG_DWORD, sizeof sParamDHCP.nPoolSize,
    KEY_DHCP_BOOTFILE,    sParamDHCP.szBootFile,       REG_SZ,    sizeof sParamDHCP.szBootFile,
    KEY_DHCP_DNS,         sParamDHCP.szDns1 ,          REG_SZ,    sizeof sParamDHCP.szDns1,
    KEY_DHCP_DNS2,        sParamDHCP.szDns2 ,          REG_SZ,    sizeof sParamDHCP.szDns2,
    KEY_DHCP_WINS,        sParamDHCP.szWins ,          REG_SZ ,   sizeof sParamDHCP.szWins,
    KEY_DHCP_MASK,        sParamDHCP.szMask ,          REG_SZ ,   sizeof sParamDHCP.szMask,
    KEY_DHCP_DEFROUTER,   sParamDHCP.szGateway,        REG_SZ,    sizeof sParamDHCP.szGateway,
    KEY_DHCP_OPTION42,    sParamDHCP.szOpt42 ,         REG_SZ,    sizeof sParamDHCP.szOpt42,
    KEY_DHCP_OPTION120,   sParamDHCP.szOpt120 ,        REG_SZ,    sizeof sParamDHCP.szOpt120,
    KEY_DHCP_DOMAINNAME,  sParamDHCP.szDomainName,     REG_SZ,    sizeof sParamDHCP.szDomainName,
    KEY_DHCP_LEASE_TIME,& sParamDHCP.nLease,           REG_DWORD, sizeof sParamDHCP.nLease,

}; // tDHCPd32Entry



// Save configuration either in INI file (if it exists) or in registry
int DHCPSaveConfig ( const struct S_DHCP_Param  *pNewParamDHCP )
{
INT   Ark;
char szBuf[64];

     // allocate new array, but keep pointers
     if ( sParamDHCP.nPoolSize!=pNewParamDHCP->nPoolSize )
	 {
         tFirstIP = realloc (tFirstIP, sizeof (tFirstIP[0]) * pNewParamDHCP->nPoolSize);
         tMAC     = realloc (tMAC,     sizeof (tMAC[0]) * pNewParamDHCP->nPoolSize);
		      // do not complain if pool is empty
         if (pNewParamDHCP->nPoolSize!=0 && (tFirstIP==NULL || tMAC==NULL) )
		{
			SVC_ERROR ("Can not allocate memory");
			return FALSE;
		}
	 }

     nAllocatedIP = min (nAllocatedIP, pNewParamDHCP->nPoolSize);

     sParamDHCP = *pNewParamDHCP;

     for (Ark=0 ; Ark < SizeOfTab (tDHCPd32Entry) ; Ark++)
        AsyncSaveKey (  TFTPD32_DHCP_KEY,
                        tDHCPd32Entry [Ark].szEntry,
                        tDHCPd32Entry [Ark].pValue,
                        tDHCPd32Entry [Ark].nBufSize,
                        tDHCPd32Entry [Ark].nType,
                        szTftpd32IniFile );

 // custom items
    for (Ark=0 ; Ark < SizeOfTab (sParamDHCP.t) ; Ark++)
    {
       wsprintf (szBuf, "%s%d", KEY_DHCP_USER_OPTION_NB, Ark+1);
       AsyncSaveKey  (TFTPD32_DHCP_KEY, szBuf, & sParamDHCP.t[Ark].nAddOption,
                 sizeof sParamDHCP.t[Ark].nAddOption, REG_DWORD, szTftpd32IniFile);
       wsprintf (szBuf, "%s%d", KEY_DHCP_USER_OPTION_VALUE, Ark+1);
       AsyncSaveKey  (TFTPD32_DHCP_KEY,  szBuf, sParamDHCP.t[Ark].szAddOption,
                 sizeof sParamDHCP.t[Ark].szAddOption, REG_SZ, szTftpd32IniFile);
    }

return TRUE;
} // DHCPSaveConfig

// read configuration either from INI file (if it exists) or from the registry
int DHCPReadConfig (void)
{
int   Ark;
char szBuf[128];

   /* 服务可反复启停：先回收上一次的池与租约，避免重复装载泄漏 */
   if (tFirstIP != NULL || tMAC != NULL)
   {
       FreeLeases (TRUE);
       nAllocatedIP = 0;
   }

   memset (& sParamDHCP, 0, sizeof sParamDHCP);
   sParamDHCP.nLease = DHCP_DEFAULT_LEASE_TIME;

   for (Ark=0 ; Ark<SizeOfTab (tDHCPd32Entry) ; Ark++)
        ReadKey (  TFTPD32_DHCP_KEY,
                   tDHCPd32Entry [Ark].szEntry,
                   tDHCPd32Entry [Ark].pValue,
                   tDHCPd32Entry [Ark].nBufSize,
                   tDHCPd32Entry [Ark].nType,
                   szTftpd32IniFile );
    // custom items
   for (Ark=0 ; Ark < SizeOfTab (sParamDHCP.t) ; Ark++)
   {
      wsprintf (szBuf, "%s%d", KEY_DHCP_USER_OPTION_NB, Ark+1);
      ReadKey  (TFTPD32_DHCP_KEY, szBuf, & sParamDHCP.t[Ark].nAddOption,
                sizeof sParamDHCP.t[Ark].nAddOption, REG_DWORD, szTftpd32IniFile);

       wsprintf (szBuf, "%s%d", KEY_DHCP_USER_OPTION_VALUE, Ark+1);
       ReadKey  (TFTPD32_DHCP_KEY,  szBuf, sParamDHCP.t[Ark].szAddOption,
                 sizeof sParamDHCP.t[Ark].szAddOption, REG_SZ, szTftpd32IniFile);
   }

   if ( sParamDHCP.nPoolSize!=0 )
   {
	  tFirstIP = malloc (sParamDHCP.nPoolSize * sizeof *tFirstIP[0]) ;
	  tMAC = malloc (sParamDHCP.nPoolSize * sizeof *tMAC[0]) ;
	  if (tFirstIP == NULL  ||  tMAC == NULL )
	   {
			SVC_ERROR ("Can not allocate memory");
			return FALSE;
	   }
	   LoadLeases ();
   }

   if (sParamDHCP.nLease==0)
   {
      sParamDHCP.nLease=DHCP_DEFAULT_LEASE_TIME;
      LOG (12, "%d, Lease time not specified, set to 2 days", GetCurrentThreadId ());
   }
   // compatibility 3 -> 4
   if (sParamDHCP.szWins[0]==0  && sParamDHCP.szDns1[0]!=0)
   {
      lstrcpy (sParamDHCP.szWins, sParamDHCP.szDns1);
      LOG (0, "WINS server copied from DNS servers");
   }

return TRUE;
} // DHCPReadConfig


///////////////////////////////////////////
//  translation
///////////////////////////////////////////
// translate $IP$, $ARCH$ and $MAC$ keywords
char *TranslateExp (const char *exp, char *to, struct in_addr ip, const char *tMac, unsigned short my_iLastArch)
{
char *q;
size_t  Ark;
char sz [256];		// somewhat larger that DHCP_FILE_LEN (128 bytes)

	// truncate input
	Ark = strnlen_s ( to, DHCP_FILE_LEN -1 );  	to [Ark] = 0;

    if ((q = strstr(exp, "$ARCH$")) != NULL)
    {
            lstrcpyn(sz, exp, 1 + (int)(q - exp));  // get the beginning of the string
            // search if architecture is known
            for (Ark = 0; Ark < (size_t) SizeOfTab(sPXE_Architecture) && sPXE_Architecture[Ark].val != my_iLastArch; Ark++);
            if (Ark < (size_t) SizeOfTab(sPXE_Architecture) && sPXE_Architecture[Ark].val == my_iLastArch)
            {
                lstrcat(sz, sPXE_Architecture[Ark].name);
                LOG(1, "ARCH is %s, my_iLastArch = %d \n", sPXE_Architecture[Ark].name, my_iLastArch);
            }
            lstrcat(sz, q + sizeof "$ARCH$" - 1);   // skip $ARCH$, but get the rest of the string
            lstrcpyn(to, sz, DHCP_FILE_LEN - 1);
    }
    else if ( (q=strstr (exp, "$IP$")) != NULL )
    {
       lstrcpyn (sz, exp, 1 + (int) (q - exp) );
       lstrcat (sz, inet_ntoa (ip) );
       lstrcat (sz, q + sizeof "$IP$" - 1);
       lstrcpyn (to, sz, DHCP_FILE_LEN - 1);
    }
    else if ( (q=strstr (exp, "$MAC$")) != NULL )
    {
       lstrcpyn (sz, exp, 1 + (int) (q - exp) );
       lstrcat (sz, haddrtoa ((const unsigned char *) tMac, 6, '.') );
       lstrcat (sz, q + sizeof "$MAC$" - 1);
       lstrcpyn (to, sz, DHCP_FILE_LEN - 1);
    }
    else if ( (q=strstr (exp, "$BootFileName$")) != NULL )
    {
	   lstrcpyn (sz, exp, 1 + (int) (q - exp) );
	   lstrcat (sz, to);
       lstrcat (sz, q + sizeof "$BootFileName$" - 1);
	   // replace to now
       lstrcpyn (to, sz, DHCP_FILE_LEN - 1);
    }
    else lstrcpyn (to, exp, DHCP_FILE_LEN - 1);

    // truncate
    to [DHCP_FILE_LEN-1]=0;
return to;
} // TranslateExp



///////////////////////////////////////////
//  DHCP database management
///////////////////////////////////////////
// Returns true if the macaddr is empty
int IsMacEmpty(struct LL_IP* pcur)
{
	return (pcur->sMacAddr[0] == 0) && (pcur->sMacAddr[1] == 0) && (pcur->sMacAddr[2] == 0) &&
		   (pcur->sMacAddr[3] == 0) && (pcur->sMacAddr[4] == 0) && (pcur->sMacAddr[5] == 0);
}

// Comparison between two struct. Serves to sort Array
int QsortCompare (const void *p1, const void *p2)
{
	DWORD addr1 = ntohl((*(const struct LL_IP**)p1)->dwIP.s_addr);
	DWORD addr2 = ntohl((*(const struct LL_IP**)p2)->dwIP.s_addr);

	if(addr1 < addr2)
		return -1;
	if(addr1 == addr2)
		return 0;
	return 1;
} // QsortCompare

int MACCompare (const void* p1, const void* p2)
{
const struct LL_IP  *ip1 = *(const struct LL_IP **) p1;
const struct LL_IP  *ip2 = *(const struct LL_IP **) p2;

	return memcmp(ip1->sMacAddr, ip2->sMacAddr, 6);
}

//Increment the number of allocated entries
void IncNumAllocated(void)
{
   ++nAllocatedIP;
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, KEY_LEASE_NUMLEASES, &nAllocatedIP, sizeof(nAllocatedIP), REG_DWORD, szTftpd32IniFile);
}

//Decrement the number of allocated entries
void DecNumAllocated(void)
{
   --nAllocatedIP;
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, KEY_LEASE_NUMLEASES, &nAllocatedIP, sizeof(nAllocatedIP), REG_DWORD, szTftpd32IniFile);
}

//Set the number of allocated entries explicitly
void SetNumAllocated(int n)
{
   nAllocatedIP = n;
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, KEY_LEASE_NUMLEASES, &nAllocatedIP, sizeof(nAllocatedIP), REG_DWORD, szTftpd32IniFile);
}

//Set the IP address to a new one（原版不落盘，见 Colin 修正，保留其语义）
void SetIP(struct LL_IP* pCur, DWORD newip)
{
   pCur->dwIP.s_addr = newip;
}

//Set the MAC address to a new one
void SetMacAddr(struct LL_IP* pCur, const unsigned char *pMac, int nMacLen)
{
   char key [_MAX_PATH];
   char* macaddr;
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_MAC);

   memset(pCur->sMacAddr, 0, sizeof pCur->sMacAddr);
   memcpy(pCur->sMacAddr, pMac, min (sizeof pCur->sMacAddr, (size_t) nMacLen));
   macaddr = haddrtoa((const unsigned char *) pCur->sMacAddr, 6, ':');
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, macaddr, (int) strlen(macaddr), REG_SZ, szTftpd32IniFile);
}

//Set the MAC address to all zeros
void ZeroMacAddr(struct LL_IP* pCur)
{
   char key [_MAX_PATH];
   char* macaddr;
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_MAC);

   memset(pCur->sMacAddr, 0, sizeof pCur->sMacAddr);
   macaddr = haddrtoa((const unsigned char *) pCur->sMacAddr, 6, ':');
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, macaddr, (int) strlen(macaddr), REG_SZ, szTftpd32IniFile);
}

//Set tAllocated to the current time
void SetAllocTime(struct LL_IP* pCur)
{
   char* t;
   char key [_MAX_PATH];
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_ALLOC);

   time(&pCur->tAllocated);
   t = timetoa(pCur->tAllocated);
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, t, (int) strlen(t)+1, REG_SZ, szTftpd32IniFile);
}

//Zero tAllocated
void ZeroAllocTime(struct LL_IP* pCur)
{
   char* t;
   char key [_MAX_PATH];
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_ALLOC);

   pCur->tAllocated = 0;
   t = timetoa(pCur->tAllocated);
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, t, (int) strlen(t) + 1, REG_SZ, szTftpd32IniFile);
}

//Set tRenewed to the current time
void SetRenewTime(struct LL_IP* pCur)
{
   char* t;
   char key [_MAX_PATH];
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_RENEW);

   time(&pCur->tRenewed);
   t = timetoa(pCur->tRenewed);
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, t, (int) strlen(t) + 1, REG_SZ, szTftpd32IniFile);
}

//Force tRenewed to a specific time
void ForceRenewTime(struct LL_IP* pCur, time_t newtime)
{
   char* t;
   char key [_MAX_PATH];
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_RENEW);

   pCur->tRenewed = newtime;
   t = timetoa(pCur->tRenewed);
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, t, (int) strlen(t) + 1, REG_SZ, szTftpd32IniFile);
}

//Zero tRenewed
void ZeroRenewTime(struct LL_IP* pCur)
{
   char* t;
   char key [_MAX_PATH];
   sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, pCur->dwAllocNum, KEY_LEASE_RENEW);

   pCur->tRenewed = 0;
   t = timetoa(pCur->tRenewed);
   if (sSettings.bPersLeases)
      AsyncSaveKey(TFTPD32_DHCP_KEY, key, t, (int) strlen(t) + 1, REG_SZ, szTftpd32IniFile);
}

//Completely renumbers and rewrites the lease list from current memory.
void ReorderLeases(void)
{
   int i;
   AsyncSaveKey (TFTPD32_DHCP_KEY,
				 KEY_LEASE_NUMLEASES,
				 & nAllocatedIP,
				 sizeof(nAllocatedIP),
				 REG_DWORD,
				 szTftpd32IniFile);

   for(i = 0; i < nAllocatedIP; ++i)
   {
      char key [_MAX_PATH];
      char* macaddr = haddrtoa((const unsigned char *) tFirstIP[i]->sMacAddr, 6, ':');
      char* addr = inet_ntoa(tFirstIP[i]->dwIP);
      /* timetoa 使用静态缓冲：每次调用前立即拷出，避免 alloc/renew 互相覆盖 */
      char szAlloc [32], szRenew [32];
      lstrcpy (szAlloc, timetoa (tFirstIP[i]->tAllocated));
      lstrcpy (szRenew, timetoa (tFirstIP[i]->tRenewed));
      /* inet_ntoa 也是静态缓冲，但 macaddr 用的是 haddrtoa（另一块静态区），互不干扰 */
      (void) addr;

      tFirstIP[i]->dwAllocNum = i;
      if (sSettings.bPersLeases)
	  {
		sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, i, KEY_LEASE_MAC);
		SaveKey(TFTPD32_DHCP_KEY, key, macaddr, (int) strlen(macaddr) + 1, REG_SZ, szTftpd32IniFile);
		  sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, i, KEY_LEASE_IP);
		  SaveKey(TFTPD32_DHCP_KEY, key, addr, (int) strlen(addr) + 1, REG_SZ, szTftpd32IniFile);
		  sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, i, KEY_LEASE_ALLOC);
		  SaveKey(TFTPD32_DHCP_KEY, key, szAlloc, (int) strlen(szAlloc) + 1, REG_SZ, szTftpd32IniFile);
		  sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, i, KEY_LEASE_RENEW);
         AsyncSaveKey(TFTPD32_DHCP_KEY, key, szRenew, (int) strlen(szRenew) + 1, REG_SZ, szTftpd32IniFile);
	  }
   }
}

//Read in and initialize the leases
void LoadLeases(void)
{
   //We need to make sure the leases we load actually fit in the address pool, so we'll be
   //tracking the index to the lease file and the index to the allocated list
   int leaseindex, allocindex;

   // From Nick : I realized that there was a race condition in that code,
   // particularly with the reading and saving of KEY_LEASE_NUMLEASES
   WaitForMsgQueueToFinish (LL_ID_SETTINGS);

   nAllocatedIP = 0;
   ReadKey(TFTPD32_DHCP_KEY, KEY_LEASE_NUMLEASES, &nAllocatedIP, sizeof(nAllocatedIP), REG_DWORD, szTftpd32IniFile);

   if (nAllocatedIP > sParamDHCP.nPoolSize)
   {
      SVC_WARNING ("The pool size is too small for the number of leases, ignoring extra leases");
      nAllocatedIP = sParamDHCP.nPoolSize;
   }

   allocindex = 0;
   for(leaseindex = 0; leaseindex < nAllocatedIP; ++leaseindex)
   {
     char key [_MAX_PATH];
     char tmpval [_MAX_PATH];

     tFirstIP[allocindex] = malloc (sizeof(struct LL_IP));
     memset(tFirstIP[allocindex], 0, sizeof(struct LL_IP));

     tFirstIP[allocindex]->dwAllocNum = leaseindex;
     sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, leaseindex, KEY_LEASE_MAC);
     if(ReadKey(TFTPD32_DHCP_KEY, key, tmpval, _MAX_PATH, REG_SZ, szTftpd32IniFile))
        atohaddr((const unsigned char *) tmpval, (unsigned char *) tFirstIP[allocindex]->sMacAddr, 6);
     sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, leaseindex, KEY_LEASE_IP);
     if(ReadKey(TFTPD32_DHCP_KEY, key, tmpval, _MAX_PATH, REG_SZ, szTftpd32IniFile))
        tFirstIP[allocindex]->dwIP.s_addr = inet_addr(tmpval);
     sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, leaseindex, KEY_LEASE_ALLOC);
     if(ReadKey(TFTPD32_DHCP_KEY, key, tmpval, _MAX_PATH, REG_SZ, szTftpd32IniFile))
        tFirstIP[allocindex]->tAllocated = atotime(tmpval);
     sprintf(key, "%s%d%s", KEY_LEASE_PREFIX, leaseindex, KEY_LEASE_RENEW);
     if(ReadKey(TFTPD32_DHCP_KEY, key, tmpval, _MAX_PATH, REG_SZ, szTftpd32IniFile))
        tFirstIP[allocindex]->tRenewed = atotime(tmpval);

    // fix errors in date conversion (registry modified at hand)
    if (tFirstIP[allocindex]->tAllocated == -1) tFirstIP[allocindex]->tAllocated = 0;
    if (tFirstIP[allocindex]->tRenewed   == -1) tFirstIP[allocindex]->tRenewed   = 0;

     //If the address doesn't fit in the pool, don't add it after all
	 //Since we are assuming the leases were written in order, do a quick check for dups
	 //and invalid macaddrs
     if((!AddrFitsPool(&tFirstIP[allocindex]->dwIP)) || (IsMacEmpty(tFirstIP[allocindex])) ||
		((allocindex > 0) && (tFirstIP[allocindex]->dwIP.s_addr == tFirstIP[allocindex - 1]->dwIP.s_addr)))
     {
        free(tFirstIP[allocindex]);
        tFirstIP[allocindex] = NULL;
     }
     else
	 {
		tMAC[allocindex] = tFirstIP[allocindex];  //Copy to cross index
        ++allocindex;   //Move on to the next one
	 }
   }

   if(allocindex != nAllocatedIP)
      SetNumAllocated(allocindex);

    // ensure that data base is sorted (especially if we've dropped some leases in the load)
    qsort (tMAC, nAllocatedIP, sizeof *tMAC, MACCompare);
    qsort (tFirstIP, nAllocatedIP, sizeof *tFirstIP, QsortCompare);
    ReorderLeases();

} // LoadLeases


//Free the Lease memory
void FreeLeases(BOOL freepool)
{
	int Ark;

	if (tFirstIP == NULL)  return;

	//Free the individual elements
	for(Ark = 0; Ark < nAllocatedIP; ++Ark)
	{
		free(tFirstIP[Ark]);
		tFirstIP[Ark] = NULL;
		if (tMAC != NULL)  tMAC[Ark] = NULL;
	}
	if(freepool)
	{
		free(tFirstIP);
		tFirstIP = NULL;
		if(tMAC != NULL)
		{
			free(tMAC);
			tMAC = NULL;
		}
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////
// Data Base Queries
//////////////////////////////////////////////////////////////////////////////////////////////


// Search in the list by IP
struct LL_IP *DHCPSearchByIP (const struct in_addr *pAddr, BOOL* wasexpired)
{
	struct LL_IP** pArk;
	struct LL_IP key, *pkey;
	time_t expire;
	time(&expire);
	expire -= sParamDHCP.nLease * 60; //Adjust now so there's just a simple compare to tRenewed

	key.dwIP.s_addr = pAddr->s_addr;
	pkey = &key;
	pArk = bsearch(&pkey, tFirstIP, nAllocatedIP, sizeof(*tFirstIP), QsortCompare);
	if(pArk)
		*wasexpired = ((*pArk)->tRenewed == 0) || (expire > (*pArk)->tRenewed);
	if(pArk)
		return *pArk;
	return NULL;
} // DHCPSearchByIP


// Search in the list by Mac Address
struct LL_IP *DHCPSearchByMacAddress (const unsigned char *pMac, int nMacLen)
{
	struct LL_IP** pArk;
	struct LL_IP key, *pkey;

	nMacLen = min (nMacLen, 6);
	memset (& key, 0, sizeof key);
	memcpy(key.sMacAddr, pMac, nMacLen);
	pkey = &key;
	pArk = bsearch(&pkey, tMAC, nAllocatedIP, sizeof(*tMAC), MACCompare);
	if(pArk)
		return *pArk;
	return NULL;
} // DHCPSearchByMacAddress
