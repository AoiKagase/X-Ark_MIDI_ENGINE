#pragma once
#include "VoicePool.h"
#include "Channel.h"
#include "../midi/MidiSequencer.h"
#include "../soundbank/SoundBank.h"
#include <string>
#include <vector>

namespace XArkMidi {

class Synthesizer {
public:
    bool Init(const MidiFile* midi, const SoundBank* soundBank,
              u32 sampleRate, u32 numChannels);

    // PCM データを buf に書き込む
    // buf には numFrames * numChannels * sizeof(int16_t) バイト以上を確保すること
    // 戻り値: 実際に書き込んだフレーム数（曲末尾では numFrames より小さくなる場合がある）
    u32 Render(i16* buf, u32 numFrames);

    bool IsFinished() const;

    const std::string& ErrorMessage() const { return errorMsg_; }

private:
    const SoundBank* soundBank_ = nullptr;
    u32             sampleRate_ = 44100;
    u32             numChannels_= 2;

    MidiSequencer   sequencer_;
    ChannelState    channels_[MIDI_CHANNEL_COUNT];
    VoicePool       voicePool_;

    bool            finished_        = false;
    bool            seqEndNotified_  = false; // シーケンサー終了時の AllNotesOff 送信済みフラグ
    std::string     errorMsg_;

    std::vector<f32> reverbDelayL_;
    std::vector<f32> reverbDelayR_;
    size_t           reverbIndex_ = 0;
    size_t           reverbTap1_ = 0;
    size_t           reverbTap2_ = 0;
    size_t           reverbTap3_ = 0;
    size_t           reverbTap4_ = 0;
    std::vector<f32> chorusDelayL_;
    std::vector<f32> chorusDelayR_;
    size_t           chorusIndex_ = 0;
    size_t           chorusBaseTapL_ = 0;
    size_t           chorusBaseTapR_ = 0;
    size_t           chorusDepthTapL_ = 0;
    size_t           chorusDepthTapR_ = 0;
    f32              chorusSin_ = 0.0f;
    f32              chorusCos_ = 1.0f;
    f32              mixGainCurrent_ = 1.0f;
    f32              masterVolume_ = 1.0f;
    f32              normGain_ = 1.0f;     // サンプルPCM正規化の補正ゲイン
    f32              gsReverbWetScale_ = 1.0f;
    f32              gsReverbFeedbackScale_ = 1.0f;
    f32              gsMasterReverbSendScale_ = 1.0f;
    f32              gsChorusWetScale_ = 1.0f;
    f32              gsChorusFeedbackScale_ = 1.0f;
    f32              gsChorusToReverbScale_ = 1.0f;
    f32              gsChorusDelayScale_ = 1.0f;
    f32              gsChorusDepthScale_ = 1.0f;
    f32              gsChorusRateScale_ = 1.0f;
    f32              dcBlockPrevInL_ = 0.0f;
    f32              dcBlockPrevInR_ = 0.0f;
    f32              dcBlockPrevOutL_ = 0.0f;
    f32              dcBlockPrevOutR_ = 0.0f;
    std::vector<f32> dryBlockL_;
    std::vector<f32> dryBlockR_;
    std::vector<f32> reverbBlockL_;
    std::vector<f32> reverbBlockR_;
    std::vector<f32> chorusBlockL_;
    std::vector<f32> chorusBlockR_;
    std::vector<ResolvedZone> zoneScratch_;

    void HandleEvent(const MidiEvent& ev);
    void HandleNoteOn(u8 ch, u8 key, u8 vel);
    void HandleNoteOff(u8 ch, u8 key);
    void HandleControlChange(u8 ch, u8 cc, u8 val);
    void HandleProgramChange(u8 ch, u8 program);
    void HandlePolyPressure(u8 ch, u8 key, u8 pressure);
    void HandleChannelPressure(u8 ch, u8 pressure);
    void HandlePitchBend(u8 ch, i16 bend);
    void HandleSysEx(const MidiEvent& ev);
    void RefreshSf2ControllersForChannel(u8 ch);
    bool HasAudibleEffectTail() const;
    void ResetGsEffectState();
};

} // namespace XArkMidi

