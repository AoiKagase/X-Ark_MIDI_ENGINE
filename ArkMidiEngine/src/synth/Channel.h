#pragma once
#include "../common/Types.h"
#include <cmath>
namespace ArkMidi {

struct ChannelState {
    u8   program    = 0;    // 0-127
    u16  bank       = 0;    // CC0(MSB)*128 + CC32(LSB)
    u8   bankMSB    = 0;    // CC0
    u8   bankLSB    = 0;    // CC32
    i16  pitchBend  = 0;    // -8192 ～ +8191
    u8   pitchBendRangeSemitones = 2; // RPN 0,0 (デフォルト ±2半音)
    u8   pitchBendRangeCents = 0;
    u8   volume     = 127;  // CC7
    u8   expression = 127;  // CC11
    u8   pan        = 64;   // CC10 (64=センター)
    u8   reverbSend = 40;   // CC91 (GM/BASSMIDI default)
    u8   chorusSend = 0;    // CC93
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
    f64 PitchBendSemitones() const {
        const f64 range = static_cast<f64>(pitchBendRangeSemitones) +
                          static_cast<f64>(pitchBendRangeCents) / 100.0;
        return static_cast<f64>(pitchBend) / 8192.0 * range;
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
    // SF2/GM準拠: CC7・CC11 は (v/127)^2 の2乗カーブ
    f32 VolumeFactor() const {
        const f32 v = static_cast<f32>(volume)  / 127.0f;
        const f32 e = static_cast<f32>(expression) / 127.0f;
        return v * v * e * e;
    }

    void Reset() {
        program    = 0;
        bank       = 0;
        bankMSB    = 0;
        bankLSB    = 0;
        pitchBend  = 0;
        pitchBendRangeSemitones = 2;
        pitchBendRangeCents = 0;
        volume     = 127;
        expression = 127;
        pan        = 64;
        reverbSend = 40;
        chorusSend = 0;
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
        ccValues[7] = volume;
        ccValues[10] = pan;
        ccValues[11] = expression;
        ccValues[91] = reverbSend;
        ccValues[93] = chorusSend;
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

} // namespace ArkMidi
