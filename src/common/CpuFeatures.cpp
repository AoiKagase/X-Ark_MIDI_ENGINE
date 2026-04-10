#include "CpuFeatures.h"
#include <cstdint>

#if defined(_MSC_VER)
#  include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
#  include <cpuid.h>
#endif

namespace XArkMidi::CpuFeatures {

namespace {

bool DetectAvx2() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    // MSVC + x86/x64
    int regs[4] = {};
    __cpuid(regs, 1);

    const bool hasOsxsave = (regs[2] & (1 << 27)) != 0;
    const bool hasAvx     = (regs[2] & (1 << 28)) != 0;
    if (!hasOsxsave || !hasAvx)
        return false;

    const unsigned __int64 xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6)
        return false;

    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;

#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
    // GCC/Clang + x86/x64
    unsigned int eax, ebx, ecx, edx;

    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx))
        return false;

    const bool hasOsxsave = (ecx & (1u << 27)) != 0;
    const bool hasAvx     = (ecx & (1u << 28)) != 0;
    if (!hasOsxsave || !hasAvx)
        return false;

    // xgetbv via inline asm
    uint32_t xcr0lo, xcr0hi;
    __asm__ volatile("xgetbv" : "=a"(xcr0lo), "=d"(xcr0hi) : "c"(0u));
    if ((xcr0lo & 0x6u) != 0x6u)
        return false;

    if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx))
        return false;
    return (ebx & (1u << 5)) != 0;

#else
    return false;
#endif
}

} // namespace

bool HasAvx2() {
    static const bool hasAvx2 = DetectAvx2();
    return hasAvx2;
}

} // namespace XArkMidi::CpuFeatures

