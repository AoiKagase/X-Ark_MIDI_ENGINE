#pragma once
#include "../common/Types.h"
#include <cmath>
#include <algorithm>

namespace ArkMidi {

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

} // namespace ArkMidi
