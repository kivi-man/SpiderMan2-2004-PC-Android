#pragma once
#include <cstdint>

#if defined(XBE_BIG_ENDIAN) && XBE_BIG_ENDIAN
// A big-endian host keeps each 32 bit lane in host order, so a 64 bit half is not the two lanes' bytes in a row: q
// is a pair of lanes that reads and writes as the value x86 sees (lane 0 the low half).
struct X86Q64 {
    uint32_t lo, hi;
    operator uint64_t() const { return uint64_t(lo) | (uint64_t(hi) << 32); }
    X86Q64& operator=(uint64_t v) {
        lo = uint32_t(v);
        hi = uint32_t(v >> 32);
        return *this;
    }
};
union X86Xmm {
    float f[4];
    uint32_t u[4];
    int32_t i[4];
    X86Q64 q[2];
};
#else
union X86Xmm {
    float f[4];
    uint32_t u[4];
    int32_t i[4];
    uint64_t q[2];
};
#endif

struct alignas(64) X86Context {
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t cf, zf, sf, of, pf;  // only live across tail calls, functions keep flags in locals
    uint32_t df;
    uint32_t xflags;  // AC and ID, kept so feature probes see them toggle
    uint32_t fs_base;
    uint32_t top;  // x87 stack top
    uint16_t fcw, fsw;
    uint32_t mxcsr;
    double st[8];
    uint64_t mm[8];
    X86Xmm xmm[8];
    // runtime owned
    uint32_t thread_handle;
    uint32_t last_icall;
    void* host;
};

using X86Func = void(X86Context& __restrict ctx, uint8_t* base);

struct X86FuncMapping {
    uint32_t guest;
    X86Func* host;
};

// produced by the recompiler
extern const X86FuncMapping X86FuncMappings[];
extern const uint32_t X86Continuations[];  // zero terminated
extern const uint32_t X86CodeBase, X86CodeEnd, X86ImageBase, X86ImageEnd, X86EntryPoint, X86KernelThunk;
