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
    0,                      // nGuiRemanence
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

static void lstrcpyn_pal (char *dst, int n, const char *src)
{ strncpy (dst, src, n - 1);  dst [n - 1] = 0; }

void Pal_SetConfigPath (const char *szPath)   /* napi_init 传入沙箱路径 */
{ lstrcpyn_pal (s_szCfgPath, sizeof s_szCfgPath, szPath); }

enum { PAL_T_INT, PAL_T_STR };
struct PalKey { const char *name; void *p; int type; int size; };
static const struct PalKey tKeys [] =
{
    { KEY_BASEDIR,              & sSettings.szBaseDirectory,        PAL_T_STR, sizeof sSettings.szBaseDirectory },
    { KEY_DEFSECURITY,          & sSettings.SecurityLvl,            PAL_T_INT, 0 },
    { KEY_TIMEOUT,              & sSettings.Timeout,                PAL_T_INT, 0 },
    { KEY_MAXRETRANSMIT,        & sSettings.Retransmit,             PAL_T_INT, 0 },
    { KEY_PORT,                 & sSettings.Port,                   PAL_T_INT, 0 },
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
    { KEY_LOWEST_PORT,          & sSettings.nTftpLowPort,           PAL_T_INT, 0 },
    { KEY_HIGHEST_PORT,         & sSettings.nTftpHighPort,          PAL_T_INT, 0 },
    { KEY_MAX_TRANSFERS,        & sSettings.dwMaxTftpTransfers,     PAL_T_INT, 0 },
    { KEY_REDUCE_PATH,          & sSettings.bReduceTFTPPath,        PAL_T_INT, 0 },
    { KEY_IGNORE_LASTBLOCK_ACK, & sSettings.bIgnoreLastBlockAck,    PAL_T_INT, 0 },
    { KEY_GUI_REMANENCE,        & sSettings.nGuiRemanence,          PAL_T_INT, 0 },
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
return TRUE;
}

BOOL Tftpd32SaveSettings (void)
{
FILE *f;
int Ark;
    if (s_szCfgPath [0] == 0)  return FALSE;
    f = fopen (s_szCfgPath, "w");
    if (f == NULL)  return FALSE;
    for (Ark = 0 ; Ark < (int)(sizeof tKeys / sizeof tKeys [0]) ; Ark ++)
    {
        if (tKeys [Ark].type == PAL_T_INT)
            fprintf (f, "%s=%u\n", tKeys [Ark].name, * (unsigned *) tKeys [Ark].p);
        else
            fprintf (f, "%s=%s\n", tKeys [Ark].name, (char *) tKeys [Ark].p);
    }
    fclose (f);
return TRUE;
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
