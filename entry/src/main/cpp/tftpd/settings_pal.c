//////////////////////////////////////////////////////////
// settings_pal.c —— 替代 settings.c + registry.c
// 文件格式：KEY=value 行；KEY 名与原版注册表键完全一致（兼容性强原则）
// 默认值逐项对照原 settings.c 初始化器；默认端口 6900 见适配方案 §5-R1
//////////////////////////////////////////////////////////
#include "headers.h"

struct S_Tftpd32Settings sSettings =
{
    ".",                    // BaseDirectory
    TFTPD32_DEF_LOG_LEVEL,  // LogLvl
    TFTP_TIMEOUT,           // Timeout
    TFTP_RETRANSMIT,        // Retransmit
    0,                      // WinSize
    SECURITY_STD,           // SecurityLvl
    6900,                   // Port（应用禁止绑 <1024，原版默认 69，UI 可改）
    0,                      // SyslogPort（0 → ReadSettings 末尾注入默认 5140）
    0,                      // SntpPort（0 → 默认 1230）
    0,                      // DnsPort（0 → 默认 5300）
    0,                      // DhcpPort（0 → 默认 6700；原版 67 为特权端口）
    FALSE,                  // bHide
    TRUE,                   // bNegociate（RFC 1782-1785）
    FALSE,                  // bPXECompatibility
    TRUE,                   // bProgressBar
    FALSE,                  // bDirText
    FALSE,                  // bMD5
    FALSE,                  // bResumeOption
    TRUE,                   // bUnixStrings
    FALSE,                  // bBeep
    FALSE,                  // bVirtualRoot
    "",                     // szTftpLocalIP
    TFTPD32_TFTP_SERVER,    // uServices（里程碑1 仅 TFTP）
    0, 0,                   // nTftpLowPort / nTftpHighPort
    "",                     // szTftpLogFile
    "",                     // szSyslogFile
    FALSE,                  // bSyslogPipe
    "",                     // szMCastAddr
    0,                      // dwMCastPort
    1,                      // bPersLeases
    0,                      // bUnicastBOOTP
    0,                      // bPing（DHCP Ping 检测，鸿蒙无 ICMP，恒关）
    0,                      // bDoubleAnswer
    "",                     // szDHCPLocalIP
    FALSE,                  // bEventLog
    "tftpd32",              // szConsolePwd
    FALSE,                  // bPortOption
    10,                     // nGuiRemanence（原 settings.c 默认：结束行保留 10 秒）
    FALSE,                  // bIgnoreLastBlockAck
    TRUE,                   // bIPv4
    TRUE,                   // bIPv6
    FALSE,                  // bReduceTFTPPath
    "",                     // szHttpDirectory
    100,                    // dwMaxTftpTransfers
    "",                     // szWorkingDirectory
    1000,                   // dwRefreshInterval
    0,                      // uConsolePort
    FALSE,                  // bTftpOnPhysicalIf
    0,                      // uRunningServices
};

static char s_szCfgPath [MAX_PATH] = "";

/* bootpd*.c 沿用原版全局变量（ReadKey/SaveKey 的路径参数），此处统一指向单一 ini */
char szTftpd32IniFile [MAX_PATH] = "";
char szTftpd32Help    [MAX_PATH] = "";

static void lstrcpyn_pal (char *dst, int n, const char *src)
{ strncpy (dst, src, n - 1);  dst [n - 1] = 0; }

void Pal_SetConfigPath (const char *szPath)   /* napi_init 传入沙箱路径 */
{
    lstrcpyn_pal (s_szCfgPath, sizeof s_szCfgPath, szPath);
    lstrcpyn_pal (szTftpd32IniFile, sizeof szTftpd32IniFile, szPath);
}

const char * Pal_GetConfigPath (void) { return s_szCfgPath; }

enum { PAL_T_INT, PAL_T_STR };
struct PalKey { const char *name; void *p; int type; int size; };
static const struct PalKey tKeys [] =
{
    { KEY_BASEDIR,              & sSettings.szBaseDirectory,        PAL_T_STR, sizeof sSettings.szBaseDirectory },
    { KEY_DEFSECURITY,          & sSettings.SecurityLvl,            PAL_T_INT, 0 },
    { KEY_TIMEOUT,              & sSettings.Timeout,                PAL_T_INT, 0 },
    { KEY_MAXRETRANSMIT,        & sSettings.Retransmit,             PAL_T_INT, 0 },
    { KEY_PORT,                 & sSettings.Port,                   PAL_T_INT, 0 },
    { KEY_SYSLOG_PORT,          & sSettings.SyslogPort,             PAL_T_INT, 0 },
    { KEY_SNTP_PORT,            & sSettings.SntpPort,               PAL_T_INT, 0 },
    { KEY_DNS_PORT,             & sSettings.DnsPort,                PAL_T_INT, 0 },
    { KEY_DHCP_PORT,            & sSettings.DhcpPort,               PAL_T_INT, 0 },
    { KEY_WINSIZE,              & sSettings.WinSize,                PAL_T_INT, 0 },
    { KEY_NEGOCIATE,            & sSettings.bNegociate,             PAL_T_INT, 0 },
    { KEY_PXE,                  & sSettings.bPXECompatibility,      PAL_T_INT, 0 },
    { KEY_DIRTEXT,              & sSettings.bDirText,               PAL_T_INT, 0 },
    { KEY_PROGRESS,             & sSettings.bProgressBar,           PAL_T_INT, 0 },
    { KEY_MD5,                  & sSettings.bMD5,                   PAL_T_INT, 0 },
    { KEY_UNIX,                 & sSettings.bUnixStrings,           PAL_T_INT, 0 },
    { KEY_BEEP,                 & sSettings.bBeep,                  PAL_T_INT, 0 },
    { KEY_VROOT,                & sSettings.bVirtualRoot,           PAL_T_INT, 0 },
    { KEY_TFTPLOCALIP,          & sSettings.szTftpLocalIP,          PAL_T_STR, sizeof sSettings.szTftpLocalIP },
    { KEY_SERVICES,             & sSettings.uServices,              PAL_T_INT, 0 },
    { KEY_TFTPLOGFILE,          & sSettings.szTftpLogFile,          PAL_T_STR, sizeof sSettings.szTftpLogFile },
    { KEY_SYSLOGFILE,           & sSettings.szSyslogFile,           PAL_T_STR, sizeof sSettings.szSyslogFile },
    { KEY_LOWEST_PORT,          & sSettings.nTftpLowPort,           PAL_T_INT, 0 },
    { KEY_HIGHEST_PORT,         & sSettings.nTftpHighPort,          PAL_T_INT, 0 },
    { KEY_MAX_TRANSFERS,        & sSettings.dwMaxTftpTransfers,     PAL_T_INT, 0 },
    { KEY_REDUCE_PATH,          & sSettings.bReduceTFTPPath,        PAL_T_INT, 0 },
    { KEY_IGNORE_LASTBLOCK_ACK, & sSettings.bIgnoreLastBlockAck,    PAL_T_INT, 0 },
    { KEY_GUI_REMANENCE,        & sSettings.nGuiRemanence,          PAL_T_INT, 0 },
    { KEY_IPv6,                 & sSettings.bIPv6,                  PAL_T_INT, 0 },
    { KEY_PORT_OPTION,          & sSettings.bPortOption,            PAL_T_INT, 0 },
    { KEY_MCAST_ADDR,             sSettings.szMCastAddr,           PAL_T_STR, sizeof sSettings.szMCastAddr },
    { KEY_MCAST_PORT,           & sSettings.dwMCastPort,            PAL_T_INT, 0 },

    /* —— DHCP（键名与原版注册表一致；与 DHCPReadConfig 的表同源，重复装载无害）—— */
    { KEY_DHCP_POOL,             sParamDHCP.szAddr,         PAL_T_STR, sizeof sParamDHCP.szAddr },
    { KEY_DHCP_POOLSIZE,       & sParamDHCP.nPoolSize,      PAL_T_INT, 0 },
    { KEY_DHCP_BOOTFILE,         sParamDHCP.szBootFile,     PAL_T_STR, sizeof sParamDHCP.szBootFile },
    { KEY_DHCP_DNS,              sParamDHCP.szDns1,         PAL_T_STR, sizeof sParamDHCP.szDns1 },
    { KEY_DHCP_DNS2,             sParamDHCP.szDns2,         PAL_T_STR, sizeof sParamDHCP.szDns2 },
    { KEY_DHCP_WINS,             sParamDHCP.szWins,         PAL_T_STR, sizeof sParamDHCP.szWins },
    { KEY_DHCP_MASK,             sParamDHCP.szMask,         PAL_T_STR, sizeof sParamDHCP.szMask },
    { KEY_DHCP_DEFROUTER,        sParamDHCP.szGateway,      PAL_T_STR, sizeof sParamDHCP.szGateway },
    { KEY_DHCP_OPTION42,         sParamDHCP.szOpt42,        PAL_T_STR, sizeof sParamDHCP.szOpt42 },
    { KEY_DHCP_OPTION120,        sParamDHCP.szOpt120,       PAL_T_STR, sizeof sParamDHCP.szOpt120 },
    { KEY_DHCP_DOMAINNAME,       sParamDHCP.szDomainName,   PAL_T_STR, sizeof sParamDHCP.szDomainName },
    { KEY_DHCP_LEASE_TIME,     & sParamDHCP.nLease,         PAL_T_INT, 0 },
    { KEY_PERS_LEASES,         & sSettings.bPersLeases,     PAL_T_INT, 0 },
    { KEY_UNICAST_BOOTP,       & sSettings.bUnicastBOOTP,   PAL_T_INT, 0 },
    { KEY_PING,                & sSettings.bPing,           PAL_T_INT, 0 },
    { KEY_DOUBLE_ANSWER,       & sSettings.bDoubleAnswer,   PAL_T_INT, 0 },
    { KEY_DHCP_LOCIP,            sSettings.szDHCPLocalIP,   PAL_T_STR, sizeof sSettings.szDHCPLocalIP },
};

BOOL Tftpd32ReadSettings (void)
{
FILE *f;
char line [768];
    if (s_szCfgPath [0] != 0)
    {
        f = fopen (s_szCfgPath, "r");
        if (f != NULL)      // 无配置 → 用默认值
        {
            while (fgets (line, sizeof line, f) != NULL)
            {
                char *pEq = strchr (line, '=');
                int Ark;
                if (pEq == NULL)  continue;
                * pEq = 0;
                line [strcspn (line, "\r\n")] = 0;
                {   /* 去掉值尾部的 CR/LF 与空白：ini 的行结束符不能进入配置值
                     * （否则 BaseDirectory 等字符串键会带 '\n'，拼路径时 ENOENT） */
                    char *pVal = pEq + 1;
                    char *pEnd = pVal + strlen (pVal);
                    while (pEnd > pVal && (pEnd [-1] == '\n' || pEnd [-1] == '\r'
                                        || pEnd [-1] == ' '  || pEnd [-1] == '\t'))
                        * (-- pEnd) = 0;
                }
                for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
                    if (strcmp (tKeys [Ark].name, line) == 0)
                    {
                        if (tKeys [Ark].type == PAL_T_INT)
                            * (unsigned *) tKeys [Ark].p = (unsigned) strtoul (pEq + 1, NULL, 10);
                        else
                            lstrcpyn_pal ((char *) tKeys [Ark].p, tKeys [Ark].size, pEq + 1);
                        break;
                    }
            }
            fclose (f);
        }
    }

    /* —— WorkingDirectory 推导（与原版 settings.c Tftpd32ReadSettings 语义一致）——
     * 环境变量 TFTP_DIR（有效目录）→ BaseDirectory（有效目录）→ 当前目录。
     * TftpExtendFileName 用 szWorkingDirectory 拼全路径，缺失时文件操作落在 CWD("/") 上。 */
    {
    const char *pszEnv = getenv ("TFTP_DIR");
    struct stat st;
    char szFull [1024];      // realpath 可能写入至 PATH_MAX，先落大缓冲再截断拷贝，避免溢出
        /* 首次运行（或目录被清空）时根目录可能不存在：原版由 GUI 设置对话框负责创建，
         * 无 GUI 时在此补齐，否则 WorkingDirectory 会回落到 CWD("/") 导致文件操作 EACCES */
        if (sSettings.szBaseDirectory[0] != 0 && stat (sSettings.szBaseDirectory, & st) != 0)
        {
            int RcMk = mkdir (sSettings.szBaseDirectory, 0755);
            LogToMonitor ("PalSettings: mkdir <%s> returns %d (errno %d)",
                          sSettings.szBaseDirectory, RcMk, errno);
        }
        if (pszEnv != NULL && stat (pszEnv, & st) == 0 && S_ISDIR (st.st_mode))
            lstrcpyn_pal (sSettings.szWorkingDirectory, sizeof sSettings.szWorkingDirectory,
                          realpath (pszEnv, szFull) ? szFull : pszEnv);
        else if (stat (sSettings.szBaseDirectory, & st) == 0 && S_ISDIR (st.st_mode))
            lstrcpyn_pal (sSettings.szWorkingDirectory, sizeof sSettings.szWorkingDirectory,
                          realpath (sSettings.szBaseDirectory, szFull) ? szFull : sSettings.szBaseDirectory);
        else
            getcwd (sSettings.szWorkingDirectory, sizeof sSettings.szWorkingDirectory);
        LogToMonitor ("PalSettings: working directory <%s>", sSettings.szWorkingDirectory);
    }

    /* —— 服务端口缺省兜底：无配置文件或旧配置缺少键时 Port 均为 0 —— */
    if (sSettings.SyslogPort == 0)  sSettings.SyslogPort = 5140;
    if (sSettings.SntpPort   == 0)  sSettings.SntpPort   = 1230;
    if (sSettings.DnsPort    == 0)  sSettings.DnsPort    = 5300;
    if (sSettings.DhcpPort   == 0)  sSettings.DhcpPort   = 6700;

    /* DHCP 参数（IP_Pool 等 27 键）装载；服务启动时 ListenDhcpMessage 会再次装载 */
    DHCPReadConfig ();
return TRUE;
}

BOOL Tftpd32SaveSettings (void)
{
int Ark, nRc = 0;
    if (s_szCfgPath [0] == 0)  return FALSE;
    /* 逐键覆盖写，保留本表未登记的键（Lease_* 租约、AddOptionNumber* 等），
       原版实现是 fopen("w") 整表重写，会把 DHCP 动态键抹掉 */
    for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    {
        if (tKeys [Ark].type == PAL_T_INT)
            nRc += SaveKey (NULL, tKeys [Ark].name, tKeys [Ark].p, 0, REG_DWORD, NULL);
        else
            nRc += SaveKey (NULL, tKeys [Ark].name, tKeys [Ark].p, 0, REG_SZ, NULL);
    }
return nRc > 0;
}

BOOL Tftpd32DestroySettings (void)
{ if (s_szCfgPath [0])  remove (s_szCfgPath);  return TRUE; }

/* napi 桥按键名写入（值已校验） */
BOOL Pal_SetSettingInt (const char *key, unsigned val)
{ int Ark;  for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    if (strcmp (tKeys [Ark].name, key) == 0 && tKeys [Ark].type == PAL_T_INT)
    { * (unsigned *) tKeys [Ark].p = val;  return TRUE; }
return FALSE; }

BOOL Pal_SetSettingStr (const char *key, const char *val)
{ int Ark;  for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    if (strcmp (tKeys [Ark].name, key) == 0 && tKeys [Ark].type == PAL_T_STR)
    { lstrcpyn_pal ((char *) tKeys [Ark].p, tKeys [Ark].size, val);  return TRUE; }
return FALSE; }

/* napi 桥按键名读取（GUI 启动时恢复已保存配置） */
BOOL Pal_GetSettingInt (const char *key, unsigned *pVal)
{ int Ark;  for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    if (strcmp (tKeys [Ark].name, key) == 0 && tKeys [Ark].type == PAL_T_INT)
    { * pVal = * (unsigned *) tKeys [Ark].p;  return TRUE; }
return FALSE; }

/* napi 桥按键名读取字符串（不区分大小写，复用 tKeys 表） */
BOOL Pal_GetSettingStr (const char *szKey, char *szOut, int nOut)
{ int Ark;
    if (szOut == NULL || nOut <= 0)  return FALSE;
    szOut [0] = 0;
    for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
        if (lstrcmpi (tKeys [Ark].name, szKey) == 0 && tKeys [Ark].type == PAL_T_STR)
        {
            lstrcpyn_pal (szOut, nOut, (char *) tKeys [Ark].p);
            return TRUE;
        }
return FALSE; }
