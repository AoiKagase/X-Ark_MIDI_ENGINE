#pragma once
#include "../common/Types.h"

namespace XArkMidi {

// UMP メッセージタイプ (先頭 32-bit ワードの上位 4-bit)
// 参照: MIDI 2.0 仕様 MC-HB-20 (Universal MIDI Packet)
enum class UmpMessageType : u8 {
    Utility         = 0x0,  // 32-bit
    System          = 0x1,  // 32-bit  (MTC, Song Position 等)
    Midi1Channel    = 0x2,  // 32-bit  (MIDI 1.0 チャンネルボイスメッセージ)
    Data64          = 0x3,  // 64-bit  (SysEx7)
    Midi2Channel    = 0x4,  // 64-bit  (MIDI 2.0 チャンネルボイスメッセージ)
    Data128         = 0x5,  // 128-bit (SysEx8 / Mixed Data Set)
    Reserved6       = 0x6,
    Reserved7       = 0x7,
    Reserved8       = 0x8,
    Reserved9       = 0x9,
    ReservedA       = 0xA,
    ReservedB       = 0xB,
    ReservedC       = 0xC,
    FlexData        = 0xD,  // 128-bit (テンポ・拍子記号 等)
    ReservedE       = 0xE,
    UmpStream       = 0xF,  // 128-bit (UMP ストリームメッセージ)
};

// UMP メッセージタイプごとの 32-bit ワード数
inline int UmpWordCount(UmpMessageType mt) {
    switch (mt) {
    case UmpMessageType::Utility:      return 1;
    case UmpMessageType::System:       return 1;
    case UmpMessageType::Midi1Channel: return 1;
    case UmpMessageType::Data64:       return 2;
    case UmpMessageType::Midi2Channel: return 2;
    case UmpMessageType::Data128:      return 4;
    case UmpMessageType::FlexData:     return 4;
    case UmpMessageType::UmpStream:    return 4;
    default:                           return 1; // reserved: 1ワードとして読み飛ばす
    }
}

// チャンネルボイスメッセージのステータスニブル (上位 4-bit)
// MT=0x2 (MIDI 1.0) と MT=0x4 (MIDI 2.0) の両方で共通
constexpr u8 kMidiStatusNoteOff         = 0x8;
constexpr u8 kMidiStatusNoteOn          = 0x9;
constexpr u8 kMidiStatusPolyPressure    = 0xA;
constexpr u8 kMidiStatusControlChange   = 0xB;
constexpr u8 kMidiStatusProgramChange   = 0xC;
constexpr u8 kMidiStatusChannelPressure = 0xD;
constexpr u8 kMidiStatusPitchBend       = 0xE;

// MIDI 2.0 専用ステータスニブル (MT=0x4 のみ)
constexpr u8 kMidi2StatusRegPerNote     = 0x0; // Per-Note Registered Controller
constexpr u8 kMidi2StatusAsgPerNote     = 0x1; // Per-Note Assignable Controller
constexpr u8 kMidi2StatusRegController  = 0x2; // Registered Controller (RPN)
constexpr u8 kMidi2StatusAsgController  = 0x3; // Assignable Controller (NRPN)
constexpr u8 kMidi2StatusRelRegCtrl     = 0x4; // Relative Registered Controller
constexpr u8 kMidi2StatusRelAsgCtrl     = 0x5; // Relative Assignable Controller
constexpr u8 kMidi2StatusPerNotePB      = 0x6; // Per-Note Pitch Bend
constexpr u8 kMidi2StatusPerNoteMgmt    = 0xF; // Per-Note Management

// Flex Data: status bank / status type (MT=0xD, Word0 下位 16-bit)
constexpr u8 kFlexBankSetup     = 0x00; // Setup and Performance bank
constexpr u8 kFlexTypeSetTempo  = 0x00; // Set Tempo
constexpr u8 kFlexTypeTimeSig   = 0x01; // Time Signature

} // namespace XArkMidi
