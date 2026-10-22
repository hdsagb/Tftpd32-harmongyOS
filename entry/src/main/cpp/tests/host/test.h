#pragma once
/* 极简断言宏：不引入外部框架，失败时打印文件/行/表达式 */
#include <stdio.h>
#include <string.h>

extern int g_pass, g_fail;

#define CHECK_TRUE(expr) \
    do { if (expr) { g_pass++; } \
         else { g_fail++; fprintf (stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #expr); } } while (0)

#define CHECK_EQ_INT(a, b) \
    do { long long _a = (long long)(a), _b = (long long)(b); \
         if (_a == _b) { g_pass++; } \
         else { g_fail++; fprintf (stderr, "  FAIL %s:%d  %s == %s (%lld != %lld)\n", \
                                   __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)

#define CHECK_EQ_MEM(p, exp, n) \
    do { if (memcmp ((p), (exp), (size_t)(n)) == 0) { g_pass++; } \
         else { g_fail++; fprintf (stderr, "  FAIL %s:%d  memcmp %s\n", __FILE__, __LINE__, #p); } } while (0)

#define CHECK_STR(p, exp) \
    do { const char *_p = (p), *_e = (exp); \
         if (strcmp (_p, _e) == 0) { g_pass++; } \
         else { g_fail++; fprintf (stderr, "  FAIL %s:%d  %s: \"%s\" != \"%s\"\n", \
                                   __FILE__, __LINE__, #p, _p, _e); } } while (0)

#define TEST_CASE_BEGIN(name)  fprintf (stderr, "- %s\n", name)
#define TEST_SUITE(name)       fprintf (stderr, "[suite] %s\n", name)
