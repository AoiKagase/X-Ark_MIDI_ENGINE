/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "MidiTrack.h"
#include "../midi2/UmpTypes.h"
#include <stdexcept>

namespace XArkMidi {

bool MidiTrack::Parse(BinaryReader& r) {
    events_.clear();
    runningStatus_ = 0;
    u32 currentTick = 0;

    try {
        while (!r.IsEof()) {
            u32 delta = r.ReadVLQ();
            currentTick += delta;
            if (!ParseEvent(r, currentTick))
                return false;
            // EndOfTrack 後はパースを停止する
            // （MIDI仕様ではこれ以降のデータは無効。パディングバイトが
            //   大量のスプリアスイベントを生成するのを防ぐ）
            if (!events_.empty() && events_.back().type == MidiEventType::MetaEndOfTrack)
                break;
        }
    }
    catch (const std::exception& e) {
        errorMsg_ = e.what();
        return false;
    }
    return true;
}

bool MidiTrack::ParseEvent(BinaryReader& r, u32 currentTick) {
    u8 statusByte = r.ReadU8();

    // ランニングステータス: MSBが0ならデータバイト（前のステータスを使う）
    if ((statusByte & 0x80) == 0) {
        if (runningStatus_ == 0) {
            errorMsg_ = "Running status without prior status byte";
            return false;
        }
        // 1バイト分を巻き戻す（ReadU8で消費してしまった）
        r.Seek(r.Tell() - 1);
        statusByte = runningStatus_;
    }
    else {
        // SysExとメタ以外はランニングステータスを更新
        if (statusByte != 0xF0 && statusByte != 0xF7 && statusByte != 0xFF)
            runningStatus_ = statusByte;
    }

    MidiEvent ev{};
    ev.absoluteTick = currentTick;
    ev.channel      = statusByte & 0x0F;

    u8 msgType = statusByte & 0xF0;

    if (statusByte == 0xFF) {
        // メタイベント
        u8  metaType = r.ReadU8();
        u32 length   = r.ReadVLQ();
        auto metaData = r.ReadSlice(length);

        switch (metaType) {
        case 0x51: // Set Tempo
            if (length >= 3) {
                ev.type    = MidiEventType::MetaTempo;
                ev.tempoUs = (static_cast<u32>(metaData.ReadU8()) << 16)
                           | (static_cast<u32>(metaData.ReadU8()) <<  8)
                           |  static_cast<u32>(metaData.ReadU8());
                events_.push_back(ev);
            }
            break;
        case 0x58: // Time Signature（現在は記録のみ）
            ev.type = MidiEventType::MetaTimeSig;
            events_.push_back(ev);
            break;
        case 0x2F: // End of Track
            ev.type = MidiEventType::MetaEndOfTrack;
            events_.push_back(ev);
            break;
        default:
            ev.type = MidiEventType::MetaOther;
            events_.push_back(ev);
            break;
        }
    }
    else if (statusByte == 0xF0 || statusByte == 0xF7) {
        // SysEx: ペイロードを保持
        u32 length = r.ReadVLQ();
        ev.payload.reserve(length);
        for (u32 i = 0; i < length; ++i) {
            ev.payload.push_back(r.ReadU8());
        }
        ev.type = MidiEventType::SysEx;
        events_.push_back(ev);
    }
    else {
        ev.type = static_cast<MidiEventType>(msgType);

        switch (msgType) {
        case 0x80: // Note Off
            ev.data1 = r.ReadU8(); // note
            ev.data2 = r.ReadU8(); // velocity
            ev.velocity16 = Scale7To16(ev.data2);
            events_.push_back(ev);
            break;
        case 0x90: // Note On (velocity=0 は NoteOff と同義)
            ev.data1 = r.ReadU8();
            ev.data2 = r.ReadU8();
            if (ev.data2 == 0)
                ev.type = MidiEventType::NoteOff;
            ev.velocity16 = Scale7To16(ev.data2);
            events_.push_back(ev);
            break;
        case 0xA0: // Poly Pressure
            ev.data1 = r.ReadU8();
            ev.data2 = r.ReadU8();
            ev.value32 = Scale7To32(ev.data2);
            events_.push_back(ev);
            break;
        case 0xB0: // Control Change
            ev.data1 = r.ReadU8();
            ev.data2 = r.ReadU8();
            ev.value32 = Scale7To32(ev.data2);
            events_.push_back(ev);
            break;
        case 0xC0: // Program Change
            ev.data1 = r.ReadU8();
            events_.push_back(ev);
            break;
        case 0xD0: // Channel Pressure
            ev.data1 = r.ReadU8();
            ev.value32 = Scale7To32(ev.data1);
            events_.push_back(ev);
            break;
        case 0xE0: { // Pitch Bend
            ev.data1 = r.ReadU8(); // LSB
            ev.data2 = r.ReadU8(); // MSB
            const u16 bend14 = static_cast<u16>((ev.data2 << 7) | ev.data1); // 0-16383
            ev.value32 = Scale14To32(bend14);
            events_.push_back(ev);
            break;
        }
        default:
            errorMsg_ = "Unknown MIDI status byte";
            return false;
        }
    }
    return true;
}

} // namespace XArkMidi

