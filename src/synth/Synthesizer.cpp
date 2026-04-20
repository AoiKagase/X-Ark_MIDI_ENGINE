/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "Synthesizer.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace XArkMidi {

namespace {
constexpr f32 kChorusFeedback = 0.15f;
constexpr f32 kChorusWetMix = 0.28f;
constexpr f32 kChorusToReverb = 0.20f;
constexpr f32 kReverbFeedback = 0.50f;
constexpr f32 kReverbWetMix = 0.80f;   // 0.65→0.80: リバーブリターンを増量
constexpr f32 kMasterReverbSend = 0.20f; // 0.12→0.20: ドライ→リバーブへの送り増量
constexpr f32 kMixGainSmooth = 0.0025f;
constexpr f32 kMasterOutputGain = 1.00f; // リバーブ増量分を考慮して dry を抑制
constexpr f32 kChorusPhaseStepSin = 0.000369999991558f;
constexpr f32 kChorusPhaseStepCos = 0.999999940395f;
constexpr f32 kEffectTailThreshold = 1.0e-4f;
constexpr const char* kProgramDebugLogPath = "./diagnostics/program_focus.log";
constexpr const char* kProgramSummaryLogPath = "./diagnostics/program_summary.log";

const char* SoundBankKindName(SoundBankKind kind) {
    switch (kind) {
    case SoundBankKind::Sf2: return "sf2";
    case SoundBankKind::Dls: return "dls";
    default: return "auto";
    }
}

bool IsProgramLoggingEnabled() {
    static const bool enabled = []() {
#ifdef _WIN32
        char* value = nullptr;
        size_t len = 0;
        _dupenv_s(&value, &len, "XARKMIDI_ENABLE_PROGRAM_LOG");
        if (!(value && value[0] != '\0' && value[0] != '0')) {
            std::free(value);
            value = nullptr;
            _dupenv_s(&value, &len, "XARKMIDI_ENABLE_SF2_PROGRAM_LOG");
        }
        const bool isEnabled = (value && value[0] != '\0' && value[0] != '0');
        std::free(value);
        return isEnabled;
#else
        const char* v = std::getenv("XARKMIDI_ENABLE_PROGRAM_LOG");
        if (v && v[0] != '\0' && v[0] != '0')
            return true;
        v = std::getenv("XARKMIDI_ENABLE_SF2_PROGRAM_LOG");
        return v && v[0] != '\0' && v[0] != '0';
#endif
    }();
    return enabled;
}

f64 EffectiveSamplePitchCorrection(const SampleHeader* sample, const SynthCompatOptions& compatOptions) {
    if (!sample || !compatOptions.enableSf2SamplePitchCorrection) {
        return 0.0;
    }
    return static_cast<f64>(sample->pitchCorrection);
}

void ApplyChannelMix(VoicePool& voicePool, u8 ch, const ChannelState& state) {
    voicePool.UpdateChannelMix(ch, state.VolumeFactor(), state.pan32, state.reverbSend32, state.chorusSend32);
}

bool IsRpnSelected(const ChannelState& state) {
    return state.rpnMSB != 0x7F || state.rpnLSB != 0x7F;
}

bool IsNrpnSelected(const ChannelState& state) {
    return state.nrpnMSB != 0x7F || state.nrpnLSB != 0x7F;
}

void ApplyCurrentPitchToChannel(VoicePool& voicePool, u8 ch, const ChannelState& state) {
    voicePool.UpdateChannelPitch(ch, state);
}

void Increment14Bit(u8& msb, u8& lsb, u16 maxValue) {
    u16 value = static_cast<u16>((static_cast<u16>(msb) << 7) | lsb);
    value = std::min<u16>(maxValue, static_cast<u16>(value + 1));
    msb = static_cast<u8>((value >> 7) & 0x7F);
    lsb = static_cast<u8>(value & 0x7F);
}

void Decrement14Bit(u8& msb, u8& lsb) {
    u16 value = static_cast<u16>((static_cast<u16>(msb) << 7) | lsb);
    value = (value > 0) ? static_cast<u16>(value - 1) : 0;
    msb = static_cast<u8>((value >> 7) & 0x7F);
    lsb = static_cast<u8>(value & 0x7F);
}

void ApplyRpnValue(ChannelState& state) {
    if (state.rpnMSB != 0 || state.rpnLSB > 5) {
        return;
    }

    switch (state.rpnLSB) {
    case 0:
        state.pitchBendRangeSemitones = state.dataEntryMSB;
        state.pitchBendRangeCents = std::min<u8>(99, state.dataEntryLSB);
        break;
    case 1: {
        const i32 value14 = (static_cast<i32>(state.dataEntryMSB) << 7) | state.dataEntryLSB;
        const i32 centered = std::clamp(value14 - 8192, -8192, 8191);
        state.fineTuningCents = static_cast<i16>(std::clamp(centered * 100 / 8192, -100, 100));
        break;
    }
    case 2:
        state.coarseTuningSemitones =
            static_cast<i8>(std::clamp(static_cast<i32>(state.dataEntryMSB) - 64, -64, 63));
        break;
    case 5:
        state.modulationDepthRangeSemitones = state.dataEntryMSB;
        state.modulationDepthRangeCents = std::min<u8>(99, state.dataEntryLSB);
        break;
    default:
        break;
    }
}

bool ShouldLogProgram(u8 program) {
    (void)program;
    return IsProgramLoggingEnabled();
}

void ResetProgramDebugLog() {
    if (!IsProgramLoggingEnabled()) {
        return;
    }
    std::filesystem::create_directories(std::filesystem::path(kProgramDebugLogPath).parent_path());
    static bool initialized = false;
    std::ofstream log(kProgramDebugLogPath, initialized ? std::ios::app : std::ios::trunc);
    log << (initialized ? "\n--- synth init ---\n" : "X-ArkMidiEngine program diagnostics\n");
    std::ofstream summary(kProgramSummaryLogPath, initialized ? std::ios::app : std::ios::trunc);
    summary << (initialized ? "--- synth init ---\n" : "X-ArkMidiEngine note_on program summary\n");
    initialized = true;
}

void AppendProgramSummaryLog(u16 requestedBank,
                             u16 resolvedBank,
                             SoundBankKind bankKind,
                             u8 channel,
                             u8 program,
                             u8 key,
                             u16 velocity) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }
    std::ofstream log(kProgramSummaryLogPath, std::ios::app);
    if (!log) {
        return;
    }
    log << "note_on"
        << " bank_kind=" << SoundBankKindName(bankKind)
        << " ch=" << static_cast<int>(channel)
        << " program=" << static_cast<int>(program)
        << " key=" << static_cast<int>(key)
        << " vel=" << static_cast<int>(velocity)
        << " requested_bank=" << requestedBank
        << " resolved_bank=" << resolvedBank
        << '\n';
}

void AppendProgramMissLog(u16 requestedBank,
                          SoundBankKind bankKind,
                          u8 channel,
                          u8 program,
                          u8 key,
                          u16 velocity,
                          const ChannelState& state) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }
    std::ofstream log(kProgramSummaryLogPath, std::ios::app);
    if (!log) {
        return;
    }
    log << "note_on_miss"
        << " bank_kind=" << SoundBankKindName(bankKind)
        << " ch=" << static_cast<int>(channel)
        << " program=" << static_cast<int>(program)
        << " key=" << static_cast<int>(key)
        << " vel=" << static_cast<int>(velocity)
        << " requested_bank=" << requestedBank
        << " is_drum=" << (state.isDrum ? 1 : 0)
        << '\n';
}

void AppendProgramDebugLog(u16 requestedBank,
                           u16 resolvedBank,
                           SoundBankKind bankKind,
                           u8 channel,
                           u8 program,
                           u8 key,
                           u16 velocity,
                           u32 outputSampleRate,
                           const ChannelState& state,
                           const std::vector<ResolvedZone>& zones,
                           const SynthCompatOptions& compatOptions) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }
    std::ofstream log(kProgramDebugLogPath, std::ios::app);
    if (!log) {
        return;
    }

    log << "note_on"
        << " bank_kind=" << SoundBankKindName(bankKind)
        << " ch=" << static_cast<int>(channel)
        << " program=" << static_cast<int>(program)
        << " key=" << static_cast<int>(key)
        << " vel=" << static_cast<int>(velocity)
        << " requested_bank=" << requestedBank
        << " resolved_bank=" << resolvedBank
        << " channel_volume=" << static_cast<int>(state.volume32 >> 25)   // 32-bit→7-bit表示
        << " channel_expression=" << static_cast<int>(state.expression32 >> 25)
        << " channel_pan=" << static_cast<int>(state.pan32 >> 25)      // 32-bit→7-bit表示
        << " channel_reverb=" << static_cast<int>(state.reverbSend32 >> 25)
        << " channel_chorus=" << static_cast<int>(state.chorusSend32 >> 25)
        << " zones=" << zones.size()
        << '\n';

    for (size_t i = 0; i < zones.size(); ++i) {
        const auto& zone = zones[i];
        const auto* sample = zone.sample;
        const auto& gen = zone.generators;
        f64 computedRoot = -1.0;
        f64 computedSampleCorrection = 0.0;
        f64 computedBaseSemitones = 0.0;
        f64 computedFinalSemitones = 0.0;
        f64 computedSampleStep = 0.0;
        if (sample && sample->sampleRate != 0 && outputSampleRate != 0) {
            const i32 rootKey = (gen[GEN_OverridingRootKey] >= 0)
                ? gen[GEN_OverridingRootKey]
                : sample->originalPitch;
            const f64 scaleTuningFactor = static_cast<f64>(gen[GEN_ScaleTuning]) / 100.0;
            const f64 fineTune = static_cast<f64>(gen[GEN_FineTune]) - EffectiveSamplePitchCorrection(sample, compatOptions);
            const f64 coarseTune = static_cast<f64>(gen[GEN_CoarseTune]);
            computedRoot = static_cast<f64>(rootKey);
            computedSampleCorrection = EffectiveSamplePitchCorrection(sample, compatOptions);
            computedBaseSemitones =
                static_cast<f64>(key - rootKey) * scaleTuningFactor +
                coarseTune +
                fineTune / 100.0;
            computedFinalSemitones = computedBaseSemitones + state.TotalPitchSemitonesForKey(key);
            computedSampleStep =
                std::pow(2.0, computedFinalSemitones / 12.0) *
                static_cast<f64>(sample->sampleRate) /
                static_cast<f64>(outputSampleRate);
        }
        log << "  zone[" << i << "]"
            << " sample_start=" << (sample ? sample->start : 0)
            << " sample_end=" << (sample ? sample->end : 0)
            << " loop_start=" << (sample ? sample->loopStart : 0)
            << " loop_end=" << (sample ? sample->loopEnd : 0)
            << " sample_rate=" << (sample ? sample->sampleRate : 0)
            << " sample_root=" << (sample ? static_cast<int>(sample->originalPitch) : -1)
            << " sample_pitch_corr=" << (sample ? static_cast<int>(sample->pitchCorrection) : 0)
            << " sample_modes=" << gen[GEN_SampleModes]
            << " overriding_root=" << gen[GEN_OverridingRootKey]
            << " coarse_tune=" << gen[GEN_CoarseTune]
            << " fine_tune=" << gen[GEN_FineTune]
            << " scale_tuning=" << gen[GEN_ScaleTuning]
            << " computed_root=" << computedRoot
            << " computed_sample_corr=" << computedSampleCorrection
            << " computed_base_semitones=" << computedBaseSemitones
            << " computed_final_semitones=" << computedFinalSemitones
            << " computed_sample_step=" << computedSampleStep
            << " initial_atten_cb=" << gen[GEN_InitialAttenuation]
            << " filter_fc=" << gen[GEN_InitialFilterFc]
            << " filter_q=" << gen[GEN_InitialFilterQ]
            << " mod_env_to_fc=" << gen[GEN_ModEnvToFilterFc]
            << " mod_lfo_to_fc=" << gen[GEN_ModLfoToFilterFc]
            << " delay_vol_env=" << gen[GEN_DelayVolEnv]
            << " attack_vol_env=" << gen[GEN_AttackVolEnv]
            << " hold_vol_env=" << gen[GEN_HoldVolEnv]
            << " decay_vol_env=" << gen[GEN_DecayVolEnv]
            << " sustain_vol_env=" << gen[GEN_SustainVolEnv]
            << " release_vol_env=" << gen[GEN_ReleaseVolEnv]
            << " no_truncation=" << (zone.noTruncation ? 1 : 0)
            << " pan=" << gen[GEN_Pan]
            << " reverb_send=" << gen[GEN_ReverbEffectsSend]
            << " chorus_send=" << gen[GEN_ChorusEffectsSend]
            << " key_range=0x" << std::hex << gen[GEN_KeyRange]
            << " vel_range=0x" << gen[GEN_VelRange] << std::dec
            << '\n';
    }
}

f32 ComputeMixGain(int /*activeVoices*/) {
    return 1.0f;
}

f32 ApplyDcBlock(f32 input, f32& prevIn, f32& prevOut) {
    constexpr f32 r = 0.995f;
    const f32 output = input - prevIn + r * prevOut;
    prevIn = input;
    prevOut = output;
    return output;
}

size_t DelaySamples(u32 sampleRate, f32 ms) {
    return std::max<size_t>(1, static_cast<size_t>(sampleRate * (ms / 1000.0f)));
}

f32 NormalizeGs7Bit(u8 value) {
    return static_cast<f32>(value) / 127.0f;
}

f32 Lerp(f32 a, f32 b, f32 t) {
    return a + (b - a) * t;
}

u32 ResolveAudibleChannelMask(u32 muteMask, u32 soloMask) {
    const u32 mask16 = 0xFFFFu;
    const u32 effectiveMuteMask = muteMask & mask16;
    const u32 effectiveSoloMask = soloMask & mask16;
    const u32 baseMask = (effectiveSoloMask != 0) ? effectiveSoloMask : mask16;
    return baseMask & ~effectiveMuteMask & mask16;
}
}

bool Synthesizer::Init(const MidiFile* midi, const SoundBank* soundBank,
                        u32 sampleRate, u32 numChannels,
                        const SynthCompatOptions& compatOptions) {
    compatOptions_    = compatOptions;
    soundBank_        = soundBank;
    sampleRate_       = sampleRate;
    numChannels_      = numChannels;
    finished_         = false;
    normGain_         = soundBank ? soundBank->GetLoudnessNormCompensation() : 1.0f;
    seqEndNotified_   = false;
    errorMsg_.clear();
    reverbIndex_      = 0;
    chorusIndex_      = 0;
    chorusSin_        = 0.0f;
    chorusCos_        = 1.0f;
    mixGainCurrent_   = 1.0f;
    masterVolume_     = 1.0f;
    ResetGsEffectState();
    dcBlockPrevInL_   = 0.0f;
    dcBlockPrevInR_   = 0.0f;
    dcBlockPrevOutL_  = 0.0f;
    dcBlockPrevOutR_  = 0.0f;
    channelMuteMask_.store(0, std::memory_order_relaxed);
    channelSoloMask_.store(0, std::memory_order_relaxed);
    for (u32 ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
        channelProgramView_[ch].store(0, std::memory_order_relaxed);
        channelActiveNoteCountView_[ch].store(0, std::memory_order_relaxed);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
    }
    ResetProgramDebugLog();

    const size_t reverbSize = DelaySamples(sampleRate, 97.0f);
    reverbDelayL_.assign(reverbSize, 0.0f);
    reverbDelayR_.assign(reverbSize, 0.0f);
    reverbTap1_ = DelaySamples(sampleRate, 29.7f) % reverbSize;
    reverbTap2_ = DelaySamples(sampleRate, 37.1f) % reverbSize;
    reverbTap3_ = DelaySamples(sampleRate, 41.1f) % reverbSize;
    reverbTap4_ = DelaySamples(sampleRate, 43.7f) % reverbSize;
    const size_t chorusSize = DelaySamples(sampleRate, 32.0f);
    chorusDelayL_.assign(chorusSize, 0.0f);
    chorusDelayR_.assign(chorusSize, 0.0f);
    chorusBaseTapL_ = DelaySamples(sampleRate, 2.0f);
    chorusBaseTapR_ = DelaySamples(sampleRate, 3.0f);
    chorusDepthTapL_ = DelaySamples(sampleRate, 0.7f);
    chorusDepthTapR_ = DelaySamples(sampleRate, 0.8f);
    zoneScratch_.clear();
    zoneScratch_.reserve(16);

    // チャンネル初期化
    for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
        channels_[ch].Reset();
        channels_[ch].isDrum = (ch == MIDI_DRUM_CHANNEL);
        channelProgramView_[ch].store(channels_[ch].program, std::memory_order_relaxed);
    }

    if (!sequencer_.Init(midi, sampleRate)) {
        errorMsg_ = "Sequencer init failed: " + sequencer_.ErrorMessage();
        return false;
    }
    return true;
}

u32 Synthesizer::Render(i16* buf, u32 numFrames) {
    if (finished_ || !soundBank_) {
        return 0;
    }
    dryBlockL_.resize(numFrames);
    dryBlockR_.resize(numFrames);
    reverbBlockL_.resize(numFrames);
    reverbBlockR_.resize(numFrames);
    chorusBlockL_.resize(numFrames);
    chorusBlockR_.resize(numFrames);

    u32 frame = 0;
    while (frame < numFrames) {
        while (!sequencer_.IsFinished()) {
            u32 samplesToNext = sequencer_.SamplesToNextEvent();
            if (samplesToNext > 0) break;
            const MidiEvent* ev = sequencer_.ConsumeEvent();
            if (ev) HandleEvent(*ev);
        }

        u32 blockFrames = numFrames - frame;
        if (!sequencer_.IsFinished()) {
            const u32 samplesToNext = sequencer_.SamplesToNextEvent();
            if (samplesToNext > 0) {
                blockFrames = std::min(blockFrames, samplesToNext);
            }
        }
        if (blockFrames == 0) {
            continue;
        }

        std::fill_n(dryBlockL_.data(), blockFrames, 0.0f);
        std::fill_n(dryBlockR_.data(), blockFrames, 0.0f);
        std::fill_n(reverbBlockL_.data(), blockFrames, 0.0f);
        std::fill_n(reverbBlockR_.data(), blockFrames, 0.0f);
        std::fill_n(chorusBlockL_.data(), blockFrames, 0.0f);
        std::fill_n(chorusBlockR_.data(), blockFrames, 0.0f);
        const u32 audibleChannelMask = ResolveAudibleChannelMask(
            channelMuteMask_.load(std::memory_order_relaxed),
            channelSoloMask_.load(std::memory_order_relaxed));

        const int activeVoices = voicePool_.RenderBlock(
            dryBlockL_.data(), dryBlockR_.data(),
            reverbBlockL_.data(), reverbBlockR_.data(),
            chorusBlockL_.data(), chorusBlockR_.data(),
            blockFrames, audibleChannelMask);
        std::array<u32, MIDI_CHANNEL_COUNT> activeRootCounts{};
        voicePool_.GetActiveRootNoteCountsPerChannel(activeRootCounts);
        for (u32 ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
            channelActiveNoteCountView_[ch].store(activeRootCounts[ch], std::memory_order_relaxed);
        }
        const f32 targetMixGain = ComputeMixGain(activeVoices);
        const f32 effectiveChorusFeedback = kChorusFeedback * gsChorusFeedbackScale_;
        const f32 effectiveChorusWetMix = kChorusWetMix * gsChorusWetScale_;
        const f32 effectiveChorusToReverb = kChorusToReverb * gsChorusToReverbScale_;
        const f32 effectiveReverbFeedback = kReverbFeedback * gsReverbFeedbackScale_;
        const f32 effectiveReverbWetMix = kReverbWetMix * gsReverbWetScale_;
        const f32 effectiveMasterReverbSend = kMasterReverbSend * gsMasterReverbSendScale_;
        const f32 phaseStepAngle =
            std::atan2(kChorusPhaseStepSin, kChorusPhaseStepCos) * gsChorusRateScale_;
        const f32 phaseStepSin = std::sin(phaseStepAngle);
        const f32 phaseStepCos = std::cos(phaseStepAngle);
        const bool stereo = (numChannels_ == 2);

        for (u32 i = 0; i < blockFrames; ++i) {
            f32 dryL = dryBlockL_[i];
            f32 dryR = dryBlockR_[i];
            const f32 masterReverbSend = effectiveMasterReverbSend;
            f32 reverbInL = reverbBlockL_[i] + dryL * masterReverbSend;
            f32 reverbInR = reverbBlockR_[i] + dryR * masterReverbSend;
            f32 chorusInL = chorusBlockL_[i];
            f32 chorusInR = chorusBlockR_[i];
            f32 wetL = 0.0f, wetR = 0.0f;

            if (!chorusDelayL_.empty()) {
                const size_t size = chorusDelayL_.size();
                const f32 baseTapL = static_cast<f32>(chorusBaseTapL_) * gsChorusDelayScale_;
                const f32 baseTapR = static_cast<f32>(chorusBaseTapR_) * gsChorusDelayScale_;
                const f32 depthTapL = static_cast<f32>(chorusDepthTapL_) * gsChorusDepthScale_;
                const f32 depthTapR = static_cast<f32>(chorusDepthTapR_) * gsChorusDepthScale_;
                // 線形補間でコーラスタップを読む: 整数切り捨てによる離散ジャンプを除去し
                // ピッチ変動の滑らかさを改善する
                const f32 fTapL = std::max(1.0f, baseTapL + (chorusSin_ + 1.0f) * 0.5f * depthTapL);
                const f32 fTapR = std::max(1.0f, baseTapR + (chorusCos_ + 1.0f) * 0.5f * depthTapR);
                const size_t iTapL = static_cast<size_t>(fTapL);
                const size_t iTapR = static_cast<size_t>(fTapR);
                const f32 fracL = fTapL - static_cast<f32>(iTapL);
                const f32 fracR = fTapR - static_cast<f32>(iTapR);
                const size_t wL0 = iTapL % size;
                const size_t wL1 = (iTapL + 1) % size;
                const size_t wR0 = iTapR % size;
                const size_t wR1 = (iTapR + 1) % size;
                const size_t idxL0 = (chorusIndex_ >= wL0) ? (chorusIndex_ - wL0) : (chorusIndex_ + size - wL0);
                const size_t idxL1 = (chorusIndex_ >= wL1) ? (chorusIndex_ - wL1) : (chorusIndex_ + size - wL1);
                const size_t idxR0 = (chorusIndex_ >= wR0) ? (chorusIndex_ - wR0) : (chorusIndex_ + size - wR0);
                const size_t idxR1 = (chorusIndex_ >= wR1) ? (chorusIndex_ - wR1) : (chorusIndex_ + size - wR1);
                const f32 chorusWetL = chorusDelayL_[idxL0] * (1.0f - fracL) + chorusDelayL_[idxL1] * fracL;
                const f32 chorusWetR = chorusDelayR_[idxR0] * (1.0f - fracR) + chorusDelayR_[idxR1] * fracR;
                chorusDelayL_[chorusIndex_] = chorusInL + chorusWetR * effectiveChorusFeedback;
                chorusDelayR_[chorusIndex_] = chorusInR + chorusWetL * effectiveChorusFeedback;
                ++chorusIndex_;
                if (chorusIndex_ == size) chorusIndex_ = 0;
                const f32 nextSin = chorusSin_ * phaseStepCos + chorusCos_ * phaseStepSin;
                const f32 nextCos = chorusCos_ * phaseStepCos - chorusSin_ * phaseStepSin;
                chorusSin_ = nextSin;
                chorusCos_ = nextCos;
                wetL += chorusWetL * effectiveChorusWetMix;
                wetR += chorusWetR * effectiveChorusWetMix;
                reverbInL += chorusWetL * effectiveChorusToReverb;
                reverbInR += chorusWetR * effectiveChorusToReverb;
            }

            if (!reverbDelayL_.empty()) {
                const size_t size = reverbDelayL_.size();
                const f32 reverbWetL =
                    reverbDelayL_[(reverbIndex_ >= reverbTap1_) ? (reverbIndex_ - reverbTap1_) : (reverbIndex_ + size - reverbTap1_)] * 0.30f +
                    reverbDelayL_[(reverbIndex_ >= reverbTap2_) ? (reverbIndex_ - reverbTap2_) : (reverbIndex_ + size - reverbTap2_)] * 0.24f +
                    reverbDelayR_[(reverbIndex_ >= reverbTap3_) ? (reverbIndex_ - reverbTap3_) : (reverbIndex_ + size - reverbTap3_)] * 0.18f +
                    reverbDelayR_[(reverbIndex_ >= reverbTap4_) ? (reverbIndex_ - reverbTap4_) : (reverbIndex_ + size - reverbTap4_)] * 0.12f;
                const f32 reverbWetR =
                    reverbDelayR_[(reverbIndex_ >= reverbTap1_) ? (reverbIndex_ - reverbTap1_) : (reverbIndex_ + size - reverbTap1_)] * 0.30f +
                    reverbDelayR_[(reverbIndex_ >= reverbTap2_) ? (reverbIndex_ - reverbTap2_) : (reverbIndex_ + size - reverbTap2_)] * 0.24f +
                    reverbDelayL_[(reverbIndex_ >= reverbTap3_) ? (reverbIndex_ - reverbTap3_) : (reverbIndex_ + size - reverbTap3_)] * 0.18f +
                    reverbDelayL_[(reverbIndex_ >= reverbTap4_) ? (reverbIndex_ - reverbTap4_) : (reverbIndex_ + size - reverbTap4_)] * 0.12f;
                reverbDelayL_[reverbIndex_] = reverbInL + reverbWetR * effectiveReverbFeedback;
                reverbDelayR_[reverbIndex_] = reverbInR + reverbWetL * effectiveReverbFeedback;
                ++reverbIndex_;
                if (reverbIndex_ == size) reverbIndex_ = 0;
                wetL += reverbWetL * effectiveReverbWetMix;
                wetR += reverbWetR * effectiveReverbWetMix;
            }

            f32 outL = dryL + wetL;
            f32 outR = dryR + wetR;
            mixGainCurrent_ += (targetMixGain - mixGainCurrent_) * kMixGainSmooth;
            outL *= mixGainCurrent_ * kMasterOutputGain * normGain_ * masterVolume_;
            outR *= mixGainCurrent_ * kMasterOutputGain * normGain_ * masterVolume_;
            outL = ApplyDcBlock(outL, dcBlockPrevInL_, dcBlockPrevOutL_);
            outR = ApplyDcBlock(outR, dcBlockPrevInR_, dcBlockPrevOutR_);
            outputLimiter_.Process(outL, outR);

            if (stereo) {
                buf[(frame + i) * 2    ] = ClampToI16(outL * 32767.0f);
                buf[(frame + i) * 2 + 1] = ClampToI16(outR * 32767.0f);
            } else {
                buf[frame + i] = ClampToI16((outL + outR) * 0.5f * 32767.0f);
            }
        }
        sequencer_.AdvanceSamples(blockFrames);
        frame += blockFrames;
    }

    // シーケンサー完了時: NoteOff が来ていないゾンビノートを全て Release へ移行
    // MIDI ファイルが EndOfTrack 前に NoteOff を省略している場合に対処
    if (sequencer_.IsFinished() && !seqEndNotified_) {
        seqEndNotified_ = true;
        for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
            voicePool_.AllNotesOff(ch);
            channelHeldKeyCounts_[ch].fill(0);
            for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
                channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
            }
        }
    }

    // シーケンサー完了後もボイスが鳴り終わるまで続ける
    if (sequencer_.IsFinished() && voicePool_.ActiveCount() == 0 && !HasAudibleEffectTail()) {
        finished_ = true;
    }

    return numFrames;
}

bool Synthesizer::IsFinished() const {
    return finished_;
}

int Synthesizer::GetChannelProgram(u32 channel) const {
    if (channel >= MIDI_CHANNEL_COUNT) {
        return -1;
    }
    return static_cast<int>(channelProgramView_[channel].load(std::memory_order_relaxed));
}

u32 Synthesizer::GetChannelActiveNoteCount(u32 channel) const {
    if (channel >= MIDI_CHANNEL_COUNT) {
        return 0;
    }
    return channelActiveNoteCountView_[channel].load(std::memory_order_relaxed);
}

u32 Synthesizer::GetChannelActiveKeyMaskWord(u32 channel, u32 wordIndex) const {
    if (channel >= MIDI_CHANNEL_COUNT || wordIndex >= 4) {
        return 0;
    }
    return channelActiveKeyMasksView_[channel][wordIndex].load(std::memory_order_relaxed);
}

bool Synthesizer::PopChannelKeyEvent(ChannelKeyEvent& eventOut) {
    std::lock_guard<std::mutex> lock(channelKeyEventMutex_);
    if (channelKeyEvents_.empty()) {
        return false;
    }
    eventOut = channelKeyEvents_.front();
    channelKeyEvents_.pop_front();
    return true;
}

bool Synthesizer::HasAudibleEffectTail() const {
    const auto hasAudibleSample = [](const std::vector<f32>& buffer) {
        for (f32 sample : buffer) {
            if (std::fabs(sample) >= kEffectTailThreshold) {
                return true;
            }
        }
        return false;
    };

    return hasAudibleSample(reverbDelayL_) || hasAudibleSample(reverbDelayR_) ||
           hasAudibleSample(chorusDelayL_) || hasAudibleSample(chorusDelayR_);
}

void Synthesizer::ResetGsEffectState() {
    gsReverbWetScale_ = 1.0f;
    gsReverbFeedbackScale_ = 1.0f;
    gsMasterReverbSendScale_ = 1.0f;
    gsChorusWetScale_ = 1.0f;
    gsChorusFeedbackScale_ = 1.0f;
    gsChorusToReverbScale_ = 1.0f;
    gsChorusDelayScale_ = 1.0f;
    gsChorusDepthScale_ = 1.0f;
    gsChorusRateScale_ = 1.0f;
}

void Synthesizer::PushChannelKeyEvent(u8 ch, u8 key, bool isNoteOn, u16 velocity) {
    constexpr size_t kMaxQueuedEvents = 4096;
    std::lock_guard<std::mutex> lock(channelKeyEventMutex_);
    if (channelKeyEvents_.size() >= kMaxQueuedEvents) {
        channelKeyEvents_.pop_front();
    }
    channelKeyEvents_.push_back(ChannelKeyEvent {
        ch,
        key,
        static_cast<u8>(isNoteOn ? 1 : 0),
        velocity,
    });
}

void Synthesizer::HandleEvent(const MidiEvent& ev) {
    switch (ev.type) {
    case MidiEventType::NoteOn:
        HandleNoteOn(ev.channel, ev.data1, ev.velocity16);
        break;
    case MidiEventType::NoteOff:
        HandleNoteOff(ev.channel, ev.data1);
        break;
    case MidiEventType::ControlChange:
        HandleControlChange(ev.channel, ev.data1, ev.value32);
        break;
    case MidiEventType::ProgramChange:
        HandleProgramChange(ev.channel, ev.data1);
        break;
    case MidiEventType::PolyPressure:
        HandlePolyPressure(ev.channel, ev.data1, ev.data2);
        break;
    case MidiEventType::ChannelPressure:
        HandleChannelPressure(ev.channel, ev.data1);
        break;
    case MidiEventType::PitchBend:
        HandlePitchBend(ev.channel, ev.value32);
        break;
    case MidiEventType::SysEx:
        HandleSysEx(ev);
        break;
    case MidiEventType::PerNotePitchBend:
        HandlePerNotePitchBend(ev.channel, ev.data1, ev.value32);
        break;
    case MidiEventType::PerNoteRegCtrl:
        HandlePerNoteRegCtrl(ev.channel, ev.data1, ev.data2, ev.value32);
        break;
    case MidiEventType::PerNoteManagement:
        HandlePerNoteManagement(ev.channel, ev.data1, ev.data2);
        break;
    default:
        break;
    }
}

void Synthesizer::HandleNoteOn(u8 ch, u8 key, u16 vel) {
    if (vel == 0) {
        HandleNoteOff(ch, key);
        return;
    }
    PushChannelKeyEvent(ch, key, true, vel);
    if (ch < MIDI_CHANNEL_COUNT && key < 128) {
        if (channelHeldKeyCounts_[ch][key] < std::numeric_limits<u16>::max()) {
            ++channelHeldKeyCounts_[ch][key];
        }
        const u32 wordIndex = static_cast<u32>(key >> 5);
        const u32 bitMask = 1u << (key & 31);
        const u32 currentMask = channelActiveKeyMasksView_[ch][wordIndex].load(std::memory_order_relaxed);
        channelActiveKeyMasksView_[ch][wordIndex].store(currentMask | bitMask, std::memory_order_relaxed);
    }

    auto& state = channels_[ch];
    // ドラムチャンネルは標準GMの bank 128 を既定値にしつつ、
    // MIDI側で明示されたドラムバリエーションバンクも先に試す。
    const u16 requestedBank = state.bank;
    u16 resolvedBank = state.isDrum
        ? static_cast<u16>((requestedBank != 0) ? requestedBank : MIDI_DRUM_BANK)
        : requestedBank;

    zoneScratch_.clear();
    ModulatorContext ctx{};
    for (int i = 0; i < 128; ++i) ctx.ccValues[i] = state.ccValues[i];
    for (int i = 0; i < 128; ++i) ctx.polyPressure[i] = state.polyPressure[i];
    ctx.channelPressure = state.channelPressure;
    ctx.pitchBend = state.PitchBend14();
    ctx.pitchWheelSensitivitySemitones = state.pitchBendRangeSemitones;
    ctx.pitchWheelSensitivityCents = state.pitchBendRangeCents;
    ctx.nrpnOffsets = state.sf2Nrpn.generatorOffsets;

    auto tryResolveZones = [&](u16 bankToTry) -> bool {
        zoneScratch_.clear();
        if (!soundBank_->FindZones(bankToTry, state.program, key, vel, zoneScratch_, &ctx)) {
            return false;
        }
        resolvedBank = bankToTry;
        return true;
    };

    bool foundZones = false;
    u8 resolvedProgram = state.program;
    if (state.isDrum) {
        if (requestedBank != 0) {
            foundZones = tryResolveZones(requestedBank);
        }
        if (!foundZones && resolvedBank != static_cast<u16>(MIDI_DRUM_BANK)) {
            foundZones = tryResolveZones(static_cast<u16>(MIDI_DRUM_BANK));
        }
        if (!foundZones && state.program != 0) {
            zoneScratch_.clear();
            if (requestedBank != 0 && soundBank_->FindZones(requestedBank, 0, key, vel, zoneScratch_, &ctx)) {
                foundZones = true;
                resolvedBank = requestedBank;
                resolvedProgram = 0;
            }
        }
        if (!foundZones && state.program != 0) {
            zoneScratch_.clear();
            if (soundBank_->FindZones(static_cast<u16>(MIDI_DRUM_BANK), 0, key, vel, zoneScratch_, &ctx)) {
                foundZones = true;
                resolvedBank = static_cast<u16>(MIDI_DRUM_BANK);
                resolvedProgram = 0;
            }
        }
    } else {
        foundZones = tryResolveZones(requestedBank);
        // 一部のGM MIDIは melodic channel に非0 bank を送るが、実際のSF2は bank 0 にしか
        // プリセットを持たないことが多い。ドラム以外は bank 0 へ一度だけフォールバックする。
        if (!foundZones && requestedBank != 0) {
            zoneScratch_.clear();
            foundZones = tryResolveZones(0);
        }
    }

    if (!foundZones) {
        AppendProgramMissLog(requestedBank, soundBank_->Kind(), ch, state.program, key, vel, state);
        return;
    }

    if (ShouldLogProgram(state.program)) {
        AppendProgramSummaryLog(requestedBank, resolvedBank, soundBank_->Kind(), ch, resolvedProgram, key, vel);
        AppendProgramDebugLog(requestedBank, resolvedBank, soundBank_->Kind(), ch, resolvedProgram, key, vel,
                              sampleRate_, state, zoneScratch_, compatOptions_);
    }

    if (state.monoMode) {
        voicePool_.AllNotesOff(ch);
    }

    u8 excClass = 0;
    if (!zoneScratch_.empty())
        excClass = static_cast<u8>(zoneScratch_[0].generators[GEN_ExclusiveClass]);

    f64 pitchBend = 0.0;
    if (!state.isDrum) {
        pitchBend = (soundBank_->Kind() == SoundBankKind::Sf2)
            ? (state.ChannelTuningSemitones() + state.NoteTuningSemitones(key))
            : state.TotalPitchSemitonesForKey(key);
    }

    i32 portamentoSourceKey = -1;
    if (!state.isDrum && state.portamento) {
        const u8 sourceKey = (state.portamentoControlKey <= 127) ? state.portamentoControlKey : state.lastNoteKey;
        if (sourceKey <= 127 && sourceKey != key) {
            portamentoSourceKey = static_cast<i32>(sourceKey);
        }
    }

    voicePool_.NoteOn(zoneScratch_, soundBank_->SampleData(), soundBank_->SampleData24(), soundBank_->SampleDataCount(),
                      resolvedBank, ch, resolvedProgram, key, vel, sampleRate_,
                      pitchBend, excClass, state.VolumeFactor(), state.pan32,
                      state.reverbSend32, state.chorusSend32, soundBank_->Kind(), compatOptions_,
                      portamentoSourceKey, state.portamentoTime, state.softPedal);
    state.lastNoteKey = key;
    state.portamentoControlKey = 0xFF;
}

void Synthesizer::HandleNoteOff(u8 ch, u8 key) {
    PushChannelKeyEvent(ch, key, false, 0);
    if (ch < MIDI_CHANNEL_COUNT && key < 128) {
        if (channelHeldKeyCounts_[ch][key] > 0) {
            --channelHeldKeyCounts_[ch][key];
        }
        if (channelHeldKeyCounts_[ch][key] == 0) {
            const u32 wordIndex = static_cast<u32>(key >> 5);
            const u32 bitMask = 1u << (key & 31);
            const u32 currentMask = channelActiveKeyMasksView_[ch][wordIndex].load(std::memory_order_relaxed);
            channelActiveKeyMasksView_[ch][wordIndex].store(currentMask & ~bitMask, std::memory_order_relaxed);
        }
    }
    voicePool_.NoteOff(ch, key, channels_[ch].sustain);
}

void Synthesizer::HandleControlChange(u8 ch, u8 cc, u32 val32) {
    auto& state = channels_[ch];
    const u8 val = static_cast<u8>(val32 >> 25); // 32-bit → 7-bit (SF2モジュレーション用)
    state.ccValues[cc] = val;
    switch (cc) {
    case 0:  // Bank Select MSB
        state.bankMSB = val;
        state.bank = static_cast<u16>(state.bankMSB) * 128 + state.bankLSB;
        break;
    case 6:  // Data Entry MSB (RPN / SF2 NRPN)
        state.dataEntryMSB = val;
        if (IsRpnSelected(state)) {
            ApplyRpnValue(state);
        } else if (state.sf2Nrpn.sf2Mode) {
            state.ApplySf2NrpnDataEntry();
        }
        break;
    case 38: // Data Entry LSB (RPN / SF2 NRPN)
        state.dataEntryLSB = val;
        if (IsRpnSelected(state)) {
            ApplyRpnValue(state);
        } else if (state.sf2Nrpn.sf2Mode) {
            state.ApplySf2NrpnDataEntry();
        }
        break;
    case 5:  // Portamento Time
        state.portamentoTime = val;
        break;
    case 7:  // Volume (32-bit精度で保持)
        state.volume32 = val32;
        ApplyChannelMix(voicePool_, ch, state);
        break;
    case 10: // Pan (32-bit精度で保持)
        state.pan32 = val32;
        ApplyChannelMix(voicePool_, ch, state);
        break;
    case 11: // Expression (32-bit精度で保持)
        state.expression32 = val32;
        ApplyChannelMix(voicePool_, ch, state);
        break;
    case 32: // Bank Select LSB
        state.bankLSB = val;
        state.bank = static_cast<u16>(state.bankMSB) * 128 + state.bankLSB;
        break;
    case 64: // Sustain Pedal
        state.sustain = (val >= 64);
        if (!state.sustain)
            voicePool_.ReleaseSustained(ch);
        break;
    case 66: // Sostenuto
        state.sostenuto = (val >= 64);
        voicePool_.SetSostenuto(ch, state.sostenuto);
        break;
    case 67: // Soft Pedal
        state.softPedal = (val >= 64);
        break;
    case 65: // Portamento On/Off
        state.portamento = (val >= 64);
        break;
    case 84: // Portamento Control
        state.portamentoControlKey = val;
        break;
    case 91: // Reverb Send Level (32-bit精度で保持)
        state.reverbSend32 = val32;
        ApplyChannelMix(voicePool_, ch, state);
        break;
    case 93: // Chorus Send Level (32-bit精度で保持)
        state.chorusSend32 = val32;
        ApplyChannelMix(voicePool_, ch, state);
        break;
    case 98: // NRPN LSB
        state.HandleSf2NrpnControl(98, val);
        break;
    case 99: // NRPN MSB
        state.HandleSf2NrpnControl(99, val);
        break;
    case 100: // RPN LSB
        state.HandleSf2NrpnControl(100, val);
        break;
    case 101: // RPN MSB
        state.HandleSf2NrpnControl(101, val);
        break;
    case 96: // Data Increment
        if (IsRpnSelected(state)) {
            switch ((static_cast<u16>(state.rpnMSB) << 7) | state.rpnLSB) {
            case 0:
                if (state.dataEntryLSB < 99) {
                    ++state.dataEntryLSB;
                } else if (state.dataEntryMSB < 24) {
                    ++state.dataEntryMSB;
                    state.dataEntryLSB = 0;
                }
                break;
            case 1:
                Increment14Bit(state.dataEntryMSB, state.dataEntryLSB, 16383);
                break;
            case 2:
                state.dataEntryMSB = std::min<u8>(127, static_cast<u8>(state.dataEntryMSB + 1));
                break;
            case 5:
                if (state.dataEntryLSB < 99) {
                    ++state.dataEntryLSB;
                } else if (state.dataEntryMSB < 127) {
                    ++state.dataEntryMSB;
                    state.dataEntryLSB = 0;
                }
                break;
            default:
                break;
            }
            ApplyRpnValue(state);
        } else if (IsNrpnSelected(state)) {
            Increment14Bit(state.dataEntryMSB, state.dataEntryLSB, 16383);
            state.ApplySf2NrpnDataEntry();
        }
        break;
    case 97: // Data Decrement
        if (IsRpnSelected(state)) {
            switch ((static_cast<u16>(state.rpnMSB) << 7) | state.rpnLSB) {
            case 0:
                if (state.dataEntryLSB > 0) {
                    --state.dataEntryLSB;
                } else if (state.dataEntryMSB > 0) {
                    --state.dataEntryMSB;
                    state.dataEntryLSB = 99;
                }
                break;
            case 1:
                Decrement14Bit(state.dataEntryMSB, state.dataEntryLSB);
                break;
            case 2:
                state.dataEntryMSB = (state.dataEntryMSB > 0) ? static_cast<u8>(state.dataEntryMSB - 1) : 0;
                break;
            case 5:
                if (state.dataEntryLSB > 0) {
                    --state.dataEntryLSB;
                } else if (state.dataEntryMSB > 0) {
                    --state.dataEntryMSB;
                    state.dataEntryLSB = 99;
                }
                break;
            default:
                break;
            }
            ApplyRpnValue(state);
        } else if (IsNrpnSelected(state)) {
            Decrement14Bit(state.dataEntryMSB, state.dataEntryLSB);
            state.ApplySf2NrpnDataEntry();
        }
        break;
    case 120: // All Sound Off
        voicePool_.AllSoundOff(ch);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
        break;
    case 121: // Reset All Controllers
        state.ResetControllersOnly();
        ApplyChannelMix(voicePool_, ch, state);
        ApplyCurrentPitchToChannel(voicePool_, ch, state);
        voicePool_.SetSostenuto(ch, false);
        break;
    case 122: // Local Control
        state.localControl = (val >= 64);
        break;
    case 123: // All Notes Off
        voicePool_.AllNotesOff(ch);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
        break;
    case 124: // Omni Off
        state.omniMode = false;
        voicePool_.AllNotesOff(ch);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
        break;
    case 125: // Omni On
        state.omniMode = true;
        voicePool_.AllNotesOff(ch);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
        break;
    case 126: // Mono On
        state.monoMode = true;
        voicePool_.AllNotesOff(ch);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
        break;
    case 127: // Poly On
        state.monoMode = false;
        voicePool_.AllNotesOff(ch);
        channelHeldKeyCounts_[ch].fill(0);
        for (u32 wordIndex = 0; wordIndex < 4; ++wordIndex) {
            channelActiveKeyMasksView_[ch][wordIndex].store(0, std::memory_order_relaxed);
        }
        break;
    default:
        break;
    }

    switch (cc) {
    case 6:
    case 38:
    case 96:
    case 97:
    case 121:
        ApplyCurrentPitchToChannel(voicePool_, ch, state);
        break;
    default:
        break;
    }

    switch (cc) {
    case 0:
    case 32:
    case 98:
    case 99:
    case 100:
    case 101:
    case 120:
    case 123:
        break;
    default:
        RefreshSf2ControllersForChannel(ch);
        break;
    }
}

void Synthesizer::HandleProgramChange(u8 ch, u8 program) {
    channels_[ch].program = program;
    channelProgramView_[ch].store(program, std::memory_order_relaxed);
}

void Synthesizer::HandlePolyPressure(u8 ch, u8 key, u8 pressure) {
    channels_[ch].polyPressure[key] = pressure;
    RefreshSf2ControllersForChannel(ch);
}

void Synthesizer::HandleChannelPressure(u8 ch, u8 pressure) {
    channels_[ch].channelPressure = pressure;
    RefreshSf2ControllersForChannel(ch);
}

void Synthesizer::HandlePitchBend(u8 ch, u32 bend32) {
    channels_[ch].pitchBend32 = bend32;
    // ドラムチャンネルはピッチベンド無効
    if (channels_[ch].isDrum) return;
    // 現在鳴っているボイスのピッチをリアルタイム更新
    voicePool_.UpdateChannelPitch(ch, channels_[ch]);
    RefreshSf2ControllersForChannel(ch);
}

void Synthesizer::HandlePerNotePitchBend(u8 ch, u8 key, u32 pb32) {
    // MIDI 2.0 Per-Note Pitch Bend: デフォルト ±2 半音のレンジ
    constexpr f64 kRange = 2.0;
    const f64 normalized = (static_cast<f64>(pb32) - 2147483648.0) / 2147483648.0; // -1.0 to +1.0
    voicePool_.UpdatePerNotePitchBend(ch, key, normalized * kRange);
}

void Synthesizer::HandlePerNoteRegCtrl(u8 ch, u8 key, u8 index, u32 value) {
    // Bank 0, Index 0 ("Pitch 7.25"): 7.25 固定小数点, 単位=半音, center=0x80000000
    // 値の変換: offset = (i32)(value ^ 0x80000000), semitones = offset / 2^25
    if (index == 0) {
        const i32 offset = static_cast<i32>(value ^ 0x80000000u);
        const f64 semitones = static_cast<f64>(offset) / 33554432.0; // 2^25
        voicePool_.UpdatePerNotePitchBend(ch, key, semitones);
    }
    // 他のインデックスは現フェーズでは未実装
}

void Synthesizer::HandlePerNoteManagement(u8 ch, u8 key, u8 flags) {
    // bit1: Reset Per-Note Controllers
    if (flags & 0x02)
        voicePool_.ResetPerNoteState(ch, key);
}

void Synthesizer::HandleSysEx(const MidiEvent& ev) {
    const auto& data = ev.payload;
    if (data.empty()) {
        return;
    }

    const auto resetChannels = [&]() {
        for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
            voicePool_.AllSoundOff(static_cast<u8>(ch));
            channels_[ch].Reset();
            channels_[ch].isDrum = (ch == MIDI_DRUM_CHANNEL);
            ApplyChannelMix(voicePool_, static_cast<u8>(ch), channels_[ch]);
            ApplyCurrentPitchToChannel(voicePool_, static_cast<u8>(ch), channels_[ch]);
        }
        masterVolume_ = 1.0f;
        ResetGsEffectState();
    };

    if (data.size() >= 4 && data[0] == 0x7E && data[2] == 0x09) {
        if (data[3] == 0x01 || data[3] == 0x02 || data[3] == 0x03) {
            resetChannels();
        }
        return;
    }

    if (data.size() >= 5 && data[0] == 0x7F && data[2] == 0x04 && data[3] == 0x01) {
        if (data.size() < 6) {
            return;
        }
        const u16 volume14 = static_cast<u16>(data[5] << 7) | data[4];
        masterVolume_ = std::clamp(static_cast<f32>(volume14) / 16383.0f, 0.0f, 1.0f);
        return;
    }

    if (data.size() >= 6 &&
        (data[0] == 0x7E || data[0] == 0x7F) &&
        data[2] == 0x08 && data[3] == 0x02) {
        const u8 count = data[5];
        size_t offset = 6;
        for (u8 i = 0; i < count && offset + 3 < data.size(); ++i, offset += 4) {
            const u8 key = data[offset];
            const u8 semitone = data[offset + 1];
            const u8 fracMsb = data[offset + 2];
            const u8 fracLsb = data[offset + 3];
            if (key >= 128) {
                continue;
            }
            if (semitone == 0x7F && fracMsb == 0x7F && fracLsb == 0x7F) {
                continue;
            }
            const i32 fraction14 = (static_cast<i32>(fracMsb) << 7) | fracLsb;
            const f64 tunedSemitone = static_cast<f64>(semitone) + static_cast<f64>(fraction14) / 16384.0;
            const i16 tuningCents = static_cast<i16>(std::clamp(
                static_cast<i32>(std::llround((tunedSemitone - static_cast<f64>(key)) * 100.0)),
                -12800, 12800));
            for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
                channels_[ch].noteTuningCents[key] = tuningCents;
            }
        }
        for (int ch = 0; ch < MIDI_CHANNEL_COUNT; ++ch) {
            ApplyCurrentPitchToChannel(voicePool_, static_cast<u8>(ch), channels_[ch]);
            RefreshSf2ControllersForChannel(static_cast<u8>(ch));
        }
        return;
    }

    if (data.size() >= 10 &&
        data[0] == 0x41 && data[2] == 0x42 && data[3] == 0x12 &&
        data[4] == 0x40 && data[5] == 0x00 && data[6] == 0x7F && data[7] == 0x00) {
        resetChannels();
        return;
    }

    if (data.size() >= 8 &&
        data[0] == 0x41 && data[2] == 0x42 && data[3] == 0x12 &&
        data[4] == 0x40 && data[5] == 0x01) {
        const u8 param = data[6];
        const u8 value = data[7];
        const f32 t = NormalizeGs7Bit(value);
        switch (param) {
        case 0x05: // Reverb Macro
            gsReverbWetScale_ = Lerp(0.75f, 1.45f, t);
            gsReverbFeedbackScale_ = Lerp(0.80f, 1.35f, t);
            break;
        case 0x08: // Reverb Level
            gsReverbWetScale_ = Lerp(0.20f, 1.85f, t);
            break;
        case 0x09: // Reverb Time
            gsReverbFeedbackScale_ = Lerp(0.70f, 1.70f, t);
            break;
        case 0x0A: // Reverb Feedback
            gsMasterReverbSendScale_ = Lerp(0.70f, 1.35f, t);
            break;
        case 0x0C: // Reverb Predelay
            gsMasterReverbSendScale_ = Lerp(0.85f, 1.20f, t);
            break;
        case 0x0D: // Chorus Macro
            gsChorusWetScale_ = Lerp(0.80f, 1.50f, t);
            gsChorusFeedbackScale_ = Lerp(0.80f, 1.30f, t);
            gsChorusDepthScale_ = Lerp(0.85f, 1.30f, t);
            break;
        case 0x0F: // Chorus Level
            gsChorusWetScale_ = Lerp(0.20f, 1.80f, t);
            break;
        case 0x10: // Chorus Feedback
            gsChorusFeedbackScale_ = Lerp(0.60f, 1.80f, t);
            break;
        case 0x11: // Chorus Delay
            gsChorusDelayScale_ = Lerp(0.65f, 1.60f, t);
            break;
        case 0x12: // Chorus Rate
            gsChorusRateScale_ = Lerp(0.60f, 1.80f, t);
            break;
        case 0x13: // Chorus Depth
            gsChorusDepthScale_ = Lerp(0.55f, 1.85f, t);
            break;
        case 0x14: // Chorus send to reverb
            gsChorusToReverbScale_ = Lerp(0.00f, 2.00f, t);
            break;
        default:
            break;
        }
        return;
    }

    if (data.size() >= 8 &&
        data[0] == 0x43 && data[2] == 0x4C &&
        data[3] == 0x00 && data[4] == 0x00 && data[5] == 0x7E && data[6] == 0x00) {
        resetChannels();
    }
}

void Synthesizer::RefreshSf2ControllersForChannel(u8 ch) {
    if (!soundBank_ || soundBank_->Kind() != SoundBankKind::Sf2) {
        return;
    }
    const auto& state = channels_[ch];
    ModulatorContext ctx{};
    for (int i = 0; i < 128; ++i) {
        ctx.ccValues[i] = state.ccValues[i];
        ctx.polyPressure[i] = state.polyPressure[i];
    }
    ctx.channelPressure = state.channelPressure;
    ctx.pitchBend = state.PitchBend14();
    ctx.pitchWheelSensitivitySemitones = state.pitchBendRangeSemitones;
    ctx.pitchWheelSensitivityCents = state.pitchBendRangeCents;
    ctx.nrpnOffsets = state.sf2Nrpn.generatorOffsets;
    voicePool_.RefreshSf2Controllers(ch, *soundBank_, ctx,
                                     state.VolumeFactor(), state.pan32, state.reverbSend32, state.chorusSend32);
}

} // namespace XArkMidi

