/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "SimdKernels.h"
#include "../common/CpuFeatures.h"

namespace XArkMidi::Simd {

XARK_NOINLINE void AccumulateSixAvx2(
    f32* dstOutL, f32* dstOutR,
    f32* dstReverbL, f32* dstReverbR,
    f32* dstChorusL, f32* dstChorusR,
    const f32* srcOutL, const f32* srcOutR,
    const f32* srcReverbL, const f32* srcReverbR,
    const f32* srcChorusL, const f32* srcChorusR,
    u32 count);

namespace {

using AccumulateSixFn = void(*)(
    f32*, f32*, f32*, f32*, f32*, f32*,
    const f32*, const f32*, const f32*, const f32*, const f32*, const f32*, u32);

void AccumulateSixScalar(
    f32* dstOutL, f32* dstOutR,
    f32* dstReverbL, f32* dstReverbR,
    f32* dstChorusL, f32* dstChorusR,
    const f32* srcOutL, const f32* srcOutR,
    const f32* srcReverbL, const f32* srcReverbR,
    const f32* srcChorusL, const f32* srcChorusR,
    u32 count) {
    for (u32 i = 0; i < count; ++i) {
        dstOutL[i] += srcOutL[i];
        dstOutR[i] += srcOutR[i];
        dstReverbL[i] += srcReverbL[i];
        dstReverbR[i] += srcReverbR[i];
        dstChorusL[i] += srcChorusL[i];
        dstChorusR[i] += srcChorusR[i];
    }
}

AccumulateSixFn ResolveAccumulateSix() {
    return CpuFeatures::HasAvx2() ? &AccumulateSixAvx2 : &AccumulateSixScalar;
}

} // namespace

bool HasAvx2() {
    return CpuFeatures::HasAvx2();
}

void AccumulateSix(
    f32* dstOutL, f32* dstOutR,
    f32* dstReverbL, f32* dstReverbR,
    f32* dstChorusL, f32* dstChorusR,
    const f32* srcOutL, const f32* srcOutR,
    const f32* srcReverbL, const f32* srcReverbR,
    const f32* srcChorusL, const f32* srcChorusR,
    u32 count) {
    static const AccumulateSixFn fn = ResolveAccumulateSix();
    fn(dstOutL, dstOutR, dstReverbL, dstReverbR, dstChorusL, dstChorusR,
       srcOutL, srcOutR, srcReverbL, srcReverbR, srcChorusL, srcChorusR, count);
}

} // namespace XArkMidi::Simd

