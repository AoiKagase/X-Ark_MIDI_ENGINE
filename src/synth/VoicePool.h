/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "Voice.h"
#include "Channel.h"
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace XArkMidi {

class VoicePool {
public:
    VoicePool();
    ~VoicePool();

    VoicePool(const VoicePool&) = delete;
    VoicePool& operator=(const VoicePool&) = delete;

    // NoteOn: ゾーンリストから各ゾーンにボイスを割り当てる
    void NoteOn(const std::vector<ResolvedZone>& zones, const i16* sampleData, const i32* sampleData24, size_t sampleDataSize,
                u16 bank, u8 channel, u8 program, u8 key, u16 velocity, u32 sampleRate,
                f64 pitchBendSemitones,
                f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32, SoundBankKind soundBankKind,
                const SynthCompatOptions& compatOptions,
                i32 portamentoSourceKey = -1, u8 portamentoTime = 0, bool softPedalActive = false);

    // NoteOff: 該当チャンネル・ノートのボイスを Release フェーズへ
    // sustain が true の場合は NoteOff を保留する
    void NoteOff(u8 channel, u8 key, bool sustain);

    // Sustain Pedal OFF: 保留中のボイスを全て Release
    void ReleaseSustained(u8 channel);
    void SetSostenuto(u8 channel, bool enabled);

    // ExclusiveClass のボイスを強制停止（ドラムの相互排他等）
    void KillExclusiveClass(u8 channel, u8 excClass);

    // チャンネルの全ボイスを即時停止
    void AllNotesOff(u8 channel);
    void AllSoundOff(u8 channel);

    // ピッチベンドをリアルタイム更新（チャンネルの全アクティブボイスに適用）
    void UpdatePitchBend(u8 channel, f64 pitchBendSemitones);
    void UpdateChannelPitch(u8 channel, const ChannelState& state);
    // MIDI 2.0 Per-Note Pitch Bend（特定ノートのボイスにのみ適用）
    void UpdatePerNotePitchBend(u8 channel, u8 key, f64 perNoteSemitones);
    void ResetPerNoteState(u8 channel, u8 key);
    void UpdateChannelMix(u8 channel, f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32);
    void RefreshSf2Controllers(u8 channel, const SoundBank& soundBank, const ModulatorContext& ctx,
                               f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32);

    // 全ボイスをレンダリング（outL, outR に加算）
    int RenderSample(f32& outL, f32& outR, f32& reverbL, f32& reverbR, f32& chorusL, f32& chorusR,
                     u32 audibleChannelMask = 0xFFFFu);
    int RenderBlock(f32* outL, f32* outR, f32* reverbL, f32* reverbR, f32* chorusL, f32* chorusR, u32 numFrames,
                    u32 audibleChannelMask = 0xFFFFu);

    // アクティブなボイス数
    int ActiveCount() const;
    void GetActiveRootNoteCountsPerChannel(std::array<u32, MIDI_CHANNEL_COUNT>& counts) const;
    void GetActiveRootKeyMasksPerChannel(std::array<std::array<u32, 4>, MIDI_CHANNEL_COUNT>& masks) const;

private:
    struct WorkerRange {
        u16 begin = 0;
        u16 end = 0;
    };

    struct WorkerScratch {
        std::vector<f32> outL;
        std::vector<f32> outR;
        std::vector<f32> reverbL;
        std::vector<f32> reverbR;
        std::vector<f32> chorusL;
        std::vector<f32> chorusR;
    };

    Voice voices_[MAX_VOICES];
    u32   nextNoteId_ = 1;
    std::deque<u32> noteQueue_[MIDI_CHANNEL_COUNT][128];
    std::array<u16, MAX_VOICES> activeIndices_{};
    std::array<i16, MAX_VOICES> activeSlots_{};
    u16 activeCount_ = 0;
    std::vector<std::thread> workers_;
    std::vector<WorkerScratch> workerScratch_;
    std::vector<WorkerRange> workerRanges_;
    std::mutex workerMutex_;
    std::condition_variable workerCv_;
    std::condition_variable workerDoneCv_;
    u64 workerGeneration_ = 0;
    u16 activeWorkerCount_ = 0;
    u16 completedWorkers_ = 0;
    u32 workerNumFrames_ = 0;
    u32 workerAudibleChannelMask_ = 0xFFFFu;
    bool stopWorkers_ = false;

    // ボイスを確保する（空きがなければスティール）
    Voice* AllocVoice(u8 channel, u8 key);
    bool HasActiveNote(u8 channel, u8 key, u32 noteId) const;
    static bool IsChannelAudible(u32 audibleChannelMask, u8 channel);
    void TrackVoice(u16 index);
    void UntrackVoice(u16 index);
    void WorkerLoop(u16 workerIndex);
    void EnsureWorkerScratch(u16 workerIndex, u32 numFrames);
};

} // namespace XArkMidi
