#pragma once
/* 宿主（Windows/MSVC）上的 <pthread.h> 替身
   本测试只链接纯逻辑单元，线程相关函数不会被调用；此处仅满足类型与声明。
   禁止把 Windows 的 HANDLE 等定义带进被测源码，因此全部用空结构体。 */
#include <stddef.h>

typedef unsigned long long pthread_t;
typedef struct { int opaque; } pthread_mutex_t;
typedef struct { int opaque; } pthread_mutexattr_t;
typedef struct { int opaque; } pthread_cond_t;
typedef struct { int opaque; } pthread_condattr_t;
typedef struct { int opaque; } pthread_attr_t;
typedef int pthread_key_t;
typedef unsigned int pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER  { 0 }
#define PTHREAD_COND_INITIALIZER   { 0 }
#define PTHREAD_ONCE_INIT          0

#ifdef __cplusplus
extern "C" {
#endif

int pthread_mutex_init (pthread_mutex_t *, const pthread_mutexattr_t *);
int pthread_mutex_destroy (pthread_mutex_t *);
int pthread_mutex_lock (pthread_mutex_t *);
int pthread_mutex_unlock (pthread_mutex_t *);
int pthread_cond_init (pthread_cond_t *, const pthread_condattr_t *);
int pthread_cond_destroy (pthread_cond_t *);
int pthread_cond_wait (pthread_cond_t *, pthread_mutex_t *);
int pthread_cond_timedwait (pthread_cond_t *, pthread_mutex_t *, const struct timespec *);
int pthread_cond_signal (pthread_cond_t *);
int pthread_cond_broadcast (pthread_cond_t *);
int pthread_create (pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
int pthread_join (pthread_t, void **);
int pthread_detach (pthread_t);
pthread_t pthread_self (void);
void pthread_exit (void *);

#ifdef __cplusplus
}
#endif
