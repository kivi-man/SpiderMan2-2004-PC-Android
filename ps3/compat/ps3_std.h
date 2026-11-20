// Included first in every PS3 source (ps3/build.sh, -include). The toolchain's libstdc++ (GCC 13.2 over newlib 1.20)
// is built without its C99 parts: these put the C library's functions into std, as libstdc++ does elsewhere.
// newlib's printf has no z, t or j length (size_t, ptrdiff_t, intmax_t): std::snprintf and std::vsnprintf here
// rewrite them to l, the same width on the 64 bit PPU.
#pragma once
#ifdef __cplusplus
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <type_traits>

namespace ps3 {
// fmt with %z, %t and %j turned into %l, in buf when it needs a change
inline const char* PrintfFormat(const char* fmt, char* buf, size_t size) {
    const char* f = fmt;
    bool any = false;
    for (; *f; f++)
        if (*f == '%') {
            const char* q = f + 1;
            while (*q && std::strchr("-+ #0123456789.*'", *q)) q++;
            if (*q == 'z' || *q == 't' || *q == 'j') any = true;
            if (*q == '%') f = q;
        }
    if (!any || size_t(f - fmt) >= size) return fmt;
    size_t n = 0;
    for (f = fmt; *f; f++) {
        buf[n++] = *f;
        if (*f != '%') continue;
        const char* q = f + 1;
        while (*q && std::strchr("-+ #0123456789.*'", *q)) buf[n++] = *q++;
        if (*q == 'z' || *q == 't' || *q == 'j') {
            buf[n++] = 'l';
            q++;
        }
        f = q - 1;
    }
    buf[n] = 0;
    return buf;
}
}  // namespace ps3

namespace std {
// math (C99, TR1)
#define PS3_MATH1(n)                                    \
    using ::n;                                          \
    inline float n(float x) { return ::n##f(x); }
#define PS3_MATH2(n)                                    \
    using ::n;                                          \
    inline float n(float x, float y) { return ::n##f(x, y); }
#define PS3_MATHL(n)                                    \
    using ::n;                                          \
    inline long n(float x) { return ::n##f(x); }
PS3_MATH1(nearbyint)
PS3_MATH1(trunc)
PS3_MATH1(round)
PS3_MATH1(rint)
PS3_MATH1(log2)
PS3_MATH1(exp2)
PS3_MATH1(cbrt)
PS3_MATH1(expm1)
PS3_MATH1(log1p)
PS3_MATH1(asinh)
PS3_MATH1(acosh)
PS3_MATH1(atanh)
PS3_MATH2(fmin)
PS3_MATH2(fmax)
PS3_MATH2(fdim)
PS3_MATH2(hypot)
PS3_MATH2(copysign)
PS3_MATH2(remainder)
PS3_MATHL(lround)
PS3_MATHL(lrint)
using ::llround;
using ::llrint;
using ::fma;
inline float fma(float x, float y, float z) { return ::fmaf(x, y, z); }
#undef PS3_MATH1
#undef PS3_MATH2
#undef PS3_MATHL

// stdio
inline int vsnprintf(char* s, size_t n, const char* fmt, va_list ap) {
    char f[512];
    return ::vsnprintf(s, n, ps3::PrintfFormat(fmt, f, sizeof f), ap);
}
inline int snprintf(char* s, size_t n, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = std::vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}
using ::vsscanf;
using ::vfscanf;
using ::vscanf;

// stdlib
using ::strtoll;
using ::strtoull;
using ::strtof;
using ::llabs;
using ::atoll;
using ::lldiv;
using ::lldiv_t;
using ::_Exit;

// string conversions (C99 stdlib and wchar parts)
inline long stol(const string& s, size_t* idx = nullptr, int base = 10) {
    char* end = nullptr;
    long v = ::strtol(s.c_str(), &end, base);
    if (idx) *idx = size_t(end - s.c_str());
    return v;
}
inline int stoi(const string& s, size_t* idx = nullptr, int base = 10) { return int(std::stol(s, idx, base)); }
inline unsigned long stoul(const string& s, size_t* idx = nullptr, int base = 10) {
    char* end = nullptr;
    unsigned long v = ::strtoul(s.c_str(), &end, base);
    if (idx) *idx = size_t(end - s.c_str());
    return v;
}
inline long long stoll(const string& s, size_t* idx = nullptr, int base = 10) { return std::stol(s, idx, base); }
inline unsigned long long stoull(const string& s, size_t* idx = nullptr, int base = 10) { return std::stoul(s, idx, base); }
inline double stod(const string& s, size_t* idx = nullptr) {
    char* end = nullptr;
    double v = ::strtod(s.c_str(), &end);
    if (idx) *idx = size_t(end - s.c_str());
    return v;
}
inline float stof(const string& s, size_t* idx = nullptr) { return float(std::stod(s, idx)); }
template <typename T, typename = enable_if_t<is_integral_v<T>>>
inline wstring to_wstring(T v) {
    string n = std::to_string(v);
    return wstring(n.begin(), n.end());
}
}  // namespace std
#endif
