/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/Types.h"
#include <vector>

namespace XArkMidi {

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
    // MIDI 2.0 Per-Note メッセージ（内部表現用）
    PerNotePitchBend  = 0xF4, // Per-Note Pitch Bend, data1=note, value32=32-bit bend (center=0x80000000)
    PerNoteRegCtrl    = 0xF5, // Per-Note Registered/Assignable Controller, data1=note, data2=index, value32
    PerNoteManagement = 0xF6, // Per-Note Management, data1=note, data2=option_flags
    // 無効
    Invalid         = 0xFF,
};

struct MidiEvent {
    u32           absoluteTick; // デルタtick累積後の絶対tick
    MidiEventType type;
    u8            channel;      // 0-15
    u8            data1;        // ノート番号 / CC番号 / プログラム番号 等 (7-bit互換)
    u8            data2;        // ベロシティ / CC値 等 (7-bit互換)

    // MIDI 2.0 高精度フィールド
    // 常に有効: MIDI 1.0 由来の場合はビットレプリケーションでアップスケール済み
    //   NoteOn/Off : velocity16 に 16-bit velocity (0-65535)
    //   CC         : value32 に 32-bit CC値 (0-4294967295)
    //   PolyPressure/ChannelPressure: value32 に 32-bit 圧力値
    //   PitchBend  : value32 に 32-bit pitch bend (center=0x80000000)
    u16           velocity16 = 0;
    u32           value32    = 0;

    // MetaTempo 専用: μsec/beat (BPM = 60,000,000 / tempoUs)
    u32           tempoUs;
    std::vector<u8> payload;

    // PitchBend の値: (data2 << 7 | data1) - 8192, 範囲 [-8192, 8191]
    i16 PitchBendValue() const {
        return static_cast<i16>((static_cast<i16>(data2) << 7 | data1) - 8192);
    }
};

} // namespace XArkMidi
