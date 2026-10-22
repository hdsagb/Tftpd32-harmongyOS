cmake_minimum_required(VERSION 3.15)
project(tftpd32_host_tests C)

# 宿主（PC）侧单元测试：直接把生产源码编进测试可执行文件，
# 不参与 HAP 构建，也不链接任何鸿蒙 SDK 库。
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(TFTPD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../tftpd)

add_executable(tftpd32_host_tests
    test_main.c
    test_dhcp_option.c
    test_ip_util.c
    test_lease.c
    test_settings.c
    host_stubs.c
    ${TFTPD_DIR}/bootpd_util.c
    ${TFTPD_DIR}/ip_pal.c
    ${TFTPD_DIR}/settings_pal.c
)

# compat 必须排在前面：Windows 没有 sys/socket.h、pthread.h 等 POSIX 头
target_include_directories(tftpd32_host_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
    ${TFTPD_DIR}
    ${TFTPD_DIR}/libs
    ${TFTPD_DIR}/libs/md5
    ${TFTPD_DIR}/libs/linked_list
    ${TFTPD_DIR}/libs/lasterr
)

if(MSVC)
    # /Gy + /OPT:REF：丢弃未被测试触达的函数，避免为整个移植层写桩
    # /utf-8：源码为 UTF-8（含中文注释），避免 C4819
    target_compile_options(tftpd32_host_tests PRIVATE
        /W3 /Gy /utf-8 /FI${CMAKE_CURRENT_SOURCE_DIR}/host_prelude.h)
    target_link_options(tftpd32_host_tests PRIVATE /OPT:REF)
else()
    target_compile_options(tftpd32_host_tests PRIVATE
        -fcommon -ffunction-sections -fdata-sections -Wall -Wno-pointer-sign -Wno-unused-result)
    target_link_options(tftpd32_host_tests PRIVATE -Wl,--gc-sections)
endif()

enable_testing()
add_test(NAME tftpd32_host_tests COMMAND tftpd32_host_tests)
