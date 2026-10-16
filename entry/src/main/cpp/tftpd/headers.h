//////////////////////////////////////////////////////////
// Tftpd32 on HarmonyOS —— 聚合头（替代原 _common/headers.h）
// 被 tftpd_main.c / tftpd_thread.c / tftpd_util.c / tftp_dbg.c 原样 include
//////////////////////////////////////////////////////////
#ifndef PAL_HEADERS_H
#define PAL_HEADERS_H

#include "pal_compat.h"          /* 全部 Win32 兼容定义，必须在最前 */

#include "_hdr_settings.h"       /* 拷贝件改名，避免与目录内同名冲突 */
#include "_hdr_Tftp.h"
#include "_hdr_tftp_struct.h"
#include "_hdr_dialog_socket.h"
#include "_hdr_custom.h"

#include "threading.h"           /* 内含 async_log.h */
#include "lasterr.h"             /* libs/lasterr/lasterr.h（include_directories 已指到 libs/） */
#include "md5.h"                 /* libs/md5/md5.h */

#include "LogToMonitor.h"        /* console_pal.c 提供实现 */

#endif
