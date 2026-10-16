cmake_minimum_required(VERSION 3.5.1)
project(Tftpd32)
set(CMAKE_C_STANDARD 11)
# -fcommon 必须：threading.h 中 tThreads[] 为公共符号（原工程 C89 语义）
add_compile_options(-fcommon -O2 -Wall -Wno-format -Wno-pointer-sign -Wno-unused-result)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tftpd ${CMAKE_CURRENT_SOURCE_DIR}/tftpd/libs)
add_library(tftpd32 SHARED
    napi_init.cpp
    tftpd/tftpd_main.c tftpd/tftpd_thread.c tftpd/tftpd_util.c tftpd/tftp_dbg.c
    tftpd/pal_compat.c tftpd/settings_pal.c tftpd/console_pal.c tftpd/async_log_pal.c
    tftpd/service_main.c tftpd/ip_pal.c tftpd/eventlog_pal.c
    tftpd/libs/linked_list/linked_list.c tftpd/libs/md5/md5.c tftpd/libs/lasterr/lasterr.c)
target_link_libraries(tftpd32 PUBLIC libace_napi.z.so)
