// libstdc++'s thread layer (gthreads) on the PS3's lv2 kernel through PSL1GHT. The toolchain's libstdc++ is built
// without threads; ps3/build.sh puts this folder first on the include path and defines _GLIBCXX_HAS_GTHREADS, so
// std::mutex, std::recursive_mutex, std::condition_variable and std::thread come from these, and ps3/gthr.cpp
// supplies the parts libstdc++ keeps out of line.
//   mutex, recursive mutex: an int, as the library's own single threaded build has it (its classes hold mutexes,
//     basic_filebuf for one, so the size must not change): the number of a slot in a table of lv2 lightweight
//     mutexes (ps3/gthr.cpp), taken on first use (a mutex can be used before its static constructor runs; a
//     zeroed one is valid)
//   condition: a count of waiters and an lv2 semaphore (wakeups may be spurious, as the standard allows)
//   thread: a joinable PPU thread
#ifndef PS3_GTHR_DEFAULT_H
#define PS3_GTHR_DEFAULT_H

#define __GTHREADS 1
#define __GTHREADS_CXX0X 1

#include <stdint.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>

#include <ppu-lv2.h>
#include <lv2/mutex.h>
#include <sys/sem.h>
#include <sys/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef sys_ppu_thread_t __gthread_t;
typedef int __gthread_mutex_t;  // 0 (recursive: -1) not made yet, -2 being made, n > 0 slot n - 1
typedef int __gthread_recursive_mutex_t;
typedef struct {
    volatile int state;  // 0, 1 running, 2 done
} __gthread_once_t;
typedef struct {
    volatile uint32_t waiters;
    volatile int state;
    sys_sem_t sem;
} __gthread_cond_t;
typedef unsigned __gthread_key_t;
typedef struct timespec __gthread_time_t;

#define __GTHREAD_MUTEX_INIT_FUNCTION(m) __gthread_ps3_mutex_init((m), 0)
#define __GTHREAD_RECURSIVE_MUTEX_INIT_FUNCTION(m) __gthread_ps3_mutex_init((m), 1)
#define __GTHREAD_COND_INIT_FUNCTION(c) __gthread_ps3_cond_init(c)
#define __GTHREAD_ONCE_INIT {0}
#define __GTHREAD_TIME_INIT {0, 0}

// ps3/gthr.cpp
extern uint64_t __gthread_ps3_stack_size;  // bytes for threads std::thread makes
void* __gthread_ps3_getspecific(__gthread_key_t key);
int __gthread_ps3_setspecific(__gthread_key_t key, const void* p);
int __gthread_ps3_key_create(__gthread_key_t* key, void (*dtor)(void*));
int __gthread_ps3_create(__gthread_t* t, void* (*fn)(void*), void* arg);
sys_lwmutex_t* __gthread_ps3_mutex(__gthread_mutex_t* m);  // the mutex's slot, made on first use
void __gthread_ps3_mutex_free(__gthread_mutex_t* m);

static inline int __gthread_active_p(void) { return 1; }

static inline void __gthread_ps3_mutex_init(__gthread_mutex_t* m, int recursive) { *m = recursive ? -1 : 0; }
static inline int __gthread_mutex_lock(__gthread_mutex_t* m) { return sysLwMutexLock(__gthread_ps3_mutex(m), 0); }
static inline int __gthread_mutex_trylock(__gthread_mutex_t* m) { return sysLwMutexTryLock(__gthread_ps3_mutex(m)); }
static inline int __gthread_mutex_unlock(__gthread_mutex_t* m) { return sysLwMutexUnlock(__gthread_ps3_mutex(m)); }
static inline int __gthread_mutex_destroy(__gthread_mutex_t* m) {
    __gthread_ps3_mutex_free(m);
    return 0;
}
// microseconds from now to an absolute system_clock time, at least 1, 0 when it has passed
static inline uint64_t __gthread_ps3_until(const __gthread_time_t* abs) {
    struct timeval now;
    gettimeofday(&now, 0);
    int64_t us = (int64_t)(abs->tv_sec - now.tv_sec) * 1000000 + (abs->tv_nsec / 1000 - now.tv_usec);
    return us <= 0 ? 0 : (uint64_t)us;
}
static inline int __gthread_mutex_timedlock(__gthread_mutex_t* m, const __gthread_time_t* abs) {
    uint64_t us = __gthread_ps3_until(abs);
    return us ? sysLwMutexLock(__gthread_ps3_mutex(m), us) : sysLwMutexTryLock(__gthread_ps3_mutex(m));
}
static inline int __gthread_recursive_mutex_lock(__gthread_recursive_mutex_t* m) { return __gthread_mutex_lock(m); }
static inline int __gthread_recursive_mutex_trylock(__gthread_recursive_mutex_t* m) { return __gthread_mutex_trylock(m); }
static inline int __gthread_recursive_mutex_unlock(__gthread_recursive_mutex_t* m) { return __gthread_mutex_unlock(m); }
static inline int __gthread_recursive_mutex_destroy(__gthread_recursive_mutex_t* m) { return __gthread_mutex_destroy(m); }
static inline int __gthread_recursive_mutex_timedlock(__gthread_recursive_mutex_t* m, const __gthread_time_t* abs) {
    return __gthread_mutex_timedlock(m, abs);
}

static inline void __gthread_ps3_cond_init(__gthread_cond_t* c) {
    c->waiters = 0;
    c->state = 0;
}
static inline void __gthread_ps3_cond_ready(__gthread_cond_t* c) {
    if (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) == 2) return;
    int expect = 0;
    if (__atomic_compare_exchange_n(&c->state, &expect, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        sys_sem_attr_t a;
        a.attr_protocol = 2;       // priority order
        a.attr_pshared = 0x200;    // this process only
        a.key = 0;
        a.flags = 0;
        a.pad = 0;
        a.name[0] = 'c', a.name[1] = 'v', a.name[2] = 0;
        sysSemCreate(&c->sem, &a, 0, 0x7FFFFFFF);
        __atomic_store_n(&c->state, 2, __ATOMIC_RELEASE);
        return;
    }
    while (__atomic_load_n(&c->state, __ATOMIC_ACQUIRE) != 2) sysThreadYield();
}
static inline int __gthread_cond_signal(__gthread_cond_t* c) {
    __gthread_ps3_cond_ready(c);
    uint32_t w = __atomic_load_n(&c->waiters, __ATOMIC_ACQUIRE);
    while (w && !__atomic_compare_exchange_n(&c->waiters, &w, w - 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
    }
    if (w) sysSemPost(c->sem, 1);
    return 0;
}
static inline int __gthread_cond_broadcast(__gthread_cond_t* c) {
    __gthread_ps3_cond_ready(c);
    uint32_t w = __atomic_exchange_n(&c->waiters, 0, __ATOMIC_ACQ_REL);
    if (w) sysSemPost(c->sem, (int32_t)w);
    return 0;
}
// gave up waiting: take ourselves off the count, or, when a signal already counted us, take its post
static inline void __gthread_ps3_cond_leave(__gthread_cond_t* c) {
    uint32_t w = __atomic_load_n(&c->waiters, __ATOMIC_ACQUIRE);
    for (;;) {
        if (!w) {
            sysSemWait(c->sem, 0);
            return;
        }
        if (__atomic_compare_exchange_n(&c->waiters, &w, w - 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return;
    }
}
static inline int __gthread_cond_wait(__gthread_cond_t* c, __gthread_mutex_t* m) {
    __gthread_ps3_cond_ready(c);
    __atomic_add_fetch(&c->waiters, 1, __ATOMIC_ACQ_REL);
    __gthread_mutex_unlock(m);
    sysSemWait(c->sem, 0);
    return __gthread_mutex_lock(m);
}
static inline int __gthread_cond_timedwait(__gthread_cond_t* c, __gthread_mutex_t* m, const __gthread_time_t* abs) {
    __gthread_ps3_cond_ready(c);
    __atomic_add_fetch(&c->waiters, 1, __ATOMIC_ACQ_REL);
    __gthread_mutex_unlock(m);
    uint64_t us = __gthread_ps3_until(abs);
    int r = us ? sysSemWait(c->sem, us) : sysSemTryWait(c->sem);
    if (r) __gthread_ps3_cond_leave(c);
    __gthread_mutex_lock(m);
    return r ? 1 : 0;
}
static inline int __gthread_cond_wait_recursive(__gthread_cond_t* c, __gthread_recursive_mutex_t* m) {
    return __gthread_cond_wait(c, m);
}
static inline int __gthread_cond_destroy(__gthread_cond_t* c) {
    if (c->state == 2) sysSemDestroy(c->sem);
    c->state = 0;
    return 0;
}

static inline int __gthread_create(__gthread_t* t, void* (*fn)(void*), void* arg) { return __gthread_ps3_create(t, fn, arg); }
static inline int __gthread_join(__gthread_t t, void** value) {
    uint64_t rv = 0;
    int r = sysThreadJoin(t, &rv);
    if (value) *value = (void*)(uintptr_t)rv;
    return r;
}
static inline int __gthread_detach(__gthread_t t) { return sysThreadDetach(t); }
static inline int __gthread_equal(__gthread_t a, __gthread_t b) { return a == b; }
static inline __gthread_t __gthread_self(void) {
    sys_ppu_thread_t id = 0;
    sysThreadGetId(&id);
    return id;
}
static inline int __gthread_yield(void) { return sysThreadYield(); }

static inline int __gthread_once(__gthread_once_t* o, void (*fn)(void)) {
    if (__atomic_load_n(&o->state, __ATOMIC_ACQUIRE) == 2) return 0;
    int expect = 0;
    if (__atomic_compare_exchange_n(&o->state, &expect, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        fn();
        __atomic_store_n(&o->state, 2, __ATOMIC_RELEASE);
        return 0;
    }
    while (__atomic_load_n(&o->state, __ATOMIC_ACQUIRE) != 2) sysThreadYield();
    return 0;
}

static inline int __gthread_key_create(__gthread_key_t* key, void (*dtor)(void*)) { return __gthread_ps3_key_create(key, dtor); }
static inline int __gthread_key_delete(__gthread_key_t key) {
    (void)key;
    return 0;
}
static inline void* __gthread_getspecific(__gthread_key_t key) { return __gthread_ps3_getspecific(key); }
static inline int __gthread_setspecific(__gthread_key_t key, const void* p) { return __gthread_ps3_setspecific(key, p); }

#ifdef __cplusplus
}
#endif

#endif
