//////////////////////////////////////////////////////////
// registry_pal.c —— 替代原 _libs/registry/registry.c
// 原版通过注册表键 + ini 双写保存配置；鸿蒙无注册表，这里把
// “注册表路径 + 键名” 统一映射到单一 KEY=value 文件（与原版 ini 键名一致）。
// 供 bootpd.c / bootpd_util.c 的 ReadKey/SaveKey/AsyncSaveKey 使用。
//////////////////////////////////////////////////////////
#include "headers.h"
#include <pthread.h>

const char * Pal_GetConfigPath (void);   /* settings_pal.c */

static pthread_mutex_t s_iniMx = PTHREAD_MUTEX_INITIALIZER;

/* 键名匹配：行首 key 后紧跟 '='，避免 "Lease_1_MAC" 误命中 "Lease_1_MACX" */
static int PalIniLineMatch (const char *szLine, const char *szKey)
{
    size_t n = strlen (szKey);
    return strncmp (szLine, szKey, n) == 0 && szLine [n] == '=';
}

static void PalIniTrim (char *sz)
{
    char *pEnd = sz + strlen (sz);
    while (pEnd > sz && (pEnd [-1] == '\n' || pEnd [-1] == '\r'))
        * (-- pEnd) = 0;
}

int ReadKey (const char *szRegPath, const char *szKey, void *buf, int BufSize,
             int nType, const char *szIniFile)
{
FILE *f;
char szLine [1024];
int  bFound = FALSE;
    (void) szRegPath;  (void) szIniFile;      /* 单一配置文件，路径参数仅为兼容签名 */
    if (szKey == NULL || buf == NULL || BufSize <= 0)  return FALSE;

    pthread_mutex_lock (& s_iniMx);
    f = fopen (Pal_GetConfigPath (), "r");
    if (f != NULL)
    {
        while (fgets (szLine, sizeof szLine, f) != NULL)
        {
            if (! PalIniLineMatch (szLine, szKey))  continue;
            {
                const char *szVal = szLine + strlen (szKey) + 1;
                if (nType == REG_DWORD)
                    * (int *) buf = (int) strtol (szVal, NULL, 10);
                else
                {
                    char szTmp [1024];
                    strncpy (szTmp, szVal, sizeof szTmp - 1);
                    szTmp [sizeof szTmp - 1] = 0;
                    PalIniTrim (szTmp);
                    strncpy ((char *) buf, szTmp, (size_t) BufSize - 1);
                    ((char *) buf) [BufSize - 1] = 0;
                }
                bFound = TRUE;
            }
            break;
        }
        fclose (f);
    }
    pthread_mutex_unlock (& s_iniMx);
return bFound;
} /* ReadKey */

/* 覆盖写：保留其它行的原始顺序，仅替换目标行；缺行则追加 */
static int PalIniSet (const char *szKey, const char *szVal)
{
const char *szPath = Pal_GetConfigPath ();
char szTmpPath [MAX_PATH + 8];
FILE *fSrc, *fDst;
char szLine [1024];
int  bFound = FALSE, bOk;

    if (szPath [0] == 0 || szKey == NULL || szVal == NULL)  return FALSE;
    snprintf (szTmpPath, sizeof szTmpPath, "%s.tmp", szPath);

    pthread_mutex_lock (& s_iniMx);
    fSrc = fopen (szPath, "r");
    fDst = fopen (szTmpPath, "w");
    if (fDst == NULL)
    {
        if (fSrc != NULL)  fclose (fSrc);
        pthread_mutex_unlock (& s_iniMx);
        return FALSE;
    }
    if (fSrc != NULL)
    {
        while (fgets (szLine, sizeof szLine, fSrc) != NULL)
        {
            if (! bFound && PalIniLineMatch (szLine, szKey))
            {
                fprintf (fDst, "%s=%s\n", szKey, szVal);
                bFound = TRUE;
            }
            else fputs (szLine, fDst);
        }
        fclose (fSrc);
    }
    if (! bFound)  fprintf (fDst, "%s=%s\n", szKey, szVal);
    bOk = (fclose (fDst) == 0);
#ifdef _WIN32
    /* MSVC 的 rename 在目标已存在时失败（POSIX 直接覆盖），
       宿主侧单元测试在 Windows 上跑，需先删掉旧文件 */
    if (bOk)  remove (szPath);
#endif
    if (bOk)  bOk = (rename (szTmpPath, szPath) == 0);
    if (! bOk)  remove (szTmpPath);
    pthread_mutex_unlock (& s_iniMx);
return bOk;
} /* PalIniSet */

int SaveKey (const char *szRegPath, const char *szKey, const void *buf, int BufSize,
             int nType, const char *szIniFile)
{
char szVal [32];
    (void) szRegPath;  (void) szIniFile;
    if (szKey == NULL || buf == NULL)  return FALSE;
    if (nType == REG_DWORD)
    {
        snprintf (szVal, sizeof szVal, "%d", * (const int *) buf);
        return PalIniSet (szKey, szVal);
    }
    (void) BufSize;
    return PalIniSet (szKey, (const char *) buf);
}

/* 原版异步写仅用于避免 GUI 卡顿；单线程写 ini 开销极小，直接同步落盘 */
int AsyncSaveKey (const char *szRegPath, const char *szKey, const void *buf, int BufSize,
                  int nType, const char *szIniFile)
{ return SaveKey (szRegPath, szKey, buf, BufSize, nType, szIniFile); }
