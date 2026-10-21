//////////////////////////////////////////////////////
//
// Projet TFTPD32.  January 2006 Ph.jounin
// Projet DHCPD32.  January 2006 Ph.jounin
// File bootp.c:    Manage BOOTP/DHCP protocols
//
// source released under European Union Public License
//
// 鸿蒙适配说明（与 reference/tftpd64/src/_services/bootpd.c 的差异，协议逻辑不变）：
//  1) Winsock → POSIX：WSARecvMsg 改 recvmsg + IP_PKTINFO，
//     GetAdaptersAddresses 改 getifaddrs（ip_pal.c）
//  2) 无 raw socket 权限：PingRange/PingRecv/ArpAndAdd 不移植；
//     DHCP Ping / ARP 冲突检测降级（见 DHCP_IPAllocate），保留静态租约与双应答
//  3) SendARP（DECLINE 时登记冲突地址）不可用 → 仅销毁租约
//  4) 服务端口可取配置（默认 6700，原版 67 为特权端口）；
//     回包广播复用监听 socket（原版另开 socket 绑定 67，鸿蒙不可行，报文内容一致）
//  5) 回环地址按单播处理，便于设备内自测（原版把 127.x 当广播）
//  6) 租约发送 GUI 改为 UI 轮询 PalDhcpLeasesJson（原版 Dhcp_Send_Leases → 消息队列）
//////////////////////////////////////////////////////

#include "headers.h"
#include "dhcp.h"

#include <stdio.h>          // sscanf is used
#include <stddef.h>         // offsetof

#include "threading.h"
#include "bootpd_functions.h"

#define CLASS_A_LOOPBACK  127
#define DHO_CUSTOM 254


// check magic cookie
#define IsDHCP(x)    ( * (DWORD *) ((x).options) == * (DWORD *) DHCP_OPTIONS_COOKIE )


#define BOOTPC_PORT  68
#define BOOTPS_PORT  67

//
// A table to display the message received
// DHCP message
struct S_DHCP_type
{
    int nType;
    char *sType;
};
static struct S_DHCP_type tDHCPType[] =
     {
        {0,             "BootP",           },
        {DHCPDISCOVER,  "DHCP Discover",   },
        {DHCPOFFER,     "DHCP Offer",      },
        {DHCPREQUEST,   "DHCP Rqst",       },
        {DHCPDECLINE,   "DHCP decline",    },
        {DHCPACK,       "DHCP Ack",        },
        {DHCPNAK,       "DHCP Nak",        },
        {DHCPRELEASE,   "DHCP release",    },
        {DHCPINFORM,    "DHCP inform"      },
        {DHCPINFORM+1,  "Invalid Pkt"     },
    };


// DHCP options
struct S_DhcpOptions
{
   unsigned nDHCPOpt;
   char     nLen;
   int      nServices;
} ; // struct S_DhcpOptions


static BOOL scanforleases = FALSE;
void setleaseprompt(BOOL doprompt) { 	scanforleases = doprompt; }

/* 租约表由 DHCP 线程写、UI 线程读 → 单锁保护（UI 轮询周期 500ms，冲突可忽略） */
static pthread_mutex_t s_leaseMx = PTHREAD_MUTEX_INITIALIZER;


///////////////////////////////////////////////////////
// Address assignation
///////////////////////////////////////////////////////

// DHCP Management
// Search an option in the DHCP extension
char *DHCPSearchOptionsField (unsigned char *pOpt, int nField, int *pLength)
{
int            Ark;
unsigned char *p;
 if (* (DWORD *) pOpt != * (DWORD *) DHCP_OPTIONS_COOKIE )   return NULL;

    for ( Ark = 0,  p =  pOpt + (sizeof DHCP_OPTIONS_COOKIE - 1)  ;
          Ark<DHCP_OPTION_LEN-3  && p[Ark]!=nField ;
          Ark += (p[Ark]==DHO_PAD ? 1 : 2+p[Ark+1]) );

    if (Ark<DHCP_OPTION_LEN-3  &&  Ark+p[Ark+1] < DHCP_OPTION_LEN && p[Ark] == nField)
    {
        if (pLength!=NULL)  *pLength = p[Ark+1];
        return &p[Ark+2];
    }
return NULL;
} // DHCPSearchField


//////////////////////////////////////////////////////////////////////////////////////////////
// Struct Management
//////////////////////////////////////////////////////////////////////////////////////////////

// DHCP Scan
// for debugging purposes only
void DHCPScan (void)
{
int  Ark;
time_t tNow;
 time (&tNow);
      for (Ark = 0 ;   Ark<nAllocatedIP  ;  Ark++ )
       LOG (15, "Item %d: IP %s, Mac %s, Age %d sec, %s",
               Ark,
               inet_ntoa (tFirstIP[Ark]->dwIP),
               haddrtoa((const unsigned char *) tFirstIP[Ark]->sMacAddr, 6,':'),
               (int) (tNow - tFirstIP[Ark]->tAllocated),
               tFirstIP[Ark]->tRenewed ? "Ack" : "Nak" );
} // DHCPScan


// Create or realloc an item
struct LL_IP *DHCPReallocItem (struct LL_IP *pCur, DWORD dwNewIP, const unsigned char *pMac, int nMacLen)
{
   if (pCur==NULL)
   {
      pCur = tFirstIP[nAllocatedIP] = malloc (sizeof *tFirstIP[0]);
      memset (pCur, 0, sizeof *pCur);
      pCur->dwAllocNum = nAllocatedIP;
	  tMAC[nAllocatedIP] = pCur;
      IncNumAllocated();
   }
   SetIP(pCur, dwNewIP);
   SetAllocTime(pCur);
   ZeroRenewTime(pCur);
   SetMacAddr(pCur, pMac, nMacLen);
   // sort whole array
   qsort (tFirstIP, nAllocatedIP, sizeof *tFirstIP, QsortCompare);
   qsort (tMAC, nAllocatedIP, sizeof *tMAC, MACCompare);
   ReorderLeases();
return pCur;
} // DHCPReallocItem

// frees an item (may crash if allocation in progress)
void DHCPDestroyItem (struct LL_IP *pCur)
{
	if (pCur!=NULL)
	{
		LOG (5, "Freeing item %s %s",
							 inet_ntoa (pCur->dwIP),
							 haddrtoa  ((const unsigned char *) pCur->sMacAddr, 6,':')) ;
		// put item at the end of the list and resort array
		SetIP(pCur, INADDR_NONE);   // will be the last item
		memset(pCur->sMacAddr, 0, 6);
		qsort (tFirstIP, nAllocatedIP, sizeof *tFirstIP, QsortCompare);
		qsort (tMAC, nAllocatedIP, sizeof *tMAC, MACCompare);
		DecNumAllocated();
		ReorderLeases();
		free ( tFirstIP[nAllocatedIP] ); // free the last item
	}
	// GUI 由 UI 轮询刷新，无需 WakeUpThread（原版唤醒 DHCP 线程发送租约）
} // DHCPDestroyItem



//////////////////////////////////////////////////////////////////////////////////////////////
// Data Base Queries
//////////////////////////////////////////////////////////////////////////////////////////////

// Assignation by MAC Address
DWORD DHCP_StaticAssignation (struct dhcp_packet *pPkt)
{
char          szIP[20];

return ( pPkt->htype==HTYPE_ETHER || pPkt->htype==HTYPE_IEEE802 )
        && pPkt->hlen==6
        && ReadKey ( TFTPD32_DHCP_KEY,
                     haddrtoa(pPkt->chaddr, pPkt->hlen,':'),
                     szIP, sizeof szIP,
                     REG_SZ,
                     szTftpd32IniFile )
        ?  inet_addr (szIP) : INADDR_NONE;

} // DHCP_StaticAssignation


//////////////////////////////////////////////////////////////////////////////////////////////
// IP assignation
//////////////////////////////////////////////////////////////////////////////////////////////


struct LL_IP *DHCP_IPAllocate2(struct in_addr *pPreviousAddr, const unsigned char *pMac, int nMacLen)
{
time_t  tNow;
int     Ark;
struct LL_IP *pCurIP;
#define TWO_MINUTES 120

//If true, the client has requested an IP address that we should try to honor
int useprev = (pPreviousAddr->s_addr != INADDR_ANY) && (AddrFitsPool(pPreviousAddr));


   // search for the previously allocated mac address
   if (nMacLen>=6) // Ethernet Mac address
   {
	   // search if mac the mac address is already known
       pCurIP = DHCPSearchByMacAddress (pMac, nMacLen);
	   if(pCurIP)
	   {
		   //We found the previous MAC record.  If the address requested is invalid, use
		   //the address in the record.  If it is valid, erase the record, since we should
		   //give the requested address
		   if(!useprev || (pPreviousAddr->s_addr == pCurIP->dwIP.s_addr))
		   {
				LOG (12, "Reply with previously allocated : %s", inet_ntoa (pCurIP->dwIP));
				SetAllocTime(pCurIP);
				return pCurIP;
		   }
		   else //Remove the old address, and continue with the allocation logic
		   {
			   //Destroying the item takes care of the macaddr table as well.
			   DHCPDestroyItem (pCurIP);
			   pCurIP = NULL;
		   }
       } // mac address found

    } // mac address not valid


    // search if requested address can be granted
	if(useprev)
    {
		// is the address already allocated but not renewed (or expired)
		BOOL wasexpired=FALSE;
		pCurIP = DHCPSearchByIP (pPreviousAddr, &wasexpired);
		if (pCurIP!=NULL)
		{
			//Only allocate if it's to the same address, or the lease expired,
			//otherwise reset for the real allocation
			if(wasexpired || (0 == memcmp(pCurIP->sMacAddr, pMac, nMacLen)))
			{
				LOG (5, "Request for %s granted", inet_ntoa (pCurIP->dwIP));
				pCurIP = DHCPReallocItem (pCurIP, pPreviousAddr->s_addr, pMac, nMacLen);
				return pCurIP ;
			}
			else
				pCurIP = NULL;
		}
		else	//Address not allocated before, just grant it
		{
			pCurIP = DHCPReallocItem (NULL, pPreviousAddr->s_addr, pMac, nMacLen);
			LOG (12, "Reply with requested address : %s", inet_ntoa (pCurIP->dwIP));
			return pCurIP;
		} //
   } // Requested address asked

  // A new IP address should be allocated :
  // First check if the pool is large enough in order to allocate a new address
   if (sParamDHCP.nPoolSize>0   &&  nAllocatedIP < sParamDHCP.nPoolSize)
   {
    // search for an "hole" in the struct or take last elem + 1
    // if an item was allocated and the first item is the first in pool
   //Don't allocate ip addresses ending in 0 or 255
    if (nAllocatedIP>0   &&  tFirstIP[0]->dwIP.s_addr == inet_addr (sParamDHCP.szAddr))
     {
          for ( Ark=1 ;
                Ark<nAllocatedIP
               &&  ntohl (tFirstIP[Ark]->dwIP.s_addr) == AddrInc(tFirstIP[Ark-1]->dwIP);
               Ark ++ );
           pCurIP = DHCPReallocItem (NULL, htonl (AddrInc(tFirstIP[Ark-1]->dwIP)), pMac, nMacLen);
       }
      else   pCurIP = DHCPReallocItem (NULL, inet_addr (sParamDHCP.szAddr), pMac, nMacLen);
       // New address : ntohl (tFirstIP[Ark]->dwIP.s_addr) + 1
    // it is OK if Ark has reach nAllocatedIP
      LOG (12, "Reply with new : %s", inet_ntoa (pCurIP->dwIP));
    return pCurIP;
    } // new allocation

    // no free address, have to reuse an "old" one
    // try addresses which have not been acknowledged (tAllocated+2 minutes),
    // or that have expired
    //
   time (&tNow);
   for (Ark=0 ;   Ark<nAllocatedIP;   Ark++)
    {
        pCurIP = tFirstIP[Ark];
        if (    pCurIP->tAllocated+TWO_MINUTES < tNow
            &&  ((pCurIP->tRenewed==0) || (tNow > pCurIP->tRenewed + (sParamDHCP.nLease * 60))))
        {
               pCurIP = DHCPReallocItem (pCurIP, pCurIP->dwIP.s_addr, pMac, nMacLen);
             LOG (12, "Reply with reuse : %s", inet_ntoa (pCurIP->dwIP));
                return pCurIP;
        }

    } // reuse an unacknowledged address

return NULL;
} // DHCP_IPAllocate2


/* 原版在 DISCOVER 时用 SendARP / PingApi 检测地址冲突。
   鸿蒙应用无 raw socket/NET_ADMIN，ARP 与 ICMP 均不可用 → 直接分配。
   （sSettings.bPing 仍保留为配置项，打开时记录一次降级日志） */
struct LL_IP *DHCP_IPAllocate(int nDhcpType, struct in_addr *pPreviousAddr, const unsigned char *pMac, int nMacLen)
{
    static BOOL bPingWarned = FALSE;
    if (sSettings.bPing && nDhcpType==DHCPDISCOVER && ! bPingWarned)
    {
        bPingWarned = TRUE;
        SVC_WARNING ("DHCP Ping/ARP 冲突检测在鸿蒙上不可用（无 raw socket），已降级为直接分配");
    }
    return DHCP_IPAllocate2 (pPreviousAddr, pMac, nMacLen);
}

//////////////////////////////////////////////////////////////////////////////////////////////
// fill DHCP fields
//////////////////////////////////////////////////////////////////////////////////////////////
int DHCPOptionsReply(struct dhcp_packet* pDhcpPkt, int nDhcpType, struct sockaddr_in* receivingAddress, unsigned short iLastArch)
{
unsigned char  *pOpt = (unsigned char *) (pDhcpPkt->options + (sizeof DHCP_OPTIONS_COOKIE - 1));
struct in_addr *pNearest;
struct in_addr  in_Aux;
int             Ark, Evan;
char            sz[256];
unsigned char  *p;
unsigned char  szDhcpAgentOpt[1024];
int				nDhcpAgentOpt=0;
unsigned char  *pSub82;
unsigned char szDhcpAgentOptSub1[128],szDhcpAgentOptSub2[128];
int           msgSize;

static struct S_DhcpOptions sDhcpOpt [] =       // 0 for unspecified
{
    DHO_DHCP_MESSAGE_TYPE,      1,	  ~TFTPD32_NONE,
    DHO_DHCP_SERVER_IDENTIFIER, 4,	  ~TFTPD32_NONE,
    DHO_SUBNET_MASK,            4,	  ~TFTPD32_NONE,
	// change : gateway not mandatory anymore
    DHO_ROUTERS,                0,	  ~TFTPD32_NONE,
    DHO_DOMAIN_NAME_SERVERS,    0,	  ~TFTPD32_NONE,
    DHO_NETBIOS_NAME_SERVERS,   0,	  ~TFTPD32_NONE,
    DHO_DHCP_LEASE_TIME,        4,	  ~TFTPD32_NONE,
    DHO_DHCP_RENEWAL_TIME,      4,	  ~TFTPD32_NONE,
    DHO_DHCP_REBINDING_TIME,    4,	  ~TFTPD32_NONE,
    DHO_LOG_SERVERS,            4,	   TFTPD32_SYSLOG_SERVER,
    DHO_NTP_SERVERS,			0,	  ~TFTPD32_NONE,
    DHO_SIP_SERVERS,			0,	  ~TFTPD32_NONE,
    DHO_BOOT_SIZE,              0,	   TFTPD32_TFTP_SERVER,
    DHO_TFTP_SERVER,			0,     TFTPD32_TFTP_SERVER,
    DHO_DOMAIN_NAME,            0,	  ~TFTPD32_NONE,
    DHO_DHCP_AGENT_OPTIONS,     0,	  ~TFTPD32_NONE,
    DHO_CUSTOM,                 0,	  ~TFTPD32_NONE,
    DHO_END,                    0,	  ~TFTPD32_NONE,
};



	// Save Agent Options
       p = DHCPSearchOptionsField (pDhcpPkt->options, DHO_DHCP_AGENT_OPTIONS, NULL);
       if (p!=NULL)
	   {
		   p--;
		   nDhcpAgentOpt = *p;
           memcpy (szDhcpAgentOpt, p+1,  nDhcpAgentOpt);
	   }

	//Always pack the magic cookie again, just in case it was corrupted
	*(DWORD*)(pDhcpPkt->options) = * (DWORD*) DHCP_OPTIONS_COOKIE;

	// pNearest points on the "good" LAN interface
   in_Aux.s_addr=inet_addr(sParamDHCP.szMask);
   pNearest = FindNearestServerAddress (&pDhcpPkt->yiaddr, & in_Aux, TRUE);
   // from Philip Taff : choose the receiving interface instead of a loopback
   if (!pNearest)
       pNearest = &(receivingAddress->sin_addr);
   //HACK -- If we are the bootp server, we are also the tftpserver
   for (Ark = 0; Ark < 10; Ark++) {
	   if (sParamDHCP.t[Ark].nAddOption == 66) {
		   pDhcpPkt->siaddr.s_addr = inet_addr(sParamDHCP.t[Ark].szAddOption);
	   }
   }
   if (pDhcpPkt->siaddr.s_addr == INADDR_NONE) {
	   pDhcpPkt->siaddr = *pNearest;   // always fill siaddr
   }

   for (Ark=0 ; Ark<SizeOfTab(sDhcpOpt) ; Ark++)
   {
	 // skip if linked to a service which is not started (change suggested by Colin)
	 if (! (sSettings.uServices & sDhcpOpt[Ark].nServices))
			continue;

     if (sDhcpOpt[Ark].nLen!=0)
     {
        *pOpt++ = (unsigned char) sDhcpOpt[Ark].nDHCPOpt ;
        *pOpt++ = (unsigned char) sDhcpOpt[Ark].nLen;
     }
     switch (sDhcpOpt[Ark].nDHCPOpt)
     {
	case DHO_DHCP_MESSAGE_TYPE       :  * pOpt = (unsigned char) nDhcpType ; break ;
	case DHO_LOG_SERVERS             :  // fallthrough
	case DHO_DHCP_SERVER_IDENTIFIER  :  * (DWORD *) pOpt = pNearest->s_addr; break ;
	case DHO_TFTP_SERVER             :
                  *pOpt++ = DHO_TFTP_SERVER;
				  *pOpt   = lstrlen (sSettings.szTftpLocalIP[0]!=0 ? sSettings.szTftpLocalIP : inet_ntoa (* pNearest) );
                   memcpy (pOpt+1, sSettings.szTftpLocalIP[0]!=0 ? sSettings.szTftpLocalIP : inet_ntoa (* pNearest), *pOpt);
                   pOpt += 1+*pOpt;
				   break;

	case DHO_SUBNET_MASK             :  * (DWORD *) pOpt = inet_addr(sParamDHCP.szMask); break ;
	case DHO_ROUTERS                 :
			// if gateway is not valid, forget this option
		   if ( inet_addr(sParamDHCP.szGateway)!=INADDR_NONE )
		   {
			   *pOpt++ = (unsigned char) DHO_ROUTERS ;
               *pOpt++ = 4;
  			   * (DWORD *) pOpt = inet_addr(sParamDHCP.szGateway);
				pOpt += 4;
		   }
		   break ;
	 case DHO_DOMAIN_NAME_SERVERS:

			if ( (strlen(sParamDHCP.szDns1)!=0  &&  inet_addr (sParamDHCP.szDns1)!=0x0) )
			{
				BOOL bkp_dns = (strlen(sParamDHCP.szDns2)!=0  &&  inet_addr (sParamDHCP.szDns2)!=0x0);
                  *pOpt++ = DHO_DOMAIN_NAME_SERVERS;
                  *pOpt++ = bkp_dns ? 8 : 4;
				  * (DWORD *) pOpt = inet_addr (sParamDHCP.szDns1);
				  if (bkp_dns) * (DWORD *) (pOpt+4) = inet_addr (sParamDHCP.szDns2);
				   pOpt += bkp_dns ? 8 : 4;
            }
            break;
     case DHO_NETBIOS_NAME_SERVERS    :
			if (!(strlen(sParamDHCP.szWins)==0 || inet_addr (sParamDHCP.szWins)==0x0))
            {
                  *pOpt++ = DHO_NETBIOS_NAME_SERVERS;
                  *pOpt++ = 4;
				  * (DWORD *) pOpt = inet_addr (sParamDHCP.szWins);
				   pOpt += 4;
            }
            break;
     case DHO_DHCP_LEASE_TIME         :  * (DWORD *) pOpt = htonl (sParamDHCP.nLease * 60);  break ;
     case DHO_DHCP_RENEWAL_TIME       :  * (DWORD *) pOpt = htonl (sParamDHCP.nLease/2 * 60);  break ;
     case DHO_DHCP_REBINDING_TIME     :  * (DWORD *) pOpt = htonl ((sParamDHCP.nLease*80)/100 * 60);  break ;
     case DHO_BOOT_SIZE               :
              /* translate $IP$ and $MAC$ from boot file name */
              TranslateExp (sParamDHCP.szBootFile, sz, pDhcpPkt->yiaddr, pDhcpPkt->chaddr, iLastArch);
              {
                  /* 原版 CreateFile/GetFileSize：改为 stat，相对路径按 WorkingDirectory 解析 */
                  char szFull [1024];
                  struct stat st;
                  if (sz [0] != '/')
                      snprintf (szFull, sizeof szFull, "%s/%s", sSettings.szWorkingDirectory, sz);
                  else
                      lstrcpyn (szFull, sz, sizeof szFull);
                  if (stat (szFull, & st) == 0 && S_ISREG (st.st_mode))
                  {
                      *pOpt++ = DHO_BOOT_SIZE ;
                      *pOpt++ = sizeof (unsigned short);
                      * (unsigned short *) pOpt = htons ( (unsigned short) (1 + st.st_size / 512) ) ;
                      pOpt += sizeof (unsigned short);
                  }
              }
              break;
     case DHO_NTP_SERVERS             :
		if (!(strlen(sParamDHCP.szOpt42)==0 || inet_addr (sParamDHCP.szOpt42)==0x0))
             {
                  *pOpt++ = DHO_NTP_SERVERS;
                  *pOpt++ = 4;
				  * (DWORD *) pOpt = inet_addr (sParamDHCP.szOpt42);
				   pOpt += 4;
             }
             break;
     case DHO_SIP_SERVERS             :
		if (!(strlen(sParamDHCP.szOpt120)==0 || inet_addr (sParamDHCP.szOpt120)==0x0))
             {
                  *pOpt++ = DHO_SIP_SERVERS;
                  *pOpt++ = 5;
				  *pOpt++ = 1;
				  * (DWORD *) pOpt = inet_addr (sParamDHCP.szOpt120);
				   pOpt += 4;
             }
             break;
     case DHO_DOMAIN_NAME             :
             if (sParamDHCP.szDomainName[0]!=0)
             {
                  *pOpt++ = DHO_DOMAIN_NAME;
                  *pOpt   = lstrlen (sParamDHCP.szDomainName);
                   memcpy (pOpt+1, sParamDHCP.szDomainName, *pOpt);
                   pOpt += 1+*pOpt;
              }
              break;
     case DHO_DHCP_AGENT_OPTIONS       :
             if (nDhcpAgentOpt!=0)
             {
                 *pOpt++ = DHO_DHCP_AGENT_OPTIONS;
                 *pOpt++ = nDhcpAgentOpt;
                  memcpy (pOpt, szDhcpAgentOpt, nDhcpAgentOpt);
                  pOpt += nDhcpAgentOpt;
				  pSub82=szDhcpAgentOpt;
				  szDhcpAgentOptSub1[0]=szDhcpAgentOptSub2[0]=0;
				  while (pSub82!=NULL && pSub82 < szDhcpAgentOpt+nDhcpAgentOpt-2)
				  {
					 if (pSub82[0]==0x01 && pSub82[1] < 127)
					 {
						memcpy (szDhcpAgentOptSub1, pSub82+2, pSub82[1]);
						szDhcpAgentOptSub1[pSub82[1]]=0;
						pSub82+=pSub82[1]+2;
					 }
					 else if (pSub82[0]==0x02 && pSub82[1] < 127)
					 {
						memcpy (szDhcpAgentOptSub2, pSub82+2, pSub82[1]);
						szDhcpAgentOptSub2[pSub82[1]]=0;
						pSub82+=pSub82[1]+2;
					 }
					 else pSub82=NULL;
				  }
				  LOG (5, "Adding option 82, length: %d ; CirID: <%s>; RemID: <%s>; ",
					  nDhcpAgentOpt, szDhcpAgentOptSub1,szDhcpAgentOptSub2);
             }
             break;
	 case DHO_CUSTOM : // Manage custom options
         for (Evan=0 ; Evan < SizeOfTab (sParamDHCP.t) ; Evan++)
                if (sParamDHCP.t[Evan].nAddOption != 0)
                {
                   *pOpt++ = (unsigned char) sParamDHCP.t[Evan].nAddOption;
				   *pOpt = TranslateParam2Value ((char *) pOpt+1, 64, sParamDHCP.t[Evan].szAddOption,
								 pDhcpPkt->yiaddr, (const char *) pDhcpPkt->chaddr, iLastArch);
                   pOpt += 1+*pOpt;
               }
              break;
     case DHO_END                     :
                 *pOpt++ = DHO_END;
                 break;
     } // switch option
     pOpt += sDhcpOpt[Ark].nLen ;    // points on next field
   } // for all option

   // 2017-11-05 pad to 64 bytes
   msgSize = (int) (pOpt - (unsigned char*) pDhcpPkt);
   if  (msgSize < 64)
   {
	   memset (pOpt, 0, 64 - msgSize);
	   msgSize = 64;
   }
return msgSize;
} // DHCPOptionsReply


///////////
// Process DHCP msg : return TRUE if an answer has been prepared

int iCounterDHCP=0;
int ProcessDHCPMessage (struct dhcp_packet *pDhcpPkt, int *pSize,  struct sockaddr_in* receivingAddress)
{
unsigned char *p=NULL;
struct LL_IP  *pCurIP=NULL, *pProposedIP=NULL;	// Thanks Sam Leitch !
int            Ark, nDhcpType = 0;
struct in_addr in_RequestedAddr;
DWORD sStaticIP;
unsigned short iLastArch = 0;	// architecture required by client, defaut is x86

    if (IsDHCP (*pDhcpPkt))
    {
       // search DHCP message type
       p = DHCPSearchOptionsField (pDhcpPkt->options, DHO_DHCP_MESSAGE_TYPE, NULL);
       if (p!=NULL)        nDhcpType = *p;
     }
    if (pDhcpPkt->yiaddr.s_addr!=INADDR_ANY  &&  pDhcpPkt->yiaddr.s_addr!=INADDR_NONE )
            return FALSE ; // address already assigned

	// Get Architecture requested by client
	p = DHCPSearchOptionsField(pDhcpPkt->options, DHO_PXE_CLIENT_ARCH_ID, NULL);
	if (p != NULL)
	{
		iLastArch = ntohs(* (unsigned short *) p);
		LOG(3, "iLastArch %d", iLastArch);
	}

     // the tab has one undef raw
     for (Ark=0 ; Ark<SizeOfTab(tDHCPType)-1 && nDhcpType!=tDHCPType[Ark].nType ; Ark++) ;
     LOG (5, "Rcvd %s Msg for IP %s, Mac %s",
                      tDHCPType[Ark].sType,
                      inet_ntoa (pDhcpPkt->ciaddr),
                      haddrtoa(pDhcpPkt->chaddr, pDhcpPkt->hlen,':'));


     switch (nDhcpType)
     {
        case 0           :    // BootP
            if(sParamDHCP.nIgnoreBootp)
            {
               LOG (5, "Ignoring Bootp request");
               break;
            }
        case DHCPDISCOVER :
            sStaticIP = DHCP_StaticAssignation (pDhcpPkt);
			in_RequestedAddr.s_addr=pDhcpPkt->ciaddr.s_addr;
			if (sStaticIP != INADDR_NONE)
			{
               LOG (0, "%s: statically assigned to address %s",
								haddrtoa(pDhcpPkt->chaddr, pDhcpPkt->hlen,':'),
								inet_ntoa (* (struct in_addr *) & sStaticIP) );
               pDhcpPkt->yiaddr.s_addr = sStaticIP;
			}
			else
			{
               p  = DHCPSearchOptionsField (pDhcpPkt->options, DHO_DHCP_REQUESTED_ADDRESS, NULL);
               if (p!=NULL)
              {
// Change to avoid ciaddr to be returned if not comming. ciaddr returned will be the received one
				   in_RequestedAddr = * (struct in_addr *) p;
                   LOG (5, "Client requested address %s", inet_ntoa (in_RequestedAddr));
              }
              pProposedIP  = DHCP_IPAllocate (nDhcpType, & in_RequestedAddr, pDhcpPkt->chaddr, pDhcpPkt->hlen);
              if (pProposedIP == NULL)
              {
                  LOG (1, "no more address or address previously allocated by another server");
                  return FALSE;
              }
              pDhcpPkt->yiaddr.s_addr = pProposedIP->dwIP.s_addr;
              LOG (2, "%s: proposed address %s", IsDHCP(*pDhcpPkt) ? "DHCP" : "BOOTP", inet_ntoa (pProposedIP->dwIP) );
			  iCounterDHCP=0;
              //If this is a bootp, there is no other response from the client.
              //Since we don't want leases expiring (or being mistaken for unAcked DHCP offers),
              //set renewed to a distant time
              if(nDhcpType == 0  &&  sStaticIP == INADDR_NONE)     // patched by Rolf Offermanns
                  ForceRenewTime(pProposedIP, 0x66666666);         // fixed by Sam Leitch
            } // dynamically assigned address

            // populate the packet to be returned
            pDhcpPkt->op = BOOTREPLY;
            // translate $IP$ and $MAC$ from boot file name
            TranslateExp (sParamDHCP.szBootFile, pDhcpPkt->file, pDhcpPkt->yiaddr, pDhcpPkt->chaddr, iLastArch);
           *pSize = DHCPOptionsReply (pDhcpPkt, DHCPOFFER, receivingAddress, iLastArch);
            break ;

		//NJW Changed how requests are handled to mimic linux -- requests are responded to even if we didn't originally allocate, but only if the requested address is in our pool range

        case DHCPREQUEST :
         {BOOL bSERVER = FALSE;  // TRUE if Tftpd32 has assigned this address
           // Static Allocation ?
             // search field REQUEST ADDR in options
            sStaticIP = DHCP_StaticAssignation (pDhcpPkt);
			if (sStaticIP != INADDR_NONE)
			{
			    // populate the packet to be returned
                   pDhcpPkt->op = BOOTREPLY;
                   pDhcpPkt->yiaddr.s_addr = sStaticIP;
                 // translate $IP$ and $MAC$ from boot file name
                 TranslateExp (sParamDHCP.szBootFile, pDhcpPkt->file, pDhcpPkt->yiaddr, pDhcpPkt->chaddr, iLastArch);
                   *pSize = DHCPOptionsReply (pDhcpPkt, DHCPACK, receivingAddress, iLastArch);
				   break;
			}

           // has tftpd32 dynamically assigned this address
			pCurIP = DHCPSearchByMacAddress (pDhcpPkt->chaddr, pDhcpPkt->hlen);
			if (pCurIP==NULL)  return FALSE; // not attributed by Tftpd32 --> do not answer

			// ok we have proposed an IP address for this MAC Address,
			// but does the client request for our assignation
			// look either at the field ciaddr or at the DHO_DHCP_REQUESTED_ADDRESS option

            // search field REQUEST ADDR in options
            // if specified should fit database
			in_RequestedAddr.s_addr= pDhcpPkt->ciaddr.s_addr;
			p  = DHCPSearchOptionsField (pDhcpPkt->options, DHO_DHCP_REQUESTED_ADDRESS, NULL);
            if (p!=NULL)
			{
				in_RequestedAddr = * (struct in_addr *) p;
			}
			if(AddrFitsPool(&in_RequestedAddr))
			{
				//Look up the address, if it's not found, or the owner is this macaddr,
				//or the lease was expired, allow the serving.
				BOOL wasexpired = FALSE;
				pProposedIP = DHCPSearchByIP(&in_RequestedAddr, &wasexpired);
				bSERVER = !pProposedIP || wasexpired || (0 == memcmp(pProposedIP->sMacAddr, pDhcpPkt->chaddr, 6));
			}

			if (bSERVER  ||  (pDhcpPkt->ciaddr.s_addr==0 && sSettings.bPXECompatibility) )
			{

				pProposedIP  = DHCP_IPAllocate (nDhcpType, & in_RequestedAddr, pDhcpPkt->chaddr, pDhcpPkt->hlen);
				if (pProposedIP == NULL)
				{
					  LOG (1, "no more addresses or address previously allocated by another server");
					  return FALSE;
				}
				if (pProposedIP->tAllocated==0) SetAllocTime(pProposedIP);
				SetRenewTime(pProposedIP);
				LOG (5, "Previously allocated address %s acked", inet_ntoa (pProposedIP->dwIP));
				// populate the packet to be returned
				pDhcpPkt->op = BOOTREPLY;
				pDhcpPkt->yiaddr.s_addr = pProposedIP->dwIP.s_addr;
				TranslateExp (sParamDHCP.szBootFile, pDhcpPkt->file, pDhcpPkt->yiaddr, pDhcpPkt->chaddr, iLastArch);
				*pSize = DHCPOptionsReply (pDhcpPkt, DHCPACK, receivingAddress, iLastArch);
			}
			else
			{
				LOG (5, "Client requested address %s which was not allocated by tftpd32 and is either outside our pool or is used by someone else",
							  inet_ntoa (in_RequestedAddr) );
				return FALSE ; // do not answer
 			}
           } // Block for bSERVER declaration
           break;


        case DHCPDECLINE :
             // search current item and its precedent
          pCurIP = DHCPSearchByMacAddress (pDhcpPkt->chaddr, pDhcpPkt->hlen);
           if (pCurIP!=NULL)
           {
             p  = DHCPSearchOptionsField (pDhcpPkt->options, DHO_DHCP_REQUESTED_ADDRESS, NULL);
             if (p!=NULL)
              {
                 in_RequestedAddr.s_addr = * (DWORD *) p;
                 if ( pCurIP->dwIP.s_addr==in_RequestedAddr.s_addr)
                 {
                     DHCPDestroyItem (pCurIP);
                     LOG (5, "item destroyed");
                 }
             }
           }
		   /* 原版随后用 SendARP 登记“已被占用”的地址；鸿蒙无 ARP 权限 → 不登记
		      （见适配方案 §5 平台偏差） */
           break;

        case DHCPRELEASE :
            // do not destroy the item but mark it free
           pCurIP = DHCPSearchByMacAddress (pDhcpPkt->chaddr, pDhcpPkt->hlen);
           if (pCurIP!=NULL) // then mac address found in table
           {
                ZeroAllocTime(pCurIP);
                ZeroRenewTime(pCurIP);
                LOG (5, "item %s released", haddrtoa(pDhcpPkt->chaddr, pDhcpPkt->hlen,':') );
           }
           break;
       } // switch type


DHCPScan();

// answer only to BootP, Request or discover msg
return  (nDhcpType==0 || nDhcpType==DHCPDISCOVER || nDhcpType==DHCPREQUEST);
} // ProcessDHCPMessage



// ---------------------------------
// send the answer using broadcast and the source address specified into siaddr files
// PJO : Nov 2013

int DHCPSingleSend (SOCKET sDHCPskt, struct sockaddr_in *pTo, struct dhcp_packet *pDhcpPkt, int nSize, BOOL bUniCast)
{
int Rc;
struct servent *lpServEnt;
int save_port = pTo->sin_port;
    // really simple if DHCP bound to one address : use it for answer
	// if no source address was specified in the incoming request reply with a broadcast
	if (!bUniCast)
		pTo->sin_addr.s_addr = htonl (INADDR_BROADCAST);	// broadcast

	// answer to incoming port (67 or 68)
	Rc = sendto ( sDHCPskt,
			  		(char *) pDhcpPkt,
					nSize,
					0,
					(struct sockaddr *) pTo,
					sizeof *pTo);
	LOG (15, "Thread 0x%X: send %d bytes", GetCurrentThreadId(), nSize );
	if (Rc<nSize)
		LOG (1, "sendto error %d: %s", GetLastError(), LastErrorText ());

	if (     sSettings.bDoubleAnswer
		 &&  (pDhcpPkt->giaddr.s_addr!=htonl(INADDR_ANY)  || pDhcpPkt->giaddr.s_addr!=htonl(INADDR_BROADCAST))
		)
		{
			lpServEnt = getservbyname ("bootpc", "udp") ;
			// duplicates answer to sends to port bootps
			pTo->sin_port =  (lpServEnt != NULL) ?  (int) lpServEnt->s_port : htons (BOOTPC_PORT);
			Rc = sendto ( sDHCPskt,
						 (char *) pDhcpPkt,
						  nSize,
						  0,
						 (struct sockaddr *) pTo,
						  sizeof *pTo);
			pTo->sin_port = save_port;
	} // double answer for relay
return Rc;
}  // DHCPSingleSend


/* 原版 DHCPSendFrom 另建 socket 绑定 bootps(67) 再广播；
   鸿蒙不可绑特权端口，且监听 socket 已开 SO_BROADCAST → 直接复用监听 socket。
   报文内容、目的端口与源端口语义与原版一致（源端口取配置端口）。 */
static int DHCPSendFrom (SOCKET sDHCPskt, struct sockaddr_in *pFrom, struct sockaddr_in *pTo,
                         struct dhcp_packet *pDhcpPkt, int nSize)
{
    (void) pFrom;
    return DHCPSingleSend (sDHCPskt, pTo, pDhcpPkt, nSize, FALSE);
}


// recv a broadcast and note the address of the interface which has received the broadcast
int SktRcvAndGetAddrOfIncomingIf (SOCKET skt, char *buf, int bufsize, struct sockaddr_in *from, struct sockaddr_in *to)
{
struct msghdr msg;
struct iovec  iov;
char          ControlBuffer [256];
ssize_t       nRc;

	memset (to, 0, sizeof *to);
	memset (& msg, 0, sizeof msg);
	iov.iov_base = buf;
	iov.iov_len  = (size_t) bufsize;
	msg.msg_name    = from;
	msg.msg_namelen = sizeof *from;
	msg.msg_iov     = & iov;
	msg.msg_iovlen  = 1;
	msg.msg_control = ControlBuffer;
	msg.msg_controllen = sizeof ControlBuffer;

	nRc = recvmsg (skt, & msg, 0);
	if (nRc < 0)  return -1;

	{
		struct cmsghdr *pCmsg;
		for (pCmsg = CMSG_FIRSTHDR (& msg) ; pCmsg != NULL ; pCmsg = CMSG_NXTHDR (& msg, pCmsg))
		{
			if (pCmsg->cmsg_level == IPPROTO_IP && pCmsg->cmsg_type == IP_PKTINFO)
			{
				struct in_pktinfo *pInfo = (struct in_pktinfo *) CMSG_DATA (pCmsg);
				to->sin_family = AF_INET;
				/* ipi_spec_dst 为本机接收地址；广播包 ipi_addr 为 255.255.255.255 */
				to->sin_addr = pInfo->ipi_spec_dst.s_addr ? pInfo->ipi_spec_dst : pInfo->ipi_addr;
			}
		}
	}
	// 内核未附带 pktinfo（老内核/模拟器）→ 用首个非回环地址
	if (to->sin_addr.s_addr == 0)
		PalGetPrimaryIPv4 (& to->sin_addr);
return (int) nRc;
} // SktRcvAndGetAddrOfIncomingIf


// /////////////////////
// DHCP Thread
// /////////////////////

void ListenDhcpMessage (void *lpVoid)
{
struct dhcp_packet      sDhcpPkt;
char szHostname [128], *p;
int                     Rc, nSize;
struct sockaddr_in      SockFrom, SockTo;
int                     True=TRUE;
socklen_t               nFromLen = sizeof SockFrom;
BOOL                    bUniCast;
    (void) lpVoid;

    DHCPReadConfig ();
	tThreads[TH_DHCP].bInit = TRUE;		// inits OK

	// let the GUI start
    Sleep (1000);

	 //NJW Prompt to see if we should discover via ping before we start doing DHCP
	 if(scanforleases)
	 {
		struct in_addr addr;
		int count;
		ReadKey(TFTPD32_DHCP_KEY, KEY_DHCP_POOL, & addr.s_addr, sizeof(addr.s_addr), REG_DWORD, szTftpd32IniFile);
		ReadKey(TFTPD32_DHCP_KEY, KEY_DHCP_POOLSIZE, &count, sizeof(count), REG_DWORD, szTftpd32IniFile);
		SetNumAllocated(0);
		FreeLeases(FALSE);
		if (sSettings.bPersLeases)  LoadLeases ();
		/* PingRange 依赖 raw socket，鸿蒙不可用 → 只重载租约 */
		SVC_WARNING ("Lease file updated (ping scan not available on HarmonyOS)");
	 }

    // add send broadcast capacity to DHCP listening socket
    Rc =    setsockopt (tThreads[TH_DHCP].skt, SOL_SOCKET, SO_BROADCAST, (const char *) & True, sizeof True)==0 ;
    if (! Rc)
	{
		SVC_WARNING ("Can not add broadcast capability to DHCP socket ! \nError %d", GetLastError ());
    }
    // enable IP_PKTINFO to know the receiving interface (POSIX equivalent of WSARecvMsg)
	Rc = setsockopt (tThreads[TH_DHCP].skt, IPPROTO_IP, IP_PKTINFO, (char *) & True, sizeof True) ;
	if (Rc==-1)
	{
		SVC_WARNING ("Can not add PKTINFO capability to DHCP socket ! \nError %d", GetLastError ());
    }

   while ( tThreads[TH_DHCP].gRunning )
   {
        memset (& sDhcpPkt, 0, sizeof sDhcpPkt);
        memset (& SockFrom, 0, sizeof SockFrom);
        memset (& SockTo, 0, sizeof SockTo);

		// try to receive request and get incoming interface
		Rc = SktRcvAndGetAddrOfIncomingIf (tThreads[TH_DHCP].skt,
											(char *) & sDhcpPkt,
											sizeof sDhcpPkt,
											& SockFrom,
											& SockTo );
		// fallback: plain recvfrom (single-homed hosts / kernels without pktinfo)
		if (Rc==-1  &&  (errno==ENOSYS || errno==EINVAL))
		{
			nFromLen = sizeof SockFrom;
			Rc = recvfrom ( tThreads[TH_DHCP].skt,
							(char *) & sDhcpPkt,
							sizeof sDhcpPkt,
							0,
							(struct sockaddr *) & SockFrom,
							& nFromLen);
		}

		// recv error
      // since the service sends broadcasts, it receives its own message, just ignore it
        if (Rc < 0)
        {
			if (! tThreads[TH_DHCP].gRunning)  break;
             if (errno != ECONNRESET)
              {
                 LOG (1, "Recv error %d", errno);
                 Sleep (500);
              }
              continue;
        } // recv failed

        // if msg is too short
        // If all bootP fields have been read
        if (Rc < (int) offsetof ( struct dhcp_packet, options ))
        {
           LOG (5, "Message truncated (length was %d)", Rc);
           if ( tThreads[TH_DHCP].gRunning ) Sleep (500);
           continue;
        }

		// receive on a unbound interface
		if (    sSettings.szDHCPLocalIP[0]!=0
			&&  SockTo.sin_addr.s_addr != inet_addr (sSettings.szDHCPLocalIP))
        {
           LOG (1, "Message received on an unbound interface (IP %s)", inet_ntoa (SockTo.sin_addr));
           Sleep (10);
           continue;
        }

        // if pool is empty and MAC address not statically assigned : ignore request
        if (    sParamDHCP.nPoolSize == 0
            &&  DHCP_StaticAssignation (& sDhcpPkt)==INADDR_NONE )
        {
           Sleep (10);
           continue;
        }

        // handle only nul-terminated strings
        sDhcpPkt.sname[sizeof sDhcpPkt.sname - 1] = 0;
        sDhcpPkt.file [sizeof sDhcpPkt.file  - 1] = 0;

        // read host name, truncate it
        if (gethostname (szHostname , sizeof szHostname )==SOCKET_ERROR)
              lstrcpy (szHostname, "Tftpd32DchpServer");
        if ((p=strchr (szHostname, '.'))!=NULL)  *p=0;
        szHostname [sizeof sDhcpPkt.sname - 1] = 0;

        if (sDhcpPkt.sname[0]!=0  && lstrcmp ((const char *) sDhcpPkt.sname, szHostname)!=0)
        {
           LOG (2, "Packet addressed to %s", sDhcpPkt.sname);
            continue;
        }

        // we have only to answer to BOOTREQUEST msg
        if (sDhcpPkt.op != BOOTREQUEST)
        {
            LOG (2, "%d Request %d not processed", GetCurrentThreadId (),sDhcpPkt.op);
            continue ;
        }

        // if request OK and answer ready
        /* 原版把 127.x 视为广播；这里允许回环单播，便于设备内自测（DH-01~05） */
        bUniCast =  (     SockFrom.sin_addr.s_addr!=htonl (INADDR_NONE)
                      &&  SockFrom.sin_addr.s_addr!=htonl (INADDR_ANY) ) ;
        pthread_mutex_lock (& s_leaseMx);
        Rc = ProcessDHCPMessage ( & sDhcpPkt, & nSize, &SockTo );
        if (Rc)
        {
		   // send reply
		   // -> if unicast replies, use default behaviour
		   // -> otherwise, send a broadcast on the incoming interface
		   if (bUniCast)
				DHCPSingleSend (tThreads[TH_DHCP].skt, & SockFrom, & sDhcpPkt, nSize, bUniCast);
		   else
				DHCPSendFrom (tThreads[TH_DHCP].skt, & SockTo, & SockFrom, & sDhcpPkt, nSize);
        }  // ProcessDHCPMessage
        pthread_mutex_unlock (& s_leaseMx);

   } // do it eternally

LogToMonitor ("DHCP thread ends here\n");
    pthread_exit (NULL);
} // ListenDhcpMessage


/* UI 轮询：租约表 JSON。字段与原版 GUI 列一致：IP / MAC / 分配时间 / 续租时间 / 状态 */
int PalDhcpLeasesJson (char *szOut, int nOut)
{
int Ark, nLen = 0;
time_t tNow, tExpire;
    if (szOut == NULL || nOut < 8)  return 0;
    szOut [0] = 0;
    pthread_mutex_lock (& s_leaseMx);
    time (& tNow);
    tExpire = tNow - sParamDHCP.nLease * 60;
    nLen += snprintf (szOut + nLen, (size_t) (nOut - nLen), "[");
    for (Ark = 0 ;  Ark < nAllocatedIP && nLen < nOut - 160 ;  Ark ++)
    {
        struct LL_IP *p = tFirstIP [Ark];
        const char *szState = (p->tAllocated == 0) ? "free"
                            : (p->tRenewed == 0)   ? "offer"
                            : (tExpire > p->tRenewed) ? "expired" : "ack";
        nLen += snprintf (szOut + nLen, (size_t) (nOut - nLen),
                          "%s{\"ip\":\"%s\",\"mac\":\"%s\",\"alloc\":%lld,\"renew\":%lld,\"state\":\"%s\"}",
                          Ark ? "," : "",
                          inet_ntoa (p->dwIP),
                          haddrtoa ((const unsigned char *) p->sMacAddr, 6, ':'),
                          (long long) p->tAllocated, (long long) p->tRenewed, szState);
    }
    if (nLen < nOut - 2)  nLen += snprintf (szOut + nLen, (size_t) (nOut - nLen), "]");
    pthread_mutex_unlock (& s_leaseMx);
return nLen;
}
