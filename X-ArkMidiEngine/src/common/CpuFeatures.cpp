#include "CpuFeatures.h"
#include <intrin.h>

namespace XArkMidi::CpuFeatures {

namespace {

bool DetectAvx2() {
#if defined(_M_X64) || defined(_M_IX86)
    int regs[4] = {};
    __cpuid(regs, 1);

    const bool hasOsxsave = (regs[2] & (1 << 27)) != 0;
    const bool hasAvx = (regs[2] & (1 << 28)) != 0;
    if (!hasOsxsave || !hasAvx) {
        return false;
    }

    const unsigned __int64 xcr0 = _xgetbv(0);
    if ((xcr0 & 0x6) != 0x6) {
        return false;
    }

    __cpuidex(regs, 7, 0);
    return (regs[1] & (1 << 5)) != 0;
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

