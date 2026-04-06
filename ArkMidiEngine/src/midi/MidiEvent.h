#pragma once
#include "../common/Types.h"
#include <vector>

namespace ArkMidi {

enum class MidiEventType : u8 {
    // チャンネルメッセージ
    NoteOff         = 0x80,
    NoteOn          = 0x90,
    PolyPressure    = 0xA0,
    ControlChange   = 0xB0,
    ProgramChange   = 0xC0,
    ChannelPressure = 0xD0,
    PitchBend       = 0xE0,
    // SysEx
    SysEx           = 0xF0,
    // メタイベント（内部表現用）
    MetaTempo       = 0xF1, // テンポ変更
    MetaTimeSig     = 0xF2, // 拍子記号
    MetaEndOfTrack  = 0xF3,
    MetaOther       = 0xFE,
    // 無効
    Invalid         = 0xFF,
};

struct MidiEvent {
    u32           absoluteTick; // デルタtick累積後の絶対tick
    MidiEventType type;
    u8            channel;      // 0-15
    u8            data1;        // ノート番号 / CC番号 / プログラム番号 等
    u8            data2;        // ベロシティ / CC値 等

    // MetaTempo 専用: μsec/beat (BPM = 60,000,000 / tempoUs)
    u32           tempoUs;
    std::vector<u8> payload;

    // PitchBend の値: (data2 << 7 | data1) - 8192, 範囲 [-8192, 8191]
    i16 PitchBendValue() const {
        return static_cast<i16>((static_cast<i16>(data2) << 7 | data1) - 8192);
    }
};

} // namespace ArkMidi
