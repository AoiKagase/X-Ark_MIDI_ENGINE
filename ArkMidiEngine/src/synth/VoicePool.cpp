#include "VoicePool.h"
#include "SimdKernels.h"
#include <cmath>
#include <limits>

namespace ArkMidi {

namespace {

f32 EstimateVoiceAudibility(const Voice& voice) {
    const f32 channelGain = std::max(voice.channelGainL, voice.channelGainR);
    return voice.envLevel * voice.attenuation * channelGain;
}

bool IsSustainHeldOnly(const Voice& voice) {
    return (voice.sustainHeld || voice.sostenutoHeld) &&
           voice.envPhase != EnvPhase::Release && voice.envPhase != EnvPhase::Off;
}

bool IsSameNote(const Voice& voice, u8 channel, u8 key) {
    return voice.channel == channel && voice.noteKey == key;
}

}

VoicePool::VoicePool() {
    activeSlots_.fill(-1);
    const u32 hwThreads = std::thread::hardware_concurrency();
    const u32 workerCount = (hwThreads > 1) ? std::min<u32>(hwThreads - 1, 7u) : 0u;
    workers_.reserve(workerCount);
    workerScratch_.resize(workerCount);
    workerRanges_.resize(workerCount);
    for (u16 i = 0; i < workerCount; ++i) {
        workers_.emplace_back(&VoicePool::WorkerLoop, this, i);
    }
}

VoicePool::~VoicePool() {
    {
        std::lock_guard<std::mutex> lock(workerMutex_);
        stopWorkers_ = true;
        ++workerGeneration_;
    }
    workerCv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

Voice* VoicePool::AllocVoice(u8 channel, u8 key) {
    // 1. 空きボイスを探す
    for (u16 i = 0; i < MAX_VOICES; ++i) {
        if (!voices_[i].active) return &voices_[i];
    }

    auto pickVoice = [&](auto&& predicate) -> Voice* {
        Voice* steal = nullptr;
        f32 minAudibility = std::numeric_limits<f32>::max();
        u32 oldestNoteId = std::numeric_limits<u32>::max();
        for (u16 i = 0; i < activeCount_; ++i) {
            auto& v = voices_[activeIndices_[i]];
            if (!predicate(v)) {
                continue;
            }
            const f32 audibility = EstimateVoiceAudibility(v);
            if (!steal || audibility < minAudibility - 1.0e-6f ||
                (std::fabs(audibility - minAudibility) <= 1.0e-6f && v.noteId < oldestNoteId)) {
                steal = &v;
                minAudibility = audibility;
                oldestNoteId = v.noteId;
            }
        }
        if (!steal) {
            return nullptr;
        }
        const u16 index = static_cast<u16>(steal - voices_);
        steal->Kill();
        UntrackVoice(index);
        return steal;
    };

    if (Voice* releaseVoice = pickVoice([](const Voice& v) { return v.envPhase == EnvPhase::Release; })) {
        return releaseVoice;
    }

    if (Voice* quietUnsustained = pickVoice([](const Voice& v) {
            return !v.sustainHeld && v.envPhase != EnvPhase::Off && v.envPhase != EnvPhase::Release;
        })) {
        return quietUnsustained;
    }

    {
        Voice* steal = nullptr;
        u32 oldestNoteId = std::numeric_limits<u32>::max();
        for (u16 i = 0; i < activeCount_; ++i) {
            auto& v = voices_[activeIndices_[i]];
            if (!IsSameNote(v, channel, key) || v.envPhase == EnvPhase::Release || v.envPhase == EnvPhase::Off) {
                continue;
            }
            if (!steal || v.noteId < oldestNoteId) {
                steal = &v;
                oldestNoteId = v.noteId;
            }
        }
        if (steal) {
            const u16 index = static_cast<u16>(steal - voices_);
            steal->Kill();
            UntrackVoice(index);
            return steal;
        }
    }

    if (Voice* quietAny = pickVoice([](const Voice& v) { return v.envPhase != EnvPhase::Off; })) {
        return quietAny;
    }

    return nullptr;
}

void VoicePool::NoteOn(const std::vector<ResolvedZone>& zones, const i16* sampleData, size_t sampleDataSize,
                        u16 bank, u8 channel, u8 program, u8 key, u8 velocity, u32 sampleRate,
                        f64 pitchBendSemitones, u8 exclusiveClass,
                        f32 volumeFactor, u8 pan, u8 reverbSend, u8 chorusSend, SoundBankKind soundBankKind,
                        i32 portamentoSourceKey, u8 portamentoTime) {
    if (zones.empty()) {
        return;
    }
    // サスティンペダル保留中に同一キーが再トリガーされた場合、古いボイスをRelease移行させる
    // （ボイスの蓄積防止。BASSMIDI互換の挙動）
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel == channel && v.noteKey == key && (v.sustainHeld || v.sostenutoHeld)) {
            v.sustainHeld = false;
            v.sostenutoHeld = false;
            v.sostenutoLatched = false;
            v.NoteOff();
        }
    }
    noteQueue_[channel][key].clear();

    // ExclusiveClass が非ゼロなら同クラスのボイスを停止
    if (exclusiveClass != 0)
        KillExclusiveClass(channel, exclusiveClass);

    u32 noteId = nextNoteId_++;
    noteQueue_[channel][key].push_back(noteId);
    for (const auto& zone : zones) {
        Voice* v = AllocVoice(channel, key);
        if (!v) break; // ボイス確保失敗
        const u16 voiceIndex = static_cast<u16>(v - voices_);
        v->NoteOn(zone, sampleData, sampleDataSize, bank, channel, program, key, velocity, noteId, sampleRate,
                  pitchBendSemitones, soundBankKind, portamentoSourceKey, portamentoTime);
        if (v->active) {
            v->UpdateChannelMix(volumeFactor, pan, reverbSend, chorusSend);
            v->exclusiveClass = exclusiveClass;
            TrackVoice(voiceIndex);
        }
    }
}

void VoicePool::NoteOff(u8 channel, u8 key, bool sustain) {
    auto& queue = noteQueue_[channel][key];
    while (!queue.empty() && !HasActiveNote(channel, key, queue.front())) {
        queue.pop_front();
    }

    if (queue.empty()) {
        return;
    }

    const u32 targetNoteId = queue.front();
    queue.pop_front();

    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel || v.noteKey != key || v.noteId != targetNoteId) continue;
        if (v.envPhase == EnvPhase::Release || v.envPhase == EnvPhase::Off) continue;
        if (sustain) {
            v.sustainHeld = true;
        } else if (v.sostenutoLatched) {
            v.sostenutoHeld = true;
        } else {
            v.NoteOff();
        }
    }
}

void VoicePool::ReleaseSustained(u8 channel) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel || !v.sustainHeld) continue;
        v.sustainHeld = false;
        if (v.sostenutoHeld) {
            continue;
        }
        v.NoteOff();
    }
}

void VoicePool::SetSostenuto(u8 channel, bool enabled) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel) {
            continue;
        }
        if (enabled) {
            if (v.envPhase != EnvPhase::Release && v.envPhase != EnvPhase::Off) {
                v.sostenutoLatched = true;
            }
            continue;
        }

        const bool releaseHeld = v.sostenutoHeld && !v.sustainHeld;
        v.sostenutoLatched = false;
        v.sostenutoHeld = false;
        if (releaseHeld) {
            v.NoteOff();
        }
    }
}

void VoicePool::KillExclusiveClass(u8 channel, u8 excClass) {
    for (u16 i = 0; i < activeCount_;) {
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (v.channel != channel || v.exclusiveClass != excClass || excClass == 0) {
            ++i;
            continue;
        }
        v.Kill();
        UntrackVoice(voiceIndex);
    }
}

void VoicePool::AllNotesOff(u8 channel) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel) continue;
        v.sustainHeld = false;
        v.sostenutoHeld = false;
        v.sostenutoLatched = false;
        v.NoteOff();
    }
    for (auto& queue : noteQueue_[channel]) {
        queue.clear();
    }
}

void VoicePool::AllSoundOff(u8 channel) {
    for (u16 i = 0; i < activeCount_;) {
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (v.channel != channel) {
            ++i;
            continue;
        }
        v.Kill();
        UntrackVoice(voiceIndex);
    }
    for (auto& queue : noteQueue_[channel]) {
        queue.clear();
    }
}

int VoicePool::RenderSample(f32& outL, f32& outR, f32& reverbL, f32& reverbR, f32& chorusL, f32& chorusR) {
    u16 write = 0;
    for (u16 read = 0; read < activeCount_; ++read) {
        const u16 voiceIndex = activeIndices_[read];
        auto& v = voices_[voiceIndex];
        if (!v.active) {
            activeSlots_[voiceIndex] = -1;
            continue;
        }
        v.Render(outL, outR, reverbL, reverbR, chorusL, chorusR);
        if (!v.active) {
            activeSlots_[voiceIndex] = -1;
            continue;
        }
        activeIndices_[write] = voiceIndex;
        activeSlots_[voiceIndex] = static_cast<i16>(write);
        ++write;
    }
    activeCount_ = write;
    return activeCount_;
}

int VoicePool::RenderBlock(f32* outL, f32* outR, f32* reverbL, f32* reverbR, f32* chorusL, f32* chorusR, u32 numFrames) {
    const u16 localActiveCount = activeCount_;
    const bool useParallel = !workers_.empty() && localActiveCount >= 24 && numFrames >= 256;

    if (useParallel) {
        const u16 totalTasks = std::min<u16>(static_cast<u16>(workers_.size() + 1), localActiveCount);
        const u16 backgroundTasks = totalTasks - 1;
        const u16 chunkSize = static_cast<u16>((localActiveCount + totalTasks - 1) / totalTasks);

        for (u16 worker = 0; worker < backgroundTasks; ++worker) {
            const u16 begin = static_cast<u16>(worker * chunkSize);
            const u16 end = std::min<u16>(localActiveCount, static_cast<u16>(begin + chunkSize));
            workerRanges_[worker] = { begin, end };
            EnsureWorkerScratch(worker, numFrames);
        }

        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            workerNumFrames_ = numFrames;
            activeWorkerCount_ = backgroundTasks;
            completedWorkers_ = 0;
            ++workerGeneration_;
        }
        workerCv_.notify_all();

        const u16 mainBegin = static_cast<u16>(backgroundTasks * chunkSize);
        for (u16 read = mainBegin; read < localActiveCount; ++read) {
            const u16 voiceIndex = activeIndices_[read];
            auto& v = voices_[voiceIndex];
            if (!v.active) {
                continue;
            }
            v.RenderBlock(outL, outR, reverbL, reverbR, chorusL, chorusR, numFrames);
        }

        if (backgroundTasks > 0) {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerDoneCv_.wait(lock, [&]() { return completedWorkers_ == activeWorkerCount_; });
        }

        for (u16 worker = 0; worker < backgroundTasks; ++worker) {
            auto& scratch = workerScratch_[worker];
            Simd::AccumulateSix(
                outL, outR, reverbL, reverbR, chorusL, chorusR,
                scratch.outL.data(), scratch.outR.data(),
                scratch.reverbL.data(), scratch.reverbR.data(),
                scratch.chorusL.data(), scratch.chorusR.data(),
                numFrames);
        }
    } else {
        for (u16 read = 0; read < localActiveCount; ++read) {
            const u16 voiceIndex = activeIndices_[read];
            auto& v = voices_[voiceIndex];
            if (!v.active) {
                activeSlots_[voiceIndex] = -1;
                continue;
            }
            v.RenderBlock(outL, outR, reverbL, reverbR, chorusL, chorusR, numFrames);
        }
    }

    u16 write = 0;
    for (u16 read = 0; read < localActiveCount; ++read) {
        const u16 voiceIndex = activeIndices_[read];
        auto& v = voices_[voiceIndex];
        if (!v.active) {
            activeSlots_[voiceIndex] = -1;
            continue;
        }
        activeIndices_[write] = voiceIndex;
        activeSlots_[voiceIndex] = static_cast<i16>(write);
        ++write;
    }
    activeCount_ = write;
    return activeCount_;
}

void VoicePool::UpdatePitchBend(u8 channel, f64 pitchBendSemitones) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel) continue;
        v.UpdatePitchBend(pitchBendSemitones);
    }
}

void VoicePool::UpdateChannelPitch(u8 channel, const ChannelState& state) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel) continue;
        v.UpdatePitchBend(state.isDrum ? 0.0 : state.TotalPitchSemitonesForKey(v.noteKey));
    }
}

void VoicePool::UpdateChannelMix(u8 channel, f32 volumeFactor, u8 pan, u8 reverbSend, u8 chorusSend) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel) continue;
        v.UpdateChannelMix(volumeFactor, pan, reverbSend, chorusSend);
    }
}

void VoicePool::RefreshSf2Controllers(u8 channel, const SoundBank& soundBank, const ModulatorContext& ctx,
                                      f32 volumeFactor, u8 pan, u8 reverbSend, u8 chorusSend) {
    std::vector<ResolvedZone> zones;
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel || !v.active) continue;
        zones.clear();
        if (!soundBank.FindZones(v.bank, v.program, v.noteKey, v.velocity, zones, &ctx)) {
            v.UpdateChannelMix(volumeFactor, pan, reverbSend, chorusSend);
            continue;
        }
        for (const auto& zone : zones) {
            if (!v.MatchesResolvedZone(zone)) {
                continue;
            }
            v.RefreshResolvedZoneControllers(zone);
            break;
        }
        v.UpdateChannelMix(volumeFactor, pan, reverbSend, chorusSend);
    }
}

int VoicePool::ActiveCount() const {
    return activeCount_;
}

bool VoicePool::HasActiveNote(u8 channel, u8 key, u32 noteId) const {
    for (u16 i = 0; i < activeCount_; ++i) {
        const auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel || v.noteKey != key || v.noteId != noteId) continue;
        if (v.envPhase == EnvPhase::Release || v.envPhase == EnvPhase::Off) continue;
        return true;
    }
    return false;
}

void VoicePool::TrackVoice(u16 index) {
    if (activeSlots_[index] >= 0) {
        return;
    }
    activeIndices_[activeCount_] = index;
    activeSlots_[index] = static_cast<i16>(activeCount_);
    ++activeCount_;
}

void VoicePool::UntrackVoice(u16 index) {
    const i16 slot = activeSlots_[index];
    if (slot < 0) {
        return;
    }

    const u16 lastSlot = static_cast<u16>(activeCount_ - 1);
    const u16 lastIndex = activeIndices_[lastSlot];
    activeIndices_[slot] = lastIndex;
    activeSlots_[lastIndex] = slot;
    activeSlots_[index] = -1;
    --activeCount_;
}

void VoicePool::WorkerLoop(u16 workerIndex) {
    u64 observedGeneration = 0;
    while (true) {
        WorkerRange range{};
        u32 numFrames = 0;
        {
            std::unique_lock<std::mutex> lock(workerMutex_);
            workerCv_.wait(lock, [&]() { return stopWorkers_ || workerGeneration_ != observedGeneration; });
            if (stopWorkers_) {
                return;
            }
            observedGeneration = workerGeneration_;
            if (workerIndex >= activeWorkerCount_) {
                continue;
            }
            range = workerRanges_[workerIndex];
            numFrames = workerNumFrames_;
        }

        auto& scratch = workerScratch_[workerIndex];
        std::fill_n(scratch.outL.data(), numFrames, 0.0f);
        std::fill_n(scratch.outR.data(), numFrames, 0.0f);
        std::fill_n(scratch.reverbL.data(), numFrames, 0.0f);
        std::fill_n(scratch.reverbR.data(), numFrames, 0.0f);
        std::fill_n(scratch.chorusL.data(), numFrames, 0.0f);
        std::fill_n(scratch.chorusR.data(), numFrames, 0.0f);

        for (u16 read = range.begin; read < range.end; ++read) {
            const u16 voiceIndex = activeIndices_[read];
            auto& v = voices_[voiceIndex];
            if (!v.active) {
                continue;
            }
            v.RenderBlock(
                scratch.outL.data(), scratch.outR.data(),
                scratch.reverbL.data(), scratch.reverbR.data(),
                scratch.chorusL.data(), scratch.chorusR.data(),
                numFrames);
        }

        {
            std::lock_guard<std::mutex> lock(workerMutex_);
            ++completedWorkers_;
            if (completedWorkers_ == activeWorkerCount_) {
                workerDoneCv_.notify_one();
            }
        }
    }
}

void VoicePool::EnsureWorkerScratch(u16 workerIndex, u32 numFrames) {
    auto& scratch = workerScratch_[workerIndex];
    if (scratch.outL.size() < numFrames) {
        scratch.outL.resize(numFrames);
        scratch.outR.resize(numFrames);
        scratch.reverbL.resize(numFrames);
        scratch.reverbR.resize(numFrames);
        scratch.chorusL.resize(numFrames);
        scratch.chorusR.resize(numFrames);
    }
}

} // namespace ArkMidi
