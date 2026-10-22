#pragma once
/* 宿主（Windows/MSVC）上的 <dirent.h> 替身：仅供编译，目录遍历不在被测范围 */
#define NAME_MAX 255

struct dirent
{
    long  d_ino;
    char  d_name[NAME_MAX + 1];
};

typedef struct HOST_DIR DIR;

#ifdef __cplusplus
extern "C" {
#endif
DIR *opendir (const char *name);
struct dirent *readdir (DIR *d);
int closedir (DIR *d);
#ifdef __cplusplus
}
#endif
