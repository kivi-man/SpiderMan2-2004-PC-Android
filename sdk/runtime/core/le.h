#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

// Console data is little-endian. x86_ld_le / x86_st_le read and write a little-endian T at any alignment on any host
// (a plain copy on a little-endian one, a byte swap on a big-endian one), x86_le_swap turns an array of them from
// console order to host order or back in place (nothing on a little-endian host). x86_bswap turns a value between
// host order and the console's: a swap on a big-endian host, the value itself on a little-endian one.
template <typename T>
struct x86_le_uint;
template <> struct x86_le_uint<uint8_t> { using type = uint8_t; };
template <> struct x86_le_uint<int8_t> { using type = uint8_t; };
template <> struct x86_le_uint<uint16_t> { using type = uint16_t; };
template <> struct x86_le_uint<int16_t> { using type = uint16_t; };
template <> struct x86_le_uint<uint32_t> { using type = uint32_t; };
template <> struct x86_le_uint<int32_t> { using type = uint32_t; };
template <> struct x86_le_uint<float> { using type = uint32_t; };
template <> struct x86_le_uint<uint64_t> { using type = uint64_t; };
template <> struct x86_le_uint<int64_t> { using type = uint64_t; };
template <> struct x86_le_uint<double> { using type = uint64_t; };
#if defined(XBE_BIG_ENDIAN) && XBE_BIG_ENDIAN
inline uint8_t x86_bswap(uint8_t v) { return v; }
inline uint16_t x86_bswap(uint16_t v) { return __builtin_bswap16(v); }
inline uint32_t x86_bswap(uint32_t v) { return __builtin_bswap32(v); }
inline uint64_t x86_bswap(uint64_t v) { return __builtin_bswap64(v); }
#else
template <typename U>
inline U x86_bswap(U v) { return v; }
#endif
template <typename T>
inline T x86_ld_le(const void* p) {
    typename x86_le_uint<T>::type u;
    std::memcpy(&u, p, sizeof(u));
    u = x86_bswap(u);
    T v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}
template <typename T>
inline void x86_st_le(void* p, T v) {
    typename x86_le_uint<T>::type u;
    std::memcpy(&u, &v, sizeof(u));
    u = x86_bswap(u);
    std::memcpy(p, &u, sizeof(u));
}
template <typename T>
inline void x86_le_swap(T* p, size_t count) {
#if defined(XBE_BIG_ENDIAN) && XBE_BIG_ENDIAN
    for (size_t i = 0; i < count; i++) p[i] = x86_ld_le<T>(&p[i]);
#else
    (void)p;
    (void)count;
#endif
}
