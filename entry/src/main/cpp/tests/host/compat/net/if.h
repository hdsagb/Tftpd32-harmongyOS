#pragma once
/* 宿主（Windows/MSVC）上的 <net/if.h> 替身：仅提供被测源码用到的标志位 */
#define IFF_UP          0x1
#define IFF_LOOPBACK    0x8
#define IFF_RUNNING     0x40
