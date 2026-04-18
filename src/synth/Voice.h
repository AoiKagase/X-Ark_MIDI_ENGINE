/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../sf2/Sf2Types.h"
#include "../soundbank/SoundBank.h"
#include "Interpolator.h"
#include <cmath>

namespace XArkMidi {

struct SynthCompatOptions {
    bool sf2ZeroLengthLoopRetrigger = false;
};

struct SpecialVoiceRoute {
    bool enabled = false;
    f64 detuneSemitones = 0.0;
    f32 pan = 0.0f;
    bool clampAboveRoot = false;
    i32 clampRootKey = -1;
};

enum class EnvPhase : u8 {
    Delay,
    Attack,
    Hold,
    Decay,
    Sustain,
    Release,
    Off,
};

class Voice {
public:
    static constexpr u16 kInvalidLinkedVoice = 0xFFFF;

    bool    active   = false;
    u16     bank     = 0;
    u8      channel  = 0;
    u8      program  = 0;
    u8      noteKey  = 0;
    u16     velocity = 0;
    u32     noteId   = 0;
    u8      exclusiveClass = 0;
    bool    sustainHeld = false; // Sustain Pedal 保留中
    bool    sostenutoLatched = false;
    bool    sostenutoHeld = false;

    // サンプル再生状態
    i64     samplePosFixed = 0;
    i64     sampleStepFixed = 0;
    f64     baseSampleStep = 0.0; // ピッチベンドなしのsampleStep（リアルタイム更新用）
    f64     pitchBendSemitones = 0.0;     // チャンネルレベルのピッチベンド
    f64     perNotePitchSemitones = 0.0;  // MIDI 2.0 Per-Note Pitch Bend 寄与分
    f64     portamentoOffsetSemitones = 0.0;
    f64     portamentoStepSemitones = 0.0;
    u32     portamentoSamplesRemaining = 0;
    u32     loopStart  = 0;
    u32     loopEnd    = 0;
    u32     sampleEnd  = 0;
    i64     loopStartFixed = 0;
    i64     loopEndFixed = 0;
    i64     sampleEndFixed = 0;
    bool    looping    = false;
    bool    loopUntilRelease = false;
    bool    usesLoopFallback = false;
    bool    ignoreNoteOffUntilSampleEnd = false;

    // ボリュームエンベロープ
    EnvPhase envPhase        = EnvPhase::Off;
    f32      envLevel        = 0.0f;  // 現在の振幅 [0.0, 1.0]
    f32      envAttackRate   = 0.0f;  // 1サンプルあたりの増分
    u32      envDelayEnd     = 0;     // Delay フェーズのサンプル数
    u32      envHoldEnd      = 0;     // Hold フェーズのサンプル数
    f32      envDecayRate    = 0.0f;  // 1サンプルあたりの乗算係数
    f32      envSustainLevel = 0.0f;  // Sustain 振幅 [0.0, 1.0]
    f32      envReleaseRate  = 0.0f;  // 1サンプルあたりの乗算係数
    f32      envReleaseTimeSeconds = 0.0f;
    u32      envSampleCount  = 0;     // 現在フェーズでの経過サンプル数

    // モジュレーションエンベロープ
    EnvPhase modEnvPhase        = EnvPhase::Off;
    f32      modEnvLevel        = 0.0f;
    f32      modEnvAttackRate   = 0.0f;
    u32      modEnvDelayEnd     = 0;
    u32      modEnvHoldEnd      = 0;
    f32      modEnvDecayRate    = 0.0f;
    f32      modEnvSustainLevel = 0.0f;
    f32      modEnvReleaseRate  = 0.0f;
    f32      modEnvReleaseTimeSeconds = 0.0f;
    u32      modEnvSampleCount  = 0;
    f32      modEnvToPitchCents = 0.0f;

    // LFO
    u32      modLfoDelayEnd = 0;
    u32      modLfoSampleCount = 0;
    f32      modLfoPhase = 0.0f;
    f32      modLfoPhaseStep = 0.0f;
    f32      modLfoToPitchCents = 0.0f;
    f32      modLfoToFilterFcCents = 0.0f;
    f32      modLfoToVolumeCb = 0.0f;
    u32      vibLfoDelayEnd = 0;
    u32      vibLfoSampleCount = 0;
    f32      vibLfoPhase = 0.0f;
    f32      vibLfoPhaseStep = 0.0f;
    f32      vibLfoToPitchCents = 0.0f;

    // 減衰量（[0.0, 1.0] 線形振幅）
    f32      attenuation = 1.0f;
    SoundBankKind soundBankKind = SoundBankKind::Sf2;
    bool     filterEnabled = false;
    f32      filterB0 = 1.0f;
    f32      filterB1 = 0.0f;
    f32      filterB2 = 0.0f;
    f32      filterA1 = 0.0f;
    f32      filterA2 = 0.0f;
    f32      filterZ1 = 0.0f;
    f32      filterZ2 = 0.0f;
    i32      filterBaseFcCents = 13500;
    i32      filterVelocityFcCents = 0;
    i32      filterQCb = 0;
    i32      filterModEnvToFcCents = 0;
    i32      filterCurrentFcCents = 13500;
    bool     useModEnv = false;
    u32      outputSampleRate = 44100;

    // ミキシングパラメーター
    f32      baseGainL = 0.5f;
    f32      baseGainR = 0.5f;
    f32      channelGainL = 1.0f;
    f32      channelGainR = 1.0f;
    f32      presetReverbSend = 0.0f;
    f32      presetChorusSend = 0.0f;
    f32      channelReverbSend = 0.0f;
    f32      channelChorusSend = 0.0f;
    f32      reverbSend = 0.0f;
    f32      chorusSend = 0.0f;
    f32      dryGainL = 0.5f;
    f32      dryGainR = 0.5f;
    f32      reverbGainL = 0.0f;
    f32      reverbGainR = 0.0f;
    f32      chorusGainL = 0.0f;
    f32      chorusGainR = 0.0f;
    const SampleHeader* sampleHeader = nullptr;
    SpecialVoiceRoute specialRoute;
    bool     ownedByParent = false;
    u16      linkedVoiceIndex = kInvalidLinkedVoice;

    // サンプルデータ参照（非所有）
    const i16* sampleData     = nullptr;
    size_t     sampleDataSize = 0;      // LinearInterp 境界チェック用

    // NoteOn で初期化
    void NoteOn(const ResolvedZone& zone, const i16* pcmData, size_t pcmDataSize,
                u16 bankNumber, u8 ch, u8 programNumber, u8 key, u16 vel, u32 noteId, u32 sampleRate, f64 pitchBendSemitones,
                SoundBankKind soundBankKind, const SynthCompatOptions& compatOptions,
                const SpecialVoiceRoute& specialRoute = {},
                i32 portamentoSourceKey = -1, u8 portamentoTime = 0);
    bool MatchesResolvedZone(const ResolvedZone& zone) const { return sampleHeader == zone.sample; }
    bool HasLinkedVoice() const { return linkedVoiceIndex != kInvalidLinkedVoice; }
    void LinkVoice(u16 index) { linkedVoiceIndex = index; }
    void ClearLinkedVoice() { linkedVoiceIndex = kInvalidLinkedVoice; }
    void RefreshResolvedZoneControllers(const ResolvedZone& zone);

    // チャンネルレベルのピッチベンドをリアルタイムで更新（per-note 分を加算）
    void UpdatePitchBend(f64 channelPitchSemitones) {
        this->pitchBendSemitones = channelPitchSemitones;
        const f64 sampleStep = baseSampleStep * pow(2.0, (channelPitchSemitones + perNotePitchSemitones) / 12.0);
        sampleStepFixed = static_cast<i64>(std::llround(sampleStep * 4294967296.0));
    }

    // MIDI 2.0 Per-Note Pitch Bend をリアルタイムで更新
    void UpdatePerNotePitchBend(f64 semitones) {
        perNotePitchSemitones = semitones;
        const f64 sampleStep = baseSampleStep * pow(2.0, (pitchBendSemitones + semitones) / 12.0);
        sampleStepFixed = static_cast<i64>(std::llround(sampleStep * 4294967296.0));
    }

    void UpdateChannelMix(f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32);

    // NoteOff で Release フェーズへ移行
    void NoteOff();

    // 強制停止
    void Kill() {
        active = false;
        envPhase = EnvPhase::Off;
        ownedByParent = false;
        linkedVoiceIndex = kInvalidLinkedVoice;
    }

    // 1フレームレンダリング（outL, outR に加算）
    void Render(f32& outL, f32& outR, f32& reverbL, f32& reverbR, f32& chorusL, f32& chorusR);
    void RenderBlock(f32* outL, f32* outR, f32* reverbL, f32* reverbR, f32* chorusL, f32* chorusR, u32 numFrames);

    bool IsFinished() const { return envPhase == EnvPhase::Off; }

private:
    void ApplyPan(f32 pan);
    void RefreshOutputGains();
};

} // namespace XArkMidi

