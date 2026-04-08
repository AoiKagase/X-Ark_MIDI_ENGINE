#pragma once
#include "../common/Types.h"
#include <cmath>
#include <algorithm>

namespace XArkMidi {

constexpr i64 kSamplePosFracBits = 32;
constexpr i64 kSamplePosFracOne = 1LL << kSamplePosFracBits;
constexpr i64 kSamplePosFracMask = kSamplePosFracOne - 1;
constexpr f32 kSamplePosFracScale = 1.0f / 4294967296.0f;

// 線形補間（サンプルデータのピッチ変換用）
// pos は i16 配列内の fractional index
// dataSize: sampleData の要素数（境界チェック用）
inline f32 LinearInterp(const i16* data, f64 pos, size_t dataSize) {
    if (dataSize == 0) return 0.0f;
    size_t i0 = static_cast<size_t>(pos);
    if (i0 + 1 >= dataSize) {
        // 末端: 1点のみ返す（パディング扱い）
        i0 = (dataSize >= 1) ? (dataSize - 1) : 0;
        return static_cast<f32>(data[i0]);
    }
    f32 f = static_cast<f32>(pos - static_cast<f64>(i0));
    return static_cast<f32>(data[i0    ]) * (1.0f - f)
         + static_cast<f32>(data[i0 + 1]) * f;
}

inline f32 LinearInterpFixed(const i16* data, i64 posFixed, size_t dataSize) {
    if (dataSize == 0) return 0.0f;
    if (posFixed <= 0) {
        return static_cast<f32>(data[0]);
    }

    size_t i0 = static_cast<size_t>(posFixed >> kSamplePosFracBits);
    if (i0 + 1 >= dataSize) {
        i0 = dataSize - 1;
        return static_cast<f32>(data[i0]);
    }

    const f32 frac = static_cast<f32>(posFixed & kSamplePosFracMask) * kSamplePosFracScale;
    const f32 s0 = static_cast<f32>(data[i0]);
    return s0 + (static_cast<f32>(data[i0 + 1]) - s0) * frac;
}

inline f32 LinearInterpFixedUnchecked(const i16* data, i64 posFixed) {
    const size_t i0 = static_cast<size_t>(posFixed >> kSamplePosFracBits);
    const f32 frac = static_cast<f32>(posFixed & kSamplePosFracMask) * kSamplePosFracScale;
    const f32 s0 = static_cast<f32>(data[i0]);
    return s0 + (static_cast<f32>(data[i0 + 1]) - s0) * frac;
}

inline f32 CubicHermite(f32 y0, f32 y1, f32 y2, f32 y3, f32 t) {
    const f32 c0 = y1;
    const f32 c1 = 0.5f * (y2 - y0);
    const f32 c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const f32 c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * t + c2) * t + c1) * t + c0;
}

inline size_t ClampSampleIndex(i64 index, size_t dataSize) {
    if (dataSize == 0) return 0;
    if (index <= 0) return 0;
    const i64 maxIndex = static_cast<i64>(dataSize - 1);
    return static_cast<size_t>(std::min(index, maxIndex));
}

inline size_t WrapLoopSampleIndex(i64 index, size_t dataSize, size_t loopStartIndex, size_t loopEndIndex) {
    if (dataSize == 0) return 0;
    const i64 maxIndex = static_cast<i64>(dataSize - 1);
    if (loopEndIndex <= loopStartIndex + 1) {
        return ClampSampleIndex(index, dataSize);
    }

    const i64 loopStart = static_cast<i64>(std::min(loopStartIndex, dataSize - 1));
    const i64 loopEnd = static_cast<i64>(std::min(loopEndIndex, dataSize));
    const i64 loopLength = loopEnd - loopStart;
    if (loopLength <= 1) {
        return ClampSampleIndex(index, dataSize);
    }

    if (index < loopStart) {
        return ClampSampleIndex(index, dataSize);
    }

    const i64 wrapped = loopStart + ((index - loopStart) % loopLength + loopLength) % loopLength;
    return static_cast<size_t>(std::clamp<i64>(wrapped, 0, maxIndex));
}

inline f32 CubicInterpFixed(const i16* data, i64 posFixed, size_t dataSize) {
    if (dataSize == 0) return 0.0f;

    const i64 i1 = posFixed >> kSamplePosFracBits;
    const f32 frac = static_cast<f32>(posFixed & kSamplePosFracMask) * kSamplePosFracScale;
    const f32 y0 = static_cast<f32>(data[ClampSampleIndex(i1 - 1, dataSize)]);
    const f32 y1 = static_cast<f32>(data[ClampSampleIndex(i1, dataSize)]);
    const f32 y2 = static_cast<f32>(data[ClampSampleIndex(i1 + 1, dataSize)]);
    const f32 y3 = static_cast<f32>(data[ClampSampleIndex(i1 + 2, dataSize)]);
    return CubicHermite(y0, y1, y2, y3, frac);
}

inline f32 CubicInterpFixedLooped(const i16* data,
                                  i64 posFixed,
                                  size_t dataSize,
                                  size_t loopStartIndex,
                                  size_t loopEndIndex) {
    if (dataSize == 0) return 0.0f;

    const i64 i1 = posFixed >> kSamplePosFracBits;
    const f32 frac = static_cast<f32>(posFixed & kSamplePosFracMask) * kSamplePosFracScale;
    const f32 y0 = static_cast<f32>(data[WrapLoopSampleIndex(i1 - 1, dataSize, loopStartIndex, loopEndIndex)]);
    const f32 y1 = static_cast<f32>(data[WrapLoopSampleIndex(i1, dataSize, loopStartIndex, loopEndIndex)]);
    const f32 y2 = static_cast<f32>(data[WrapLoopSampleIndex(i1 + 1, dataSize, loopStartIndex, loopEndIndex)]);
    const f32 y3 = static_cast<f32>(data[WrapLoopSampleIndex(i1 + 2, dataSize, loopStartIndex, loopEndIndex)]);
    return CubicHermite(y0, y1, y2, y3, frac);
}

// timecents -> 秒変換（SF2 spec: tc = 1200 * log2(sec)）
// SF2 仕様の有効範囲 [-12000, 8000] にクランプして異常値を防ぐ
inline f64 TimecentsToSeconds(i32 tc) {
    if (tc <= -32768) {
        return 0.0;
    }
    if (tc < -12000) tc = -12000; // 最短: pow(2,-10) ≈ 0.001 sec
    if (tc >   8000) tc =   8000; // 最長: pow(2, 6.67) ≈ 101 sec
    return pow(2.0, static_cast<f64>(tc) / 1200.0);
}

// centibels -> 線形振幅変換（SustainVolEnv に使用）
inline f64 CentibelsToGain(i32 cb) {
    if (cb <= 0)    return 1.0; // 0cb = フル音量（負値はクランプ）
    if (cb >= 1440) return 0.0; // -144dB = 無音
    return pow(10.0, -static_cast<f64>(cb) / 200.0);
}

// InitialAttenuation: centibels -> 線形振幅変換
inline f64 AttenuationToGain(i32 centibels) {
    if (centibels <= 0) return 1.0;
    if (centibels >= 1440) return 0.0; // -144dB 以上は無音
    return pow(10.0, -static_cast<f64>(centibels) / 200.0);
}

} // namespace XArkMidi

