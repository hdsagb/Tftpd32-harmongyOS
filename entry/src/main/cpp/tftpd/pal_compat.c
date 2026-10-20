//////////////////////////////////////////////////////////
// pal_compat.c —— Win32 API 的 POSIX 实现
//////////////////////////////////////////////////////////
#include "headers.h"

static HANDLE PalMake (int tag) { HANDLE h = calloc (1, sizeof *h); h->tag = tag; return h; }

HANDLE CreateFile (LPCSTR szName, DWORD access, DWORD share, LPVOID sa,
                   DWORD disp, DWORD flags, HANDLE hTemplate)
{
int fl = 0;
    if (access & GENERIC_READ)   fl = O_RDONLY;
    if (access & GENERIC_WRITE)  fl = (access & GENERIC_READ) ? O_RDWR : O_WRONLY;
    switch (disp)
    {
        case CREATE_NEW        : fl |= O_CREAT | O_EXCL;  break;
        case CREATE_ALWAYS     : fl |= O_CREAT | O_TRUNC; break;
        case OPEN_ALWAYS       : fl |= O_CREAT;           break;
        case TRUNCATE_EXISTING : fl |= O_TRUNC;           break;
        default : break;
    }
    int fd = open (szName, fl, 0644);
    if (fd < 0)  return INVALID_HANDLE_VALUE;
    HANDLE h = PalMake (PAL_TAG_FILE);
    h->fd = fd;
return h;
}

BOOL ReadFile (HANDLE h, LPVOID buf, DWORD nToRead, DWORD *pRead, LPVOID ov)
{ *pRead = (DWORD) read (h->fd, buf, nToRead);  return *pRead != (DWORD)-1; }

BOOL WriteFile (HANDLE h, LPCVOID buf, DWORD nToWrite, DWORD *pWritten, LPVOID ov)
{ *pWritten = (DWORD) write (h->fd, buf, nToWrite);  return *pWritten != (DWORD)-1; }

BOOL SetFilePointerEx (HANDLE h, LARGE_INTEGER liDist, PLARGE_INTEGER pNew, DWORD whence)
{ off_t rc = lseek (h->fd, (off_t) liDist.QuadPart, (int) whence);
  if (pNew != NULL)  pNew->QuadPart = (__int64) rc;
return rc != (off_t)-1; }

BOOL GetFileSizeEx (HANDLE h, PLARGE_INTEGER pSize)
{ struct stat st;  if (fstat (h->fd, & st) != 0)  return FALSE;
  pSize->QuadPart = (__int64) st.st_size;  return TRUE; }

BOOL CloseHandle (HANDLE h)
{
    if (h == NULL || h == INVALID_HANDLE_VALUE)  return FALSE;
    switch (h->tag)
    {
        case PAL_TAG_FILE    : close (h->fd);  break;
        case PAL_TAG_PIPEEV  : close (h->fd); close (h->rfd); break;
        case PAL_TAG_EVENT   : pthread_mutex_destroy (& h->mx); pthread_cond_destroy (& h->cv); break;
        case PAL_TAG_THREAD  : /* 不 join，仅丢弃 */ break;
    }
    free (h);
return TRUE;
}

BOOL GetFileAttributesEx (LPCSTR szFile, DWORD level, LPVOID pBuf)
{
struct stat st;
WIN32_FILE_ATTRIBUTE_DATA *fad = (WIN32_FILE_ATTRIBUTE_DATA *) pBuf;
    if (stat (szFile, & st) != 0)  return FALSE;
    fad->dwFileAttributes = (st.st_mode & S_IFDIR) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
    fad->nFileSizeHigh = (DWORD) ((unsigned long long) st.st_size >> 32);
    fad->nFileSizeLow  = (DWORD) (st.st_size & 0xFFFFFFFFu);
    fad->ftLastWriteTime.dwLowDateTime  = (DWORD) st.st_mtime;
    fad->ftLastWriteTime.dwHighDateTime = 0;
return TRUE;
}

HANDLE CreateEvent (LPVOID sa, BOOL manual, BOOL init, LPCSTR name)
{
    HANDLE h = PalMake (PAL_TAG_EVENT);
    h->manual = manual;  h->state = init;
    pthread_mutex_init (& h->mx, NULL);
    pthread_cond_init  (& h->cv, NULL);
return h;
}

BOOL SetEvent (HANDLE h)
{
    if (h->tag == PAL_TAG_PIPEEV)
    { char c = 'W';  write (h->fd, & c, 1);  return TRUE; }
    pthread_mutex_lock (& h->mx);
    h->state = 1;
    if (h->manual)  pthread_cond_broadcast (& h->cv);
    else            pthread_cond_signal   (& h->cv);
    pthread_mutex_unlock (& h->mx);
return TRUE;
}

BOOL ResetEvent (HANDLE h)
{
    if (h->tag == PAL_TAG_PIPEEV)
    { char buf [64];  while (read (h->rfd, buf, sizeof buf) > 0) ;  return TRUE; }
    pthread_mutex_lock (& h->mx);  h->state = 0;  pthread_mutex_unlock (& h->mx);
return TRUE;
}

HANDLE PalCreatePipeEvent (void)
{
int p[2];
    if (pipe (p) != 0)  return INVALID_HANDLE_VALUE;
    /* HarmonyOS/POSIX 适配：必须非阻塞。主循环 poll 之后用
       "while (read(...) > 0)" 清空管道，阻塞模式下最后一次 read 会永久挂起
       TFTP 监听线程（并发传输创建/销毁临时线程时必现）。 */
    fcntl (p[0], F_SETFL, fcntl (p[0], F_GETFL, 0) | O_NONBLOCK);
    fcntl (p[1], F_SETFL, fcntl (p[1], F_GETFL, 0) | O_NONBLOCK);
    HANDLE h = PalMake (PAL_TAG_PIPEEV);
    h->fd = p[1];   /* 写端：SetEvent 用 */
    h->rfd = p[0];  /* 读端：tftpd_main poll 用 */
return h;
}

int PalWakeFd (HANDLE h) { return h->rfd; }

DWORD WaitForSingleObject (HANDLE h, DWORD ms)
{
    if (h->tag == PAL_TAG_THREAD)
    {  /* 关停路径专用：忽略超时（musl 无 timedjoin），线程必已/即将退出 */
       if (h->th_valid)  { pthread_join (h->th, NULL);  h->th_valid = 0; }
       return 0;
    }
    if (h->tag == PAL_TAG_MUTEX)
    { pthread_mutex_lock (& h->mx);  return WAIT_OBJECT_0; }
    pthread_mutex_lock (& h->mx);
    while (! h->state)
    {
        if (ms == INFINITE)  pthread_cond_wait (& h->cv, & h->mx);
        else
        {
            struct timespec ts;  clock_gettime (CLOCK_REALTIME, & ts);
            ts.tv_sec += ms / 1000;  ts.tv_nsec += (long)(ms % 1000) * 1000000L;
            if (ts.tv_nsec >= 1000000000L)  { ts.tv_sec ++;  ts.tv_nsec -= 1000000000L; }
            if (pthread_cond_timedwait (& h->cv, & h->mx, & ts) != 0)
            { pthread_mutex_unlock (& h->mx);  return 0x102 /*WAIT_TIMEOUT*/; }
        }
    }
    if (! h->manual)  h->state = 0;   /* auto-reset */
    pthread_mutex_unlock (& h->mx);
return 0;
}

HANDLE CreateThread (LPVOID sa, DWORD stack, DWORD (WINAPI *fn)(LPVOID), LPVOID arg, DWORD fl, DWORD *pid)
{
    HANDLE h = PalMake (PAL_TAG_THREAD);
    /* 原生线程函数为 DWORD WINAPI f(LPVOID)；经函数指针转型（musl ABI 下安全） */
    if (pthread_create (& h->th, NULL, (void * (*)(void *)) fn, arg) == 0)
    { h->th_valid = 1;  if (pid != NULL)  *pid = (DWORD) h->th; }
    else { free (h);  return INVALID_HANDLE_VALUE; }
return h;
}

DWORD SuspendThread (HANDLE h) { return 0; }   /* 仅 TftpdCleanup 关停路径使用（该函数当前无调用点） */

BOOL TerminateThread (HANDLE h, DWORD code)
{
    /* 仅 TftpdCleanup 关停路径使用（当前无调用点）；鸿蒙 musl 无 pthread_cancel，置空操作 */
    return TRUE;
}

void PalFreeThreadHandle (HANDLE h)
{
    if (h != NULL && h != INVALID_HANDLE_VALUE && h->tag == PAL_TAG_THREAD)
    {  if (h->th_valid)  { pthread_detach (h->th);  h->th_valid = 0; }  free (h); }
}

HANDLE CreateMutex (LPVOID sa, BOOL initOwner, LPCSTR name)
{
    HANDLE h = PalMake (PAL_TAG_MUTEX);
    pthread_mutex_init (& h->mx, NULL);
    /* initOwner 语义未用：linked_list.c 恒传 0 */
return h;
}

BOOL ReleaseMutex (HANDLE h)
{ pthread_mutex_unlock (& h->mx);  return TRUE; }

void Sleep (DWORD ms) { usleep (ms * 1000u); }
