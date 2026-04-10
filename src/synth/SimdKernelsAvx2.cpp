/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "SimdKernels.h"
#include <immintrin.h>

namespace XArkMidi::Simd {

XARK_NOINLINE void AccumulateSixAvx2(
    f32* dstOutL, f32* dstOutR,
    f32* dstReverbL, f32* dstReverbR,
    f32* dstChorusL, f32* dstChorusR,
    const f32* srcOutL, const f32* srcOutR,
    const f32* srcReverbL, const f32* srcReverbR,
    const f32* srcChorusL, const f32* srcChorusR,
    u32 count) {
    u32 i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m256 src0 = _mm256_loadu_ps(srcOutL + i);
        const __m256 src1 = _mm256_loadu_ps(srcOutR + i);
        const __m256 src2 = _mm256_loadu_ps(srcReverbL + i);
        const __m256 src3 = _mm256_loadu_ps(srcReverbR + i);
        const __m256 src4 = _mm256_loadu_ps(srcChorusL + i);
        const __m256 src5 = _mm256_loadu_ps(srcChorusR + i);

        _mm256_storeu_ps(dstOutL + i, _mm256_add_ps(_mm256_loadu_ps(dstOutL + i), src0));
        _mm256_storeu_ps(dstOutR + i, _mm256_add_ps(_mm256_loadu_ps(dstOutR + i), src1));
        _mm256_storeu_ps(dstReverbL + i, _mm256_add_ps(_mm256_loadu_ps(dstReverbL + i), src2));
        _mm256_storeu_ps(dstReverbR + i, _mm256_add_ps(_mm256_loadu_ps(dstReverbR + i), src3));
        _mm256_storeu_ps(dstChorusL + i, _mm256_add_ps(_mm256_loadu_ps(dstChorusL + i), src4));
        _mm256_storeu_ps(dstChorusR + i, _mm256_add_ps(_mm256_loadu_ps(dstChorusR + i), src5));
    }

    for (; i < count; ++i) {
        dstOutL[i] += srcOutL[i];
        dstOutR[i] += srcOutR[i];
        dstReverbL[i] += srcReverbL[i];
        dstReverbR[i] += srcReverbR[i];
        dstChorusL[i] += srcChorusL[i];
        dstChorusR[i] += srcChorusR[i];
    }
}

XARK_NOINLINE void MixMonoContiguousDryAvx2(
    f32* dstOutL, f32* dstOutR,
    const i16* src,
    u32 count,
    f32 gainL, f32 gainR) {
    const __m256 gainVecL = _mm256_set1_ps(gainL);
    const __m256 gainVecR = _mm256_set1_ps(gainR);

    u32 i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m128i s16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        const __m256 sample = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(s16));

        _mm256_storeu_ps(dstOutL + i, _mm256_add_ps(_mm256_loadu_ps(dstOutL + i), _mm256_mul_ps(sample, gainVecL)));
        _mm256_storeu_ps(dstOutR + i, _mm256_add_ps(_mm256_loadu_ps(dstOutR + i), _mm256_mul_ps(sample, gainVecR)));
    }

    for (; i < count; ++i) {
        const f32 sample = static_cast<f32>(src[i]);
        dstOutL[i] += sample * gainL;
        dstOutR[i] += sample * gainR;
    }
}

XARK_NOINLINE void MixMonoContiguousFxAvx2(
    f32* dstOutL, f32* dstOutR,
    f32* dstReverbL, f32* dstReverbR,
    f32* dstChorusL, f32* dstChorusR,
    const i16* src,
    u32 count,
    f32 dryGainL, f32 dryGainR,
    f32 reverbGainL, f32 reverbGainR,
    f32 chorusGainL, f32 chorusGainR) {
    const __m256 dryGainVecL = _mm256_set1_ps(dryGainL);
    const __m256 dryGainVecR = _mm256_set1_ps(dryGainR);
    const __m256 reverbGainVecL = _mm256_set1_ps(reverbGainL);
    const __m256 reverbGainVecR = _mm256_set1_ps(reverbGainR);
    const __m256 chorusGainVecL = _mm256_set1_ps(chorusGainL);
    const __m256 chorusGainVecR = _mm256_set1_ps(chorusGainR);

    u32 i = 0;
    for (; i + 8 <= count; i += 8) {
        const __m128i s16 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        const __m256 sample = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(s16));

        _mm256_storeu_ps(dstOutL + i, _mm256_add_ps(_mm256_loadu_ps(dstOutL + i), _mm256_mul_ps(sample, dryGainVecL)));
        _mm256_storeu_ps(dstOutR + i, _mm256_add_ps(_mm256_loadu_ps(dstOutR + i), _mm256_mul_ps(sample, dryGainVecR)));
        _mm256_storeu_ps(dstReverbL + i, _mm256_add_ps(_mm256_loadu_ps(dstReverbL + i), _mm256_mul_ps(sample, reverbGainVecL)));
        _mm256_storeu_ps(dstReverbR + i, _mm256_add_ps(_mm256_loadu_ps(dstReverbR + i), _mm256_mul_ps(sample, reverbGainVecR)));
        _mm256_storeu_ps(dstChorusL + i, _mm256_add_ps(_mm256_loadu_ps(dstChorusL + i), _mm256_mul_ps(sample, chorusGainVecL)));
        _mm256_storeu_ps(dstChorusR + i, _mm256_add_ps(_mm256_loadu_ps(dstChorusR + i), _mm256_mul_ps(sample, chorusGainVecR)));
    }

    for (; i < count; ++i) {
        const f32 sample = static_cast<f32>(src[i]);
        dstOutL[i] += sample * dryGainL;
        dstOutR[i] += sample * dryGainR;
        dstReverbL[i] += sample * reverbGainL;
        dstReverbR[i] += sample * reverbGainR;
        dstChorusL[i] += sample * chorusGainL;
        dstChorusR[i] += sample * chorusGainR;
    }
}

} // namespace XArkMidi::Simd

