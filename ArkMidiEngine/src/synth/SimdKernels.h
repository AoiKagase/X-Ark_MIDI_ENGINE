#pragma once
#include "../common/Types.h"

namespace ArkMidi::Simd {

bool HasAvx2();

void AccumulateSix(f32* dstOutL, f32* dstOutR,
                   f32* dstReverbL, f32* dstReverbR,
                   f32* dstChorusL, f32* dstChorusR,
                   const f32* srcOutL, const f32* srcOutR,
                   const f32* srcReverbL, const f32* srcReverbR,
                   const f32* srcChorusL, const f32* srcChorusR,
                   u32 count);

void MixMonoContiguousDryAvx2(
    f32* dstOutL, f32* dstOutR,
    const i16* src,
    u32 count,
    f32 gainL, f32 gainR);

void MixMonoContiguousFxAvx2(
    f32* dstOutL, f32* dstOutR,
    f32* dstReverbL, f32* dstReverbR,
    f32* dstChorusL, f32* dstChorusR,
    const i16* src,
    u32 count,
    f32 dryGainL, f32 dryGainR,
    f32 reverbGainL, f32 reverbGainR,
    f32 chorusGainL, f32 chorusGainR);

} // namespace ArkMidi::Simd
