//////////////////////////////////////////////////////////
// file_tool_pal.c —— MD5 计算与目录列表（GUI Directory 页签 / MD5 工具）
//////////////////////////////////////////////////////////
#include "headers.h"

BOOL PalComputeMd5 (const char *szPath, char *szOut, int nOut)
{
HANDLE h;
char   buf [8192];
DWORD  nRead = 0;
MD5_CTX ctx;
unsigned char id [16];
int Ark;

    if (nOut < 33 || szPath == NULL || szOut == NULL)  return FALSE;

    h = CreateFile (szPath, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE)  return FALSE;

    MD5Init (& ctx);
    while (ReadFile (h, buf, sizeof buf, & nRead, NULL) && nRead > 0)
        MD5Update (& ctx, (unsigned char *) buf, (unsigned) nRead);
    CloseHandle (h);

    MD5Final (id, & ctx);
    for (Ark = 0; Ark < 16; Ark++)
        snprintf (szOut + 2 * Ark, 3, "%02x", id[Ark]);
    szOut[32] = 0;
    return TRUE;
}

int PalListDirectory (const char *szPath, char *szJson, int nJson)
{
DIR *d;
struct dirent *e;
const char *pszDir;
int n = 0, nFirst = 1;

    if (szJson == NULL || nJson <= 0)  return 0;
    szJson [0] = 0;
    if (nJson < 3)  return 0;

    pszDir = (szPath != NULL) ? szPath : ".";
    d = opendir (pszDir);
    if (d == NULL)
    {
        int nc = snprintf (szJson, (size_t) nJson, "[]");
        return (nc < nJson) ? nc : nJson - 1;
    }

    n = snprintf (szJson, (size_t) nJson, "[");
    if (n > nJson - 1)  n = nJson - 1;

    while ((e = readdir (d)) != NULL)
    {
        char szFull [1024], szName [512];
        struct stat st;
        int  bDir = 0, nc;
        long long qwSize = 0;

        if (strcmp (e->d_name, ".") == 0 || strcmp (e->d_name, "..") == 0)  continue;
        if (nJson - n < 340)  break;             /* 余量不足：安全截断 */

        snprintf (szFull, sizeof szFull, "%s/%s", pszDir, e->d_name);
        if (stat (szFull, & st) == 0)
        { bDir = S_ISDIR (st.st_mode) ? 1 : 0;  qwSize = (long long) st.st_size; }

        PalJsonEscape (szName, sizeof szName, e->d_name);
        nc = snprintf (szJson + n, (size_t) (nJson - n),
                       "%s{\"name\":\"%s\",\"size\":%lld,\"dir\":%d}",
                       nFirst ? "" : ",", szName, qwSize, bDir);
        if (nc < 0)  break;
        if (nc >= nJson - n)                     /* 已写满：截断并结束 */
        { n = nJson - 1;  szJson [n] = 0;  break; }
        n += nc;
        nFirst = 0;
    }
    closedir (d);

    if (nJson - n < 2)  return n;
    n += snprintf (szJson + n, (size_t) (nJson - n), "]");
    if (n > nJson - 1)  n = nJson - 1;
    return n;
}
