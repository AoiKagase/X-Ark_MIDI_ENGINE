#include "UmpDownConverter.h"

namespace XArkMidi {

namespace {

// MIDI 2.0 の 32-bit 値を MIDI 1.0 の 7-bit にスケール (上位 7-bit を採用)
inline u8 Scale32To7(u32 v) {
    return static_cast<u8>(v >> 25);
}

// MIDI 2.0 の 16-bit velocity を MIDI 1.0 の 7-bit にスケール
inline u8 Scale16To7(u16 v) {
    return static_cast<u8>(v >> 9);
}

// MIDI 2.0 の 32-bit Pitch Bend (center=0x80000000) を
// MIDI 1.0 の 14-bit (0-16383, center=8192) に変換し data1/data2 に格納する。
inline void SetPitchBend14(MidiEvent& ev, u32 pb32) {
    const u32 v14 = pb32 >> 18; // 0..16383
    ev.data1 = static_cast<u8>(v14 & 0x7F);        // LSB
    ev.data2 = static_cast<u8>((v14 >> 7) & 0x7F); // MSB
}

} // namespace

void UmpDownConverter::Convert(const u32* words, int wordCount,
                               u32 absoluteTick, std::vector<MidiEvent>& out) {
    if (wordCount < 1) return;

    const auto mt = static_cast<UmpMessageType>((words[0] >> 28) & 0xF);

    switch (mt) {
    case UmpMessageType::Midi1Channel:
        if (wordCount >= 1)
            ConvertMidi1Channel(words[0], absoluteTick, out);
        break;
    case UmpMessageType::Midi2Channel:
        if (wordCount >= 2)
            ConvertMidi2Channel(words[0], words[1], absoluteTick, out);
        break;
    case UmpMessageType::FlexData:
        if (wordCount >= 4)
            ConvertFlexData(words, absoluteTick, out);
        break;
    default:
        // System, Utility, SysEx 等は Phase 1 ではスキップ
        break;
    }
}

// MT=0x2: MIDI 1.0 チャンネルボイスメッセージ
// Word: [MT(4)][group(4)][status(4)][ch(4)][data1(8)][data2(8)]
void UmpDownConverter::ConvertMidi1Channel(u32 word, u32 tick,
                                           std::vector<MidiEvent>& out) {
    const u8 statusNibble = (word >> 20) & 0xF;
    const u8 channel      = (word >> 16) & 0xF;
    const u8 data1        = (word >>  8) & 0x7F;
    const u8 data2        =  word        & 0x7F;

    MidiEvent ev{};
    ev.absoluteTick = tick;
    ev.channel      = channel;
    ev.data1        = data1;
    ev.data2        = data2;

    switch (statusNibble) {
    case kMidiStatusNoteOff:
        ev.type = MidiEventType::NoteOff;
        out.push_back(ev);
        break;
    case kMidiStatusNoteOn:
        ev.type = (data2 == 0) ? MidiEventType::NoteOff : MidiEventType::NoteOn;
        out.push_back(ev);
        break;
    case kMidiStatusPolyPressure:
        ev.type = MidiEventType::PolyPressure;
        out.push_back(ev);
        break;
    case kMidiStatusControlChange:
        ev.type = MidiEventType::ControlChange;
        out.push_back(ev);
        break;
    case kMidiStatusProgramChange:
        ev.type = MidiEventType::ProgramChange;
        out.push_back(ev);
        break;
    case kMidiStatusChannelPressure:
        ev.type = MidiEventType::ChannelPressure;
        out.push_back(ev);
        break;
    case kMidiStatusPitchBend:
        ev.type = MidiEventType::PitchBend;
        out.push_back(ev);
        break;
    default:
        break;
    }
}

// MT=0x4: MIDI 2.0 チャンネルボイスメッセージ
// Word0: [MT(4)][group(4)][status(4)][ch(4)][index1(8)][index2(8)]
// Word1: メッセージ種別に依存した 32-bit データ
void UmpDownConverter::ConvertMidi2Channel(u32 word0, u32 word1, u32 tick,
                                           std::vector<MidiEvent>& out) {
    const u8 statusNibble = (word0 >> 20) & 0xF;
    const u8 channel      = (word0 >> 16) & 0xF;
    const u8 index1       = (word0 >>  8) & 0xFF;
    const u8 index2       =  word0        & 0xFF;

    MidiEvent ev{};
    ev.absoluteTick = tick;
    ev.channel      = channel;

    switch (statusNibble) {
    case kMidiStatusNoteOff: {
        // Word1: [velocity(16)][attribute_data(16)]
        const u8 vel7 = Scale16To7(static_cast<u16>(word1 >> 16));
        ev.type  = MidiEventType::NoteOff;
        ev.data1 = index1; // note number
        ev.data2 = vel7;
        out.push_back(ev);
        break;
    }
    case kMidiStatusNoteOn: {
        // Word1: [velocity(16)][attribute_data(16)]
        const u8 vel7 = Scale16To7(static_cast<u16>(word1 >> 16));
        ev.type  = (vel7 == 0) ? MidiEventType::NoteOff : MidiEventType::NoteOn;
        ev.data1 = index1; // note number
        ev.data2 = vel7;
        out.push_back(ev);
        break;
    }
    case kMidiStatusPolyPressure:
        // Word1: pressure(32)
        ev.type  = MidiEventType::PolyPressure;
        ev.data1 = index1; // note number
        ev.data2 = Scale32To7(word1);
        out.push_back(ev);
        break;

    case kMidiStatusControlChange:
        // Word1: cc_value(32)
        ev.type  = MidiEventType::ControlChange;
        ev.data1 = index1; // CC number (0-127)
        ev.data2 = Scale32To7(word1);
        out.push_back(ev);
        break;

    case kMidiStatusProgramChange: {
        // Word0 index2: option_flags (bit 0 = bank_valid)
        // Word1: [program(8)][reserved(8)][bank_msb(8)][bank_lsb(8)]
        const bool bankValid = (index2 & 0x01) != 0;
        const u8 program     = static_cast<u8>(word1 >> 24);
        const u8 bankMsb     = static_cast<u8>((word1 >> 8) & 0xFF);
        const u8 bankLsb     = static_cast<u8>(word1 & 0xFF);

        if (bankValid) {
            // Bank Select MSB (CC 0)
            MidiEvent ccMsb{};
            ccMsb.absoluteTick = tick;
            ccMsb.channel = channel;
            ccMsb.type    = MidiEventType::ControlChange;
            ccMsb.data1   = 0;
            ccMsb.data2   = bankMsb;
            out.push_back(ccMsb);

            // Bank Select LSB (CC 32)
            MidiEvent ccLsb{};
            ccLsb.absoluteTick = tick;
            ccLsb.channel = channel;
            ccLsb.type    = MidiEventType::ControlChange;
            ccLsb.data1   = 32;
            ccLsb.data2   = bankLsb;
            out.push_back(ccLsb);
        }

        ev.type  = MidiEventType::ProgramChange;
        ev.data1 = program;
        out.push_back(ev);
        break;
    }
    case kMidiStatusChannelPressure:
        // Word1: pressure(32)
        ev.type  = MidiEventType::ChannelPressure;
        ev.data1 = Scale32To7(word1);
        out.push_back(ev);
        break;

    case kMidiStatusPitchBend:
        // Word1: pitch_bend(32), center=0x80000000
        ev.type = MidiEventType::PitchBend;
        SetPitchBend14(ev, word1);
        out.push_back(ev);
        break;

    case kMidi2StatusRegController: {
        // Registered Controller (RPN) → CC101/100/6/38 シーケンスに展開
        // Word0: index1=bank, index2=index
        // Word1: value(32)
        const u8 rpnMsb  = index1; // bank
        const u8 rpnLsb  = index2; // index
        const u8 dataMsb = static_cast<u8>(word1 >> 25);           // 上位 7-bit
        const u8 dataLsb = static_cast<u8>((word1 >> 18) & 0x7F);  // 次の 7-bit

        auto makeCC = [&](u8 cc, u8 val) {
            MidiEvent e{};
            e.absoluteTick = tick;
            e.channel = channel;
            e.type    = MidiEventType::ControlChange;
            e.data1   = cc;
            e.data2   = val;
            return e;
        };
        out.push_back(makeCC(101, rpnMsb));  // RPN MSB
        out.push_back(makeCC(100, rpnLsb));  // RPN LSB
        out.push_back(makeCC(6,   dataMsb)); // Data Entry MSB
        out.push_back(makeCC(38,  dataLsb)); // Data Entry LSB
        break;
    }
    case kMidi2StatusAsgController: {
        // Assignable Controller (NRPN) → CC99/98/6/38 シーケンスに展開
        const u8 nrpnMsb = index1;
        const u8 nrpnLsb = index2;
        const u8 dataMsb = static_cast<u8>(word1 >> 25);
        const u8 dataLsb = static_cast<u8>((word1 >> 18) & 0x7F);

        auto makeCC = [&](u8 cc, u8 val) {
            MidiEvent e{};
            e.absoluteTick = tick;
            e.channel = channel;
            e.type    = MidiEventType::ControlChange;
            e.data1   = cc;
            e.data2   = val;
            return e;
        };
        out.push_back(makeCC(99, nrpnMsb));  // NRPN MSB
        out.push_back(makeCC(98, nrpnLsb));  // NRPN LSB
        out.push_back(makeCC(6,  dataMsb));  // Data Entry MSB
        out.push_back(makeCC(38, dataLsb));  // Data Entry LSB
        break;
    }
    default:
        // Per-Note Controller, Per-Note Pitch Bend 等は Phase 1 ではスキップ
        break;
    }
}

// MT=0xD: Flex Data (128-bit = 4 words)
// Word0: [MT(4)][group(4)][form(2)][addrs(2)][ch(4)][status_bank(8)][status_type(8)]
// Word1-3: ペイロード
void UmpDownConverter::ConvertFlexData(const u32* words, u32 tick,
                                       std::vector<MidiEvent>& out) {
    const u8 statusBank = (words[0] >> 8) & 0xFF;
    const u8 statusType =  words[0]       & 0xFF;

    if (statusBank != kFlexBankSetup) return;

    if (statusType == kFlexTypeSetTempo) {
        // Word1: テンポ (10ナノ秒/beat 単位)
        // μs/beat = tempo_10ns / 100
        const u32 tempo10ns = words[1];
        const u32 tempoUs   = (tempo10ns == 0) ? 500000u
                            : static_cast<u32>((static_cast<u64>(tempo10ns) + 50) / 100);

        MidiEvent ev{};
        ev.absoluteTick = tick;
        ev.type         = MidiEventType::MetaTempo;
        ev.tempoUs      = tempoUs;
        out.push_back(ev);
    }
    else if (statusType == kFlexTypeTimeSig) {
        // 拍子記号: 現時点では記録のみ
        MidiEvent ev{};
        ev.absoluteTick = tick;
        ev.type         = MidiEventType::MetaTimeSig;
        out.push_back(ev);
    }
}

} // namespace XArkMidi
