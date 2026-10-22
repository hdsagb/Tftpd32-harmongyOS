cmake_minimum_required(VERSION 3.5.1)
project(Tftpd32)
set(CMAKE_C_STANDARD 11)
# -fcommon 必须：threading.h 中 tThreads[] 为公共符号（原工程 C89 语义）
add_compile_options(-fcommon -O2 -Wall -Wno-format -Wno-pointer-sign -Wno-unused-result)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/tftpd
                    ${CMAKE_CURRENT_SOURCE_DIR}/tftpd/libs
                    ${CMAKE_CURRENT_SOURCE_DIR}/tftpd/libs/md5
                    ${CMAKE_CURRENT_SOURCE_DIR}/tftpd/libs/linked_list
                    ${CMAKE_CURRENT_SOURCE_DIR}/tftpd/libs/lasterr)
add_library(tftpd32 SHARED
    napi_init.cpp
    tftpd/tftpd_main.c tftpd/tftpd_thread.c tftpd/tftpd_util.c tftpd/tftp_dbg.c
    tftpd/pal_compat.c tftpd/settings_pal.c tftpd/console_pal.c tftpd/async_log_pal.c
    tftpd/service_main.c tftpd/ip_pal.c tftpd/eventlog_pal.c tftpd/syslogd.c
    tftpd/sntpd.c tftpd/dns.c tftpd/dns_hosts.c tftpd/tftp_client.c tftpd/file_tool_pal.c
    tftpd/registry_pal.c tftpd/bootpd.c tftpd/bootpd_util.c
    tftpd/libs/linked_list/linked_list.c tftpd/libs/md5/md5.c tftpd/libs/lasterr/lasterr.c)
target_link_libraries(tftpd32 PUBLIC libace_napi.z.so)
# 静态链接 libc++（本库仅 napi_init 用到 C++），配合 build-profile 排除 libc++_shared.so 以减小 HAP
target_link_options(tftpd32 PRIVATE -static-libstdc++)
# D1：release 构建去除符号表（-O2 已在 add_compile_options 中全局开启）
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(tftpd32 PRIVATE -s)
endif()
