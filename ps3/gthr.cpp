// The out of line parts of libstdc++'s threads (its library is built without them) on PSL1GHT, for the gthreads of
// ps3/compat/bits/gthr-default.h; and the few C library calls newlib lacks that libstdc++ links against.
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>

#include <sys/stat.h>
#include <sys/thread.h>
#include <unistd.h>

#include <lv2/mutex.h>

extern "C" {

uint64_t __gthread_ps3_stack_size = 1u << 20;

// keys: a fixed table per thread (libstdc++ needs few)
namespace {
constexpr unsigned kKeys = 64;
__thread void* t_keys[kKeys];
std::atomic<unsigned> g_next_key{1};

struct Start {
    void* (*fn)(void*);
    void* arg;
};
void Entry(void* p) {
    Start s = *static_cast<Start*>(p);
    delete static_cast<Start*>(p);
    void* r = s.fn(s.arg);
    sysThreadExit(uint64_t(uintptr_t(r)));
}
}  // namespace

int __gthread_ps3_key_create(__gthread_key_t* key, void (*)(void*)) {
    unsigned k = g_next_key.fetch_add(1);
    if (k >= kKeys) return EAGAIN;
    *key = k;
    return 0;
}
void* __gthread_ps3_getspecific(__gthread_key_t key) { return key < kKeys ? t_keys[key] : nullptr; }
int __gthread_ps3_setspecific(__gthread_key_t key, const void* p) {
    if (key >= kKeys) return EINVAL;
    t_keys[key] = const_cast<void*>(p);
    return 0;
}

// the mutex table: slots taken on first use, given back when a mutex is destroyed
namespace {
constexpr int kSlots = 8192;
sys_lwmutex_t g_slots[kSlots];
int g_free[kSlots];
int g_free_count = 0, g_next_slot = 0;
volatile int g_table_lock = 0;
void TableLock() {
    while (__atomic_exchange_n(&g_table_lock, 1, __ATOMIC_ACQUIRE)) sysThreadYield();
}
void TableUnlock() { __atomic_store_n(&g_table_lock, 0, __ATOMIC_RELEASE); }
}  // namespace

sys_lwmutex_t* __gthread_ps3_mutex(__gthread_mutex_t* m) {
    int v = __atomic_load_n(m, __ATOMIC_ACQUIRE);
    while (v <= 0) {
        if (v == -2) {  // another thread is making it
            sysThreadYield();
            v = __atomic_load_n(m, __ATOMIC_ACQUIRE);
            continue;
        }
        if (!__atomic_compare_exchange_n(m, &v, -2, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) continue;
        bool recursive = v == -1;
        TableLock();
        int slot = g_free_count ? g_free[--g_free_count] : g_next_slot < kSlots ? g_next_slot++ : -1;
        TableUnlock();
        if (slot < 0) {
            static const char msg[] = "gthr: out of mutex slots\n";
            write(2, msg, sizeof msg - 1);
            abort();
        }
        sys_lwmutex_attr_t a;
        a.attr_protocol = SYS_LWMUTEX_PROTOCOL_PRIO;
        a.attr_recursive = recursive ? SYS_LWMUTEX_ATTR_RECURSIVE : SYS_LWMUTEX_ATTR_NOT_RECURSIVE;
        a.name[0] = 's', a.name[1] = 't', a.name[2] = 'd', a.name[3] = 0;
        sysLwMutexCreate(&g_slots[slot], &a);
        v = slot + 1;
        __atomic_store_n(m, v, __ATOMIC_RELEASE);
    }
    return &g_slots[v - 1];
}

void __gthread_ps3_mutex_free(__gthread_mutex_t* m) {
    int v = *m;
    if (v <= 0) return;
    sysLwMutexDestroy(&g_slots[v - 1]);
    *m = 0;
    TableLock();
    g_free[g_free_count++] = v - 1;
    TableUnlock();
}

int __gthread_ps3_create(__gthread_t* t, void* (*fn)(void*), void* arg) {
    auto* s = new Start{fn, arg};
    int rc = sysThreadCreate(t, Entry, s, 1000, __gthread_ps3_stack_size, THREAD_JOINABLE, const_cast<char*>("std"));
    if (rc) delete s;
    return rc ? EAGAIN : 0;
}

// newlib has none of these; libstdc++'s filesystem links against them
int symlink(const char*, const char*) {
    errno = ENOSYS;
    return -1;
}
ssize_t readlink(const char*, char*, size_t) {
    errno = EINVAL;
    return -1;
}
int fchmod(int, mode_t) { return 0; }
long pathconf(const char*, int) { return 1024; }

// newlib's remove only unlinks files; std::filesystem::remove and remove_all need folders too
int rmdir(const char* path);
int remove(const char* path) {
    struct stat st;
    if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return rmdir(path);
    return unlink(path);
}

// pread, which newlib lacks: a seek and a read under one lock (the disc image is read from several threads)
ssize_t pread(int fd, void* buf, size_t n, off_t at) {
    static std::mutex lock;
    std::lock_guard<std::mutex> g(lock);
    if (lseek(fd, at, SEEK_SET) == off_t(-1)) return -1;
    return read(fd, buf, n);
}

// a relaunch into another image (kernel::Relaunch) has nothing to start on the PS3
int execl(const char*, const char*, ...) {
    errno = ENOSYS;
    return -1;
}

}  // extern "C"

namespace std {

// call_once (with _GLIBCXX_HAVE_TLS)
__thread void* __once_callable;
__thread void (*__once_call)();
extern "C" void __once_proxy() {
    if (__once_call) __once_call();
}

// condition_variable: the __condvar inside does the work
condition_variable::condition_variable() noexcept = default;
condition_variable::~condition_variable() noexcept = default;
void condition_variable::wait(unique_lock<mutex>& lock) { _M_cond.wait(*lock.mutex()); }
void condition_variable::notify_one() noexcept { _M_cond.notify_one(); }
void condition_variable::notify_all() noexcept { _M_cond.notify_all(); }

// thread
thread::_State::~_State() = default;

void thread::_M_start_thread(_State_ptr state, void (*)()) {
    void* (*run)(void*) = [](void* p) -> void* {
        unique_ptr<_State> s(static_cast<_State*>(p));
        s->_M_run();
        return nullptr;
    };
    if (int e = __gthread_create(&_M_id._M_thread, run, state.get())) __throw_system_error(e);
    state.release();
}
void thread::join() {
    if (!joinable()) __throw_system_error(EINVAL);
    if (int e = __gthread_join(_M_id._M_thread, nullptr)) __throw_system_error(e);
    _M_id = id();
}
void thread::detach() {
    if (!joinable()) __throw_system_error(EINVAL);
    if (int e = __gthread_detach(_M_id._M_thread)) __throw_system_error(e);
    _M_id = id();
}
unsigned thread::hardware_concurrency() noexcept { return 2; }  // the PPU's two hardware threads

namespace this_thread {
void __sleep_for(chrono::seconds s, chrono::nanoseconds ns) {
    uint64_t us = uint64_t(s.count()) * 1000000 + uint64_t(ns.count()) / 1000;
    while (us) {
        uint32_t step = us > 1000000000u ? 1000000000u : uint32_t(us);
        usleep(step);
        us -= step;
    }
}
}  // namespace this_thread

}  // namespace std

// a failed allocation says how big it was and who asked, on the TTY, before bad_alloc ends the program
#include <new>
void* operator new(std::size_t n) {
    if (void* p = std::malloc(n ? n : 1)) return p;
    std::fprintf(stderr, "ps3: out of memory: %lu bytes for %p\n", (unsigned long)n, __builtin_return_address(0));
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
