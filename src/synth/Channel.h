/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/Types.h"
#include <cmath>
namespace XArkMidi {

struct ChannelState {
    u8   program    = 0;    // 0-127
    u16  bank       = 0;    // CC0(MSB)*128 + CC32(LSB)
    u8   bankMSB    = 0;    // CC0
    u8   bankLSB    = 0;    // CC32
    // MIDI 2.0 対応: 32-bit pitch bend (center=0x80000000)
    // MIDI 1.0 由来の場合は 14-bit からビットレプリケーションでアップスケール済み
    u32  pitchBend32 = 0x80000000u; // center=0
    u8   pitchBendRangeSemitones = 2; // RPN 0,0 (デフォルト ±2半音)
    u8   pitchBendRangeCents = 0;
    // MIDI 2.0 対応: 32-bit volume/expression
    // MIDI 1.0 由来の場合は 7-bit からビットレプリケーションでアップスケール済み
    // Scale7To32(127) = 0xFFFFFFFF (full volume)
    u32  volume32     = 0xFFFFFFFFu; // CC7
    u32  expression32 = 0xFFFFFFFFu; // CC11
    // MIDI 2.0 対応: 32-bit pan/reverbSend/chorusSend
    // MIDI 1.0 由来の場合は 7-bit からビットレプリケーションでアップスケール済み
    u32  pan32       = 0x81020408u; // CC10, Scale7To32(64), center
    u32  reverbSend32 = 0x50A14285u; // CC91, Scale7To32(40), GM/BASSMIDI default
    u32  chorusSend32 = 0u;          // CC93
    bool sustain    = false; // CC64 >= 64
    bool sostenuto  = false; // CC66 >= 64
    bool softPedal  = false; // CC67 >= 64
    bool portamento = false; // CC65 >= 64
    bool localControl = true; // CC122
    bool omniMode   = false; // CC124/125
    bool monoMode   = false; // CC126/127
    bool isDrum     = false; // channel == 9
    u8   channelPressure = 0;
    u8   polyPressure[128] = {};
    i16  noteTuningCents[128] = {};
    u8   portamentoTime = 0; // CC5
    u8   portamentoControlKey = 0xFF; // CC84
    u8   lastNoteKey = 0xFF;
    u8   ccValues[128] = {};
    u8   dataEntryMSB = 0;
    u8   dataEntryLSB = 0;

    // RPN 処理用
    u8   rpnMSB = 0x7F;
    u8   rpnLSB = 0x7F;
    u8   nrpnMSB = 0x7F;
    u8   nrpnLSB = 0x7F;
    i8   coarseTuningSemitones = 0; // RPN 0,2
    i16  fineTuningCents = 0;       // RPN 0,1
    u8   modulationDepthRangeSemitones = 0; // RPN 0,5
    u8   modulationDepthRangeCents = 0;

    // ピッチベンド量をセミトーン単位で返す
    // pitchBend32: center=0x80000000, range [0, 0xFFFFFFFF]
    f64 PitchBendSemitones() const {
        const f64 range = static_cast<f64>(pitchBendRangeSemitones) +
                          static_cast<f64>(pitchBendRangeCents) / 100.0;
        const f64 normalized =
            (static_cast<f64>(pitchBend32) - 2147483648.0) / 2147483648.0;
        return normalized * range;
    }

    // SF2 モジュレーション用: pitchBend32 を MIDI 1.0 互換の 14-bit i16 (-8192..+8191) に変換
    i16 PitchBend14() const {
        const i64 centered = static_cast<i64>(pitchBend32) - 0x80000000LL;
        return static_cast<i16>(centered * 8192LL / 0x80000000LL);
    }

    f64 ChannelTuningSemitones() const {
        return static_cast<f64>(coarseTuningSemitones) + static_cast<f64>(fineTuningCents) / 100.0;
    }

    f64 NoteTuningSemitones(u8 key) const {
        return static_cast<f64>(noteTuningCents[key]) / 100.0;
    }

    f64 TotalPitchSemitones() const {
        return PitchBendSemitones() + ChannelTuningSemitones();
    }

    f64 TotalPitchSemitonesForKey(u8 key) const {
        return TotalPitchSemitones() + NoteTuningSemitones(key);
    }

    // マスター音量係数 [0.0, 1.0]
    // SF2/GM準拠: CC7・CC11 は (v/max)^2 の2乗カーブ
    // 32-bit 精度: volume32/expression32 を使用
    f32 VolumeFactor() const {
        constexpr f32 kMax = 4294967295.0f; // 0xFFFFFFFF
        const f32 v = static_cast<f32>(volume32)     / kMax;
        const f32 e = static_cast<f32>(expression32) / kMax;
        return v * v * e * e;
    }

    void Reset() {
        program    = 0;
        bank       = 0;
        bankMSB    = 0;
        bankLSB    = 0;
        pitchBend32 = 0x80000000u; // center
        pitchBendRangeSemitones = 2;
        pitchBendRangeCents = 0;
        volume32     = 0xFFFFFFFFu; // Scale7To32(127)
        expression32 = 0xFFFFFFFFu;
        pan32       = 0x81020408u; // Scale7To32(64), center
        reverbSend32 = 0x50A14285u; // Scale7To32(40), GM/BASSMIDI default
        chorusSend32 = 0u;
        sustain    = false;
        sostenuto  = false;
        softPedal  = false;
        portamento = false;
        localControl = true;
        omniMode   = false;
        monoMode   = false;
        channelPressure = 0;
        portamentoTime = 0;
        portamentoControlKey = 0xFF;
        lastNoteKey = 0xFF;
        dataEntryMSB = 0;
        dataEntryLSB = 0;
        rpnMSB = rpnLSB = 0x7F;
        nrpnMSB = nrpnLSB = 0x7F;
        coarseTuningSemitones = 0;
        fineTuningCents = 0;
        modulationDepthRangeSemitones = 0;
        modulationDepthRangeCents = 0;
        for (int i = 0; i < 128; ++i) ccValues[i] = 0;
        for (int i = 0; i < 128; ++i) polyPressure[i] = 0;
        for (int i = 0; i < 128; ++i) noteTuningCents[i] = 0;
        ccValues[7]  = 127;  // volume32=0xFFFFFFFF の 7-bit 相当
        ccValues[10] = 64;   // pan32=Scale7To32(64) の 7-bit 相当
        ccValues[11] = 127;  // expression32=0xFFFFFFFF の 7-bit 相当
        ccValues[91] = 40;   // reverbSend32=Scale7To32(40) の 7-bit 相当
        ccValues[93] = 0;    // chorusSend32=0 の 7-bit 相当
        ccValues[64] = 0;
        ccValues[66] = 0;
        ccValues[67] = 0;
        ccValues[65] = 0;
        ccValues[5] = 0;
        ccValues[84] = 0;
        ccValues[122] = 127;
    }

    void ResetControllersOnly() {
        const u8 keepProgram = program;
        const u16 keepBank = bank;
        const u8 keepBankMSB = bankMSB;
        const u8 keepBankLSB = bankLSB;
        const bool keepIsDrum = isDrum;
        Reset();
        program = keepProgram;
        bank = keepBank;
        bankMSB = keepBankMSB;
        bankLSB = keepBankLSB;
        isDrum = keepIsDrum;
    }
};

} // namespace XArkMidi

