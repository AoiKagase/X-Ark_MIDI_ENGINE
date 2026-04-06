#pragma once
#include <cstdint>
#include <cstddef>
#include <cmath>

namespace ArkMidi {

// 基本型エイリアス
using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i8  = int8_t;
using i16 = int16_t;
using i32 = int32_t;
using i64 = int64_t;
using f32 = float;
using f64 = double;

// MIDI定数
constexpr int   MIDI_CHANNEL_COUNT    = 16;
constexpr int   MIDI_DRUM_CHANNEL     = 9;
constexpr int   MIDI_DRUM_BANK        = 128;
constexpr int   MIDI_NOTE_COUNT       = 128;
constexpr int   MIDI_CC_COUNT         = 128;
constexpr u32   MIDI_DEFAULT_TEMPO_US = 500000; // 120 BPM

// SF2定数
constexpr int   SF2_GEN_COUNT         = 61;

// シンセサイザー定数
constexpr int   MAX_VOICES            = 256;

// クリップ（int16_t範囲に収める）
inline i16 ClampToI16(f32 v) {
    // Round to nearest integer to avoid truncation bias when converting
    // floating samples to int16. Then clamp to int16 range.
    const long rounded = std::lround(static_cast<double>(v));
    if (rounded >  32767L) return  32767;
    if (rounded < -32768L) return -32768;
    return static_cast<i16>(rounded);
}

} // namespace ArkMidi
