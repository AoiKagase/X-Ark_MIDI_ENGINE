/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "Voice.h"
#include "SimdKernels.h"
#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>

namespace XArkMidi {

namespace {

constexpr i32 kFilterFcMin = 1500;
constexpr i32 kFilterFcMax = 13500;
constexpr i32 kFilterQCbMin = 0;
constexpr i32 kFilterQCbMax = 960;
constexpr f32 kInvPcmScale = 1.0f / 32768.0f;
constexpr f32 kInvPcm24Scale = 1.0f / 8388608.0f;
constexpr f32 kEnvelopeSilenceThreshold = 1.0e-5f;
constexpr f64 kEnvelopeSilenceTarget = 1.0e-5;
constexpr f32 kMinimumLoopReleaseSeconds = 0.005f;

f64 EffectiveSamplePitchCorrection(const SampleHeader* sample, const SynthCompatOptions& compatOptions) {
    if (!sample || !compatOptions.enableSf2SamplePitchCorrection) {
        return 0.0;
    }
    return static_cast<f64>(sample->pitchCorrection);
}

f32 NormalizeMidiSend(u32 value) {
    return static_cast<f32>(value) / 4294967295.0f;
}

f32 NormalizeSf2EffectsSend(i32 value) {
    return std::clamp(static_cast<f32>(value) / 1000.0f, 0.0f, 1.0f);
}

f32 MixEffectsSend(f32 presetSend, f32 channelSend, const SynthCompatOptions& compatOptions) {
    if (compatOptions.multiplySf2MidiEffectsSends) {
        return std::clamp(presetSend * channelSend, 0.0f, 1.0f);
    }
    return std::clamp(presetSend + channelSend, 0.0f, 1.0f);
}

u8 ResolveForcedKey(u8 key, const i32* gen) {
    const i32 forcedKey = gen[GEN_Keynum];
    if (forcedKey >= 0 && forcedKey <= 127) {
        return static_cast<u8>(forcedKey);
    }
    return key;
}

u16 ResolveForcedVelocity(u16 velocity, const i32* gen) {
    const i32 forcedVelocity = gen[GEN_Velocity];
    if (forcedVelocity >= 0 && forcedVelocity <= 127) {
        return static_cast<u16>((forcedVelocity * 65535 + 63) / 127);
    }
    return velocity;
}

f32 TriangleLfoValue(f32 phase) {
    const f32 wrapped = phase - std::floor(phase);
    return 1.0f - 4.0f * std::fabs(wrapped - 0.5f);
}

f32 HertzToPhaseStep(f64 hz, u32 sampleRate) {
    if (sampleRate == 0 || hz <= 0.0) return 0.0f;
    return static_cast<f32>(hz / static_cast<f64>(sampleRate));
}

f32 ComputeLfoValue(u32 delayEnd, u32& sampleCount, f32& phase, f32 phaseStep) {
    if (sampleCount < delayEnd) {
        ++sampleCount;
        return 0.0f;
    }
    const f32 value = TriangleLfoValue(phase);
    phase += phaseStep;
    phase -= std::floor(phase);
    ++sampleCount;
    return value;
}

f32 ComputeReleaseRate(f32 currentLevel, f32 releaseTimeSeconds, u32 sampleRate) {
    if (sampleRate == 0) {
        return 0.0f;
    }
    if (releaseTimeSeconds <= 0.0f || currentLevel <= kEnvelopeSilenceThreshold) {
        return 0.0f;
    }

    const f64 level = std::max(static_cast<f64>(currentLevel), kEnvelopeSilenceTarget);
    return static_cast<f32>(std::pow(kEnvelopeSilenceTarget / level,
                                     1.0 / (static_cast<f64>(releaseTimeSeconds) * sampleRate)));
}

f64 CentsToHertz(i32 cents) {
    return 8.176 * std::pow(2.0, static_cast<f64>(cents) / 1200.0);
}

f32 FilterQCbToQ(i32 qCb) {
    const f32 normalized = static_cast<f32>(std::clamp(qCb, kFilterQCbMin, kFilterQCbMax)) / static_cast<f32>(kFilterQCbMax);
    return std::clamp(0.7071f + normalized * 11.0f, 0.7071f, 12.0f);
}

void ComputeLowPassCoeffs(i32 fcCents, i32 qCb, u32 sampleRate,
                          f32& b0, f32& b1, f32& b2, f32& a1, f32& a2) {
    const f64 cutoffHz = std::clamp(CentsToHertz(fcCents), 20.0, static_cast<f64>(sampleRate) * 0.45);
    const f64 omega = 2.0 * 3.14159265358979323846 * cutoffHz / static_cast<f64>(sampleRate);
    const f64 sinW = std::sin(omega);
    const f64 cosW = std::cos(omega);
    const f64 q = static_cast<f64>(FilterQCbToQ(qCb));
    const f64 alpha = sinW / (2.0 * q);
    const f64 a0 = 1.0 + alpha;
    b0 = static_cast<f32>(((1.0 - cosW) * 0.5) / a0);
    b1 = static_cast<f32>((1.0 - cosW) / a0);
    b2 = static_cast<f32>(((1.0 - cosW) * 0.5) / a0);
    a1 = static_cast<f32>((-2.0 * cosW) / a0);
    a2 = static_cast<f32>((1.0 - alpha) / a0);
}

f32 ProcessFilterSample(f32 input,
                        f32 b0, f32 b1, f32 b2, f32 a1, f32 a2,
                        f32& z1, f32& z2) {
    const f32 output = b0 * input + z1;
    z1 = b1 * input - a1 * output + z2;
    z2 = b2 * input - a2 * output;
    return output;
}

i64 ToFixedSamplePos(i32 sampleIndex) {
    return static_cast<i64>(sampleIndex) << kSamplePosFracBits;
}

inline f32 Normalize24BitSample(i32 sample) {
    return static_cast<f32>(sample) * kInvPcm24Scale;
}

inline f32 Read24BitSampleNormalized(const i32* data, size_t index) {
    return Normalize24BitSample(data[index]);
}

inline f32 CubicInterpFixed24Bit(const i32* data, i64 posFixed, size_t dataSize) {
    if (dataSize == 0) return 0.0f;

    const i64 i1 = posFixed >> kSamplePosFracBits;
    const f32 frac = static_cast<f32>(posFixed & kSamplePosFracMask) * kSamplePosFracScale;
    const f32 y0 = Normalize24BitSample(data[ClampSampleIndex(i1 - 1, dataSize)]);
    const f32 y1 = Normalize24BitSample(data[ClampSampleIndex(i1, dataSize)]);
    const f32 y2 = Normalize24BitSample(data[ClampSampleIndex(i1 + 1, dataSize)]);
    const f32 y3 = Normalize24BitSample(data[ClampSampleIndex(i1 + 2, dataSize)]);
    return CubicHermite(y0, y1, y2, y3, frac);
}

inline size_t ClampNonLoopSampleLimit(size_t sampleDataSize, u32 sampleEnd) {
    if (sampleDataSize == 0) return 0;
    const size_t requestedLimit = static_cast<size_t>(sampleEnd);
    if (requestedLimit == 0) return std::min<size_t>(1, sampleDataSize);
    return std::min(sampleDataSize, requestedLimit);
}

inline f32 CubicInterpFixedLooped24Bit(const i32* data,
                                       i64 posFixed,
                                       size_t dataSize,
                                       size_t loopStartIndex,
                                       size_t loopEndIndex) {
    if (dataSize == 0) return 0.0f;

    const i64 i1 = posFixed >> kSamplePosFracBits;
    const f32 frac = static_cast<f32>(posFixed & kSamplePosFracMask) * kSamplePosFracScale;
    const f32 y0 = Normalize24BitSample(data[WrapLoopSampleIndex(i1 - 1, dataSize, loopStartIndex, loopEndIndex)]);
    const f32 y1 = Normalize24BitSample(data[WrapLoopSampleIndex(i1, dataSize, loopStartIndex, loopEndIndex)]);
    const f32 y2 = Normalize24BitSample(data[WrapLoopSampleIndex(i1 + 1, dataSize, loopStartIndex, loopEndIndex)]);
    const f32 y3 = Normalize24BitSample(data[WrapLoopSampleIndex(i1 + 2, dataSize, loopStartIndex, loopEndIndex)]);
    return CubicHermite(y0, y1, y2, y3, frac);
}

u32 ComputeRenderableFrames(i64 posFixed, i64 stepFixed, i64 boundaryFixed, u32 maxFrames) {
    if (maxFrames == 0 || stepFixed <= 0 || posFixed >= boundaryFixed) {
        return 0;
    }

    const i64 remaining = boundaryFixed - 1 - posFixed;
    if (remaining < 0) {
        return 0;
    }

    const u64 frames = static_cast<u64>(remaining / stepFixed) + 1ULL;
    return static_cast<u32>(std::min<u64>(frames, maxFrames));
}

u32 FramesUntilPhaseChange(EnvPhase phase,
                           f32 level,
                           u32 sampleCount,
                           u32 delayEnd,
                           f32 attackRate,
                           u32 holdEnd,
                           f32 decayRate,
                           f32 sustainLevel,
                           f32 releaseRate) {
    switch (phase) {
    case EnvPhase::Delay:
        return (sampleCount < delayEnd) ? (delayEnd - sampleCount) : 0;
    case EnvPhase::Attack:
        if (attackRate <= 0.0f || level >= 1.0f) return 0;
        return std::max<u32>(1, static_cast<u32>(std::ceil((1.0f - level) / attackRate)));
    case EnvPhase::Hold:
        return (sampleCount < holdEnd) ? (holdEnd - sampleCount) : 0;
    case EnvPhase::Decay:
        if (decayRate <= 0.0f) return 0;
        if (sustainLevel <= kEnvelopeSilenceThreshold) {
            if (level <= kEnvelopeSilenceThreshold) return 0;
            return std::max<u32>(1, static_cast<u32>(std::ceil(
                std::log(kEnvelopeSilenceThreshold / level) / std::log(decayRate))));
        }
        if (level <= sustainLevel + 1e-9f) return 0;
        return std::max<u32>(1, static_cast<u32>(std::ceil(std::log(sustainLevel / level) / std::log(decayRate))));
    case EnvPhase::Sustain:
        return std::numeric_limits<u32>::max();
    case EnvPhase::Release:
        if (releaseRate <= 0.0f || level < kEnvelopeSilenceThreshold) return 0;
        return std::max<u32>(1, static_cast<u32>(std::ceil(std::log(kEnvelopeSilenceThreshold / level) / std::log(releaseRate))));
    case EnvPhase::Off:
    default:
        return 0;
    }
}

template<bool HasReverb, bool HasChorus, bool IntegralStep>
inline void MixConstantChunkScalar(
    f32* outL, f32* outR,
    f32* reverbL, f32* reverbR,
    f32* chorusL, f32* chorusR,
    const i16* sampleData,
    size_t sampleDataSize,
    i64& samplePosFixed,
    i64 sampleStepFixed,
    bool looping,
    i64 loopStartFixed,
    i64 loopEndFixed,
    u32 offset,
    u32 chunkFrames,
    f32 dryGainL, f32 dryGainR,
    f32 reverbGainL, f32 reverbGainR,
    f32 chorusGainL, f32 chorusGainR) {
    if constexpr (IntegralStep) {
        for (u32 i = 0; i < chunkFrames; ++i) {
            const f32 sample = static_cast<f32>(sampleData[static_cast<size_t>(samplePosFixed >> kSamplePosFracBits)]);
            samplePosFixed += sampleStepFixed;

            const u32 frameIndex = offset + i;
            outL[frameIndex] += sample * dryGainL;
            outR[frameIndex] += sample * dryGainR;
            if constexpr (HasReverb) {
                reverbL[frameIndex] += sample * reverbGainL;
                reverbR[frameIndex] += sample * reverbGainR;
            }
            if constexpr (HasChorus) {
                chorusL[frameIndex] += sample * chorusGainL;
                chorusR[frameIndex] += sample * chorusGainR;
            }
        }
    } else {
        for (u32 i = 0; i < chunkFrames; ++i) {
            const size_t loopStartIndex = static_cast<size_t>(loopStartFixed >> kSamplePosFracBits);
            const size_t loopEndIndex = static_cast<size_t>(loopEndFixed >> kSamplePosFracBits);
            const f32 sample = looping
                ? CubicInterpFixedLooped(sampleData, samplePosFixed, sampleDataSize, loopStartIndex, loopEndIndex)
                : CubicInterpFixed(sampleData, samplePosFixed, sampleDataSize);
            samplePosFixed += sampleStepFixed;

            const u32 frameIndex = offset + i;
            outL[frameIndex] += sample * dryGainL;
            outR[frameIndex] += sample * dryGainR;
            if constexpr (HasReverb) {
                reverbL[frameIndex] += sample * reverbGainL;
                reverbR[frameIndex] += sample * reverbGainR;
            }
            if constexpr (HasChorus) {
                chorusL[frameIndex] += sample * chorusGainL;
                chorusR[frameIndex] += sample * chorusGainR;
            }
        }
    }
}

template<bool HasReverb, bool HasChorus>
inline bool TryMixConstantChunkAvx2(
    f32* outL, f32* outR,
    f32* reverbL, f32* reverbR,
    f32* chorusL, f32* chorusR,
    const i16* sampleData,
    i64& samplePosFixed,
    u32 offset,
    u32 chunkFrames,
    f32 dryGainL, f32 dryGainR,
    f32 reverbGainL, f32 reverbGainR,
    f32 chorusGainL, f32 chorusGainR) {
    if (chunkFrames < 32) {
        return false;
    }

    const i16* src = sampleData + static_cast<size_t>(samplePosFixed >> kSamplePosFracBits);
    if constexpr (!HasReverb && !HasChorus) {
        Simd::MixMonoContiguousDryAvx2(
            outL + offset, outR + offset, src, chunkFrames, dryGainL, dryGainR);
    } else {
        Simd::MixMonoContiguousFxAvx2(
            outL + offset, outR + offset,
            reverbL + offset, reverbR + offset,
            chorusL + offset, chorusR + offset,
            src, chunkFrames,
            dryGainL, dryGainR,
            HasReverb ? reverbGainL : 0.0f,
            HasReverb ? reverbGainR : 0.0f,
            HasChorus ? chorusGainL : 0.0f,
            HasChorus ? chorusGainR : 0.0f);
    }

    samplePosFixed += static_cast<i64>(chunkFrames) * kSamplePosFracOne;
    return true;
}

void AdvanceEnvelope(EnvPhase& phase,
                     f32& level,
                     u32& sampleCount,
                     u32 delayEnd,
                     f32 attackRate,
                     u32 holdEnd,
                     f32 decayRate,
                     f32 sustainLevel,
                     f32 releaseRate) {
    switch (phase) {
    case EnvPhase::Delay:
        sampleCount++;
        if (sampleCount >= delayEnd) {
            phase = EnvPhase::Attack;
            sampleCount = 0;
        }
        break;
    case EnvPhase::Attack:
        level += attackRate;
        if (level >= 1.0) {
            level = 1.0f;
            phase = (holdEnd > 0.0) ? EnvPhase::Hold : EnvPhase::Decay;
            sampleCount = 0;
        }
        break;
    case EnvPhase::Hold:
        sampleCount++;
        if (sampleCount >= holdEnd) {
            phase = EnvPhase::Decay;
            sampleCount = 0;
        }
        break;
    case EnvPhase::Decay:
        if (decayRate <= 0.0f) {
            level = sustainLevel;
            phase = EnvPhase::Sustain;
        } else {
            level *= decayRate;
            if (level <= sustainLevel + 1e-9f) {
                level = sustainLevel;
                phase = EnvPhase::Sustain;
            }
        }
        break;
    case EnvPhase::Sustain:
        break;
    case EnvPhase::Release:
        level *= releaseRate;
        if (level < kEnvelopeSilenceThreshold) {
            phase = EnvPhase::Off;
        }
        break;
    case EnvPhase::Off:
        break;
    }
}

}

void Voice::ApplyResolvedZoneEnvelopeParameters(const i32* gen, i32 effectiveKey) {
    const f64 outputRate = static_cast<f64>(outputSampleRate);

    f64 delayTime   = TimecentsToSeconds(gen[GEN_DelayVolEnv]);
    f64 attackTime  = TimecentsToSeconds(gen[GEN_AttackVolEnv]);
    f64 holdTime    = TimecentsToSeconds(gen[GEN_HoldVolEnv]);
    f64 decayTime   = TimecentsToSeconds(gen[GEN_DecayVolEnv]);
    f64 releaseTime = TimecentsToSeconds(gen[GEN_ReleaseVolEnv]);
    const f64 sustainCb = static_cast<f64>(gen[GEN_SustainVolEnv]);

    holdTime *= std::pow(2.0, static_cast<f64>(gen[GEN_KeynumToVolEnvHold]) * (60 - effectiveKey) / 1200.0);
    decayTime *= std::pow(2.0, static_cast<f64>(gen[GEN_KeynumToVolEnvDecay]) * (60 - effectiveKey) / 1200.0);

    envSustainLevel = static_cast<f32>(CentibelsToGain(static_cast<i32>(sustainCb)));
    envDelayEnd = static_cast<u32>(std::max(0.0, delayTime * outputRate));
    envAttackRate = (attackTime > 0.0) ? static_cast<f32>(1.0 / (attackTime * outputRate)) : 1.0f;
    envHoldEnd = static_cast<u32>(std::max(0.0, holdTime * outputRate));
    if (decayTime > 0.0) {
        const f64 sustainForCalc = std::max(static_cast<f64>(envSustainLevel), 1e-9);
        envDecayRate = static_cast<f32>(std::pow(sustainForCalc, 1.0 / (decayTime * outputRate)));
    } else {
        envDecayRate = 0.0f;
    }
    envReleaseTimeSeconds = static_cast<f32>(releaseTime);
    envReleaseRate = ComputeReleaseRate(1.0f, envReleaseTimeSeconds, outputSampleRate);

    f64 modDelayTime   = TimecentsToSeconds(gen[GEN_DelayModEnv]);
    f64 modAttackTime  = TimecentsToSeconds(gen[GEN_AttackModEnv]);
    f64 modHoldTime    = TimecentsToSeconds(gen[GEN_HoldModEnv]);
    f64 modDecayTime   = TimecentsToSeconds(gen[GEN_DecayModEnv]);
    f64 modReleaseTime = TimecentsToSeconds(gen[GEN_ReleaseModEnv]);
    const f64 modSustainRaw = static_cast<f64>(gen[GEN_SustainModEnv]);

    modHoldTime *= std::pow(2.0, static_cast<f64>(gen[GEN_KeynumToModEnvHold]) * (60 - effectiveKey) / 1200.0);
    modDecayTime *= std::pow(2.0, static_cast<f64>(gen[GEN_KeynumToModEnvDecay]) * (60 - effectiveKey) / 1200.0);

    modEnvDelayEnd = static_cast<u32>(std::max(0.0, modDelayTime * outputRate));
    modEnvAttackRate = (modAttackTime > 0.0) ? static_cast<f32>(1.0 / (modAttackTime * outputRate)) : 1.0f;
    modEnvHoldEnd = static_cast<u32>(std::max(0.0, modHoldTime * outputRate));
    modEnvSustainLevel = static_cast<f32>(std::clamp(1.0 - (modSustainRaw / 1000.0), 0.0, 1.0));
    if (modDecayTime > 0.0) {
        const f64 sustainForCalc = std::max(static_cast<f64>(modEnvSustainLevel), 1e-9);
        modEnvDecayRate = static_cast<f32>(std::pow(sustainForCalc, 1.0 / (modDecayTime * outputRate)));
    } else {
        modEnvDecayRate = 0.0f;
    }
    modEnvReleaseTimeSeconds = static_cast<f32>(modReleaseTime);
    modEnvReleaseRate = ComputeReleaseRate(1.0f, modEnvReleaseTimeSeconds, outputSampleRate);

    modLfoDelayEnd = static_cast<u32>(std::max(0.0, TimecentsToSeconds(gen[GEN_DelayModLFO]) * outputRate));
    modLfoPhaseStep = HertzToPhaseStep(CentsToHertz(gen[GEN_FreqModLFO]), outputSampleRate);
    modLfoToPitchCents = static_cast<f32>(gen[GEN_ModLfoToPitch]);
    modLfoToFilterFcCents = static_cast<f32>(gen[GEN_ModLfoToFilterFc]);
    modLfoToVolumeCb = static_cast<f32>(gen[GEN_ModLfoToVolume]);
    modEnvToPitchCents = static_cast<f32>(gen[GEN_ModEnvToPitch]);

    vibLfoDelayEnd = static_cast<u32>(std::max(0.0, TimecentsToSeconds(gen[GEN_DelayVibLFO]) * outputRate));
    vibLfoPhaseStep = HertzToPhaseStep(CentsToHertz(gen[GEN_FreqVibLFO]), outputSampleRate);
    vibLfoToPitchCents = static_cast<f32>(gen[GEN_VibLfoToPitch]);
}

void Voice::ApplyResolvedZoneControllerState(const ResolvedZone& zone, i32 effectiveKey) {
    const i32* gen = zone.generators;
    const f64 attenCb = static_cast<f64>(gen[GEN_InitialAttenuation]);
    attenuation = static_cast<f32>(AttenuationToGain(static_cast<i32>(attenCb)));
    if (zone.sample) attenuation *= zone.sample->loudnessGain;

    ApplyResolvedZoneEnvelopeParameters(gen, effectiveKey);

    filterBaseFcCents = std::clamp(gen[GEN_InitialFilterFc], kFilterFcMin, kFilterFcMax);
    filterQCb = std::clamp(gen[GEN_InitialFilterQ], kFilterQCbMin, kFilterQCbMax);
    filterModEnvToFcCents = gen[GEN_ModEnvToFilterFc];
    filterCurrentFcCents = filterBaseFcCents;
    useModEnv = (filterModEnvToFcCents != 0 || modEnvToPitchCents != 0.0f);
    filterEnabled = (filterCurrentFcCents < 13500 || filterModEnvToFcCents != 0 || modLfoToFilterFcCents != 0.0f);
    if (filterEnabled) {
        ComputeLowPassCoeffs(filterCurrentFcCents, filterQCb, outputSampleRate, filterB0, filterB1, filterB2, filterA1, filterA2);
    }

    f32 pan = std::clamp(static_cast<f32>(gen[GEN_Pan]) / 500.0f, -1.0f, 1.0f);
    if (specialRoute.enabled) {
        pan = specialRoute.pan;
    }
    ApplyPan(pan);
    presetReverbSend = NormalizeSf2EffectsSend(gen[GEN_ReverbEffectsSend]);
    presetChorusSend = NormalizeSf2EffectsSend(gen[GEN_ChorusEffectsSend]);
    exclusiveClass = static_cast<u8>(gen[GEN_ExclusiveClass]);
    RefreshOutputGains();
}

void Voice::NoteOn(const ResolvedZone& zone, const i16* pcmData, const i32* pcmData24, size_t pcmDataSize,
                   u16 bankNumber, u8 ch, u8 programNumber, u8 key, u16 vel, u32 newNoteId, u32 sampleRate, f64 pitchBendSemitones,
                   SoundBankKind newSoundBankKind, const SynthCompatOptions& compatOptions,
                   const SpecialVoiceRoute& newSpecialRoute,
                   i32 portamentoSourceKey, u8 portamentoTime, bool softPedalActive) {
    active         = true;
    bank           = bankNumber;
    channel        = ch;
    program        = programNumber;
    noteKey        = key;
    velocity       = vel;
    noteId         = newNoteId;
    sustainHeld    = false;
    sostenutoLatched = false;
    sostenutoHeld = false;
    soundBankKind  = newSoundBankKind;
    sampleData     = zone.sampleDataOverride ? zone.sampleDataOverride : pcmData;
    sampleData24   = zone.sampleData24Override ? zone.sampleData24Override : pcmData24;
    use24BitSamples = (sampleData24 != nullptr);
    sampleDataSize = (zone.sampleDataOverrideCount != 0) ? zone.sampleDataOverrideCount : pcmDataSize;
    outputSampleRate = sampleRate;
    this->compatOptions = compatOptions;
    specialRoute = newSpecialRoute;
    this->pitchBendSemitones = pitchBendSemitones;
    perNotePitchSemitones = 0.0; // NoteOn 毎にリセット
    portamentoOffsetSemitones = 0.0;
    portamentoStepSemitones = 0.0;
    portamentoSamplesRemaining = 0;
    usesLoopFallback = false;
    ignoreNoteOffUntilSampleEnd = false;
    envPhase      = EnvPhase::Delay;
    envLevel      = 0.0f;
    envSampleCount= 0;
    modEnvPhase      = EnvPhase::Delay;
    modEnvLevel      = 0.0f;
    modEnvSampleCount= 0;
    modEnvToPitchCents = 0.0f;
    modLfoDelayEnd = 0;
    modLfoSampleCount = 0;
    modLfoPhase = 0.0f;
    modLfoPhaseStep = 0.0f;
    modLfoToPitchCents = 0.0f;
    modLfoToFilterFcCents = 0.0f;
    modLfoToVolumeCb = 0.0f;
    vibLfoDelayEnd = 0;
    vibLfoSampleCount = 0;
    vibLfoPhase = 0.0f;
    vibLfoPhaseStep = 0.0f;
    vibLfoToPitchCents = 0.0f;

    std::array<i32, GEN_COUNT> effectiveGenerators{};
    std::memcpy(effectiveGenerators.data(), zone.generators, sizeof(zone.generators));
    if (compatOptions.enableSoftPedal && softPedalActive) {
        effectiveGenerators[GEN_InitialAttenuation] =
            std::clamp(effectiveGenerators[GEN_InitialAttenuation] + 80, 0, 1440);
        effectiveGenerators[GEN_InitialFilterFc] =
            std::clamp(effectiveGenerators[GEN_InitialFilterFc] - 200, kFilterFcMin, kFilterFcMax);
    }
    ResolvedZone effectiveZone = zone;
    std::memcpy(effectiveZone.generators, effectiveGenerators.data(), sizeof(effectiveZone.generators));
    const i32* gen = effectiveGenerators.data();
    const SampleHeader* smp = zone.sample;
    sampleHeader = smp;
    const u8 effectiveKeyU8 = ResolveForcedKey(key, gen);
    i32 effectiveKey = static_cast<i32>(effectiveKeyU8);

    // ---- サンプル範囲 ----
    i32 startOff     = gen[GEN_StartAddrsOffset]     + gen[GEN_StartAddrsCoarseOffset]   * 32768;
    i32 endOff       = gen[GEN_EndAddrsOffset]        + gen[GEN_EndAddrsCoarseOffset]     * 32768;
    i32 loopStartOff = gen[GEN_StartloopAddrsOffset] + gen[GEN_StartloopAddrsCoarse]     * 32768;
    i32 loopEndOff   = gen[GEN_EndloopAddrsOffset]   + gen[GEN_EndloopAddrsCoarse]       * 32768;

    const i32 sampleStartIndex = std::max<i32>(0, static_cast<i32>(smp->start) + startOff);
    const i32 sampleEndIndex = std::max<i32>(0, static_cast<i32>(smp->end) + endOff);
    const i32 loopStartIndex = std::max<i32>(0, static_cast<i32>(smp->loopStart) + loopStartOff);
    const i32 loopEndIndex = std::max<i32>(0, static_cast<i32>(smp->loopEnd) + loopEndOff);
    const i32 sampleDataLimit = static_cast<i32>(std::min<size_t>(pcmDataSize, static_cast<size_t>(std::numeric_limits<i32>::max())));

    if (sampleStartIndex >= sampleDataLimit || sampleEndIndex <= sampleStartIndex) {
        active = false;
        envPhase = EnvPhase::Off;
        return;
    }

    sampleEnd      = static_cast<u32>(std::min(sampleEndIndex, sampleDataLimit));
    loopStart      = static_cast<u32>(std::min(loopStartIndex, sampleDataLimit));
    loopEnd        = static_cast<u32>(std::min(loopEndIndex, sampleDataLimit));
    sampleEndFixed = ToFixedSamplePos(static_cast<i32>(sampleEnd));
    loopStartFixed = ToFixedSamplePos(static_cast<i32>(loopStart));
    loopEndFixed   = ToFixedSamplePos(static_cast<i32>(loopEnd));

    // SampleModes: 0=no loop, 1=continuous loop, 3=loop until release
    i32 sampleModes = gen[GEN_SampleModes] & 0x3;
    loopUntilRelease = (sampleModes == 3);
    looping = (sampleModes == 1 || sampleModes == 3);

    // 縮退ループガード: loopEnd <= loopStart はループ無効扱い
    if (looping && loopEnd <= loopStart + 1) {
        if (compatOptions.sf2ZeroLengthLoopRetrigger &&
            soundBankKind == SoundBankKind::Sf2 &&
            sampleModes != 0 &&
            sampleEnd > static_cast<u32>(sampleStartIndex + 1)) {
            loopStart = static_cast<u32>(sampleStartIndex);
            loopEnd = sampleEnd;
            usesLoopFallback = true;
        } else {
            looping = false;
        }
    }
    // サンプル終端がデータ末尾を超えないよう保護（念のため）
    if (sampleEnd < loopEnd) sampleEnd = loopEnd;
    sampleEndFixed = ToFixedSamplePos(static_cast<i32>(sampleEnd));
    loopStartFixed = ToFixedSamplePos(static_cast<i32>(loopStart));
    loopEndFixed = ToFixedSamplePos(static_cast<i32>(loopEnd));
    samplePosFixed = ToFixedSamplePos(sampleStartIndex);
    ignoreNoteOffUntilSampleEnd =
        (soundBankKind == SoundBankKind::Dls && zone.noTruncation && !looping);

    // ---- ピッチ計算 ----
    i32 rootKey = (gen[GEN_OverridingRootKey] >= 0)
                  ? gen[GEN_OverridingRootKey]
                  : smp->originalPitch;
    // ScaleTuning: cents/semitone (デフォルト100 = 通常の半音スケール)
    f64 scaleTuningFactor = static_cast<f64>(gen[GEN_ScaleTuning]) / 100.0;
    // SF2 sample header pitchCorrection is part of the sample's original pitch.
    // A positive correction means the recorded sample is sharper than originalPitch,
    // so playback for a target key must subtract it from the resampling offset.
    f64 fineTune          = static_cast<f64>(gen[GEN_FineTune]) - EffectiveSamplePitchCorrection(smp, this->compatOptions); // cents
    f64 coarseTune        = static_cast<f64>(gen[GEN_CoarseTune]);                        // semitones

    if (specialRoute.enabled && specialRoute.clampAboveRoot &&
        specialRoute.clampRootKey >= 0 && effectiveKey > specialRoute.clampRootKey) {
        effectiveKey = specialRoute.clampRootKey;
    }

    f64 baseSemitones = static_cast<f64>(effectiveKey - rootKey) * scaleTuningFactor
                      + coarseTune
                      + fineTune / 100.0;
    if (specialRoute.enabled) {
        baseSemitones += specialRoute.detuneSemitones;
    }

    // dwSampleRate == 0 は不正SF2: ゼロ除算を防ぎ無音で終了
    if (smp->sampleRate == 0 || sampleRate == 0) {
        active = false;
        return;
    }

    baseSampleStep = pow(2.0, baseSemitones / 12.0)
                   * static_cast<f64>(smp->sampleRate)
                   / static_cast<f64>(sampleRate);
    if (portamentoSourceKey >= 0 && portamentoTime > 0) {
        const f64 sourceSemitones = static_cast<f64>(portamentoSourceKey - rootKey) * scaleTuningFactor
                                  + coarseTune
                                  + fineTune / 100.0;
        portamentoOffsetSemitones = sourceSemitones - baseSemitones;
        const f64 t = static_cast<f64>(portamentoTime) / 127.0;
        const f64 glideSeconds = 0.012 + t * t * 0.75;
        portamentoSamplesRemaining = static_cast<u32>(std::max(1.0, glideSeconds * sampleRate));
        portamentoStepSemitones = (portamentoSamplesRemaining > 0)
            ? (-portamentoOffsetSemitones / static_cast<f64>(portamentoSamplesRemaining))
            : 0.0;
    }
    const f64 sampleStep = baseSampleStep * pow(2.0, pitchBendSemitones / 12.0);
    sampleStepFixed = static_cast<i64>(std::llround(sampleStep * 4294967296.0));

    ApplyResolvedZoneEnvelopeParameters(gen, effectiveKey);

    if (envDelayEnd == 0) {
        envPhase      = EnvPhase::Attack;
        envSampleCount = 0;
    }
    if (modEnvDelayEnd == 0) {
        modEnvPhase = EnvPhase::Attack;
        modEnvSampleCount = 0;
    }

    // ---- 初期ローパスフィルター ----
    filterEnabled = false;
    filterB0 = 1.0f;
    filterB1 = 0.0f;
    filterB2 = 0.0f;
    filterA1 = 0.0f;
    filterA2 = 0.0f;
    filterZ1 = 0.0f;
    filterZ2 = 0.0f;
    ApplyResolvedZoneControllerState(effectiveZone, effectiveKey);
    channelGainL = 1.0f;
    channelGainR = 1.0f;
    channelReverbSend = 0.0f;
    channelChorusSend = 0.0f;
    reverbSend = presetReverbSend;
    chorusSend = presetChorusSend;
    RefreshOutputGains();
}

void Voice::RefreshResolvedZoneControllers(const ResolvedZone& zone) {
    if (!active || zone.sample != sampleHeader) {
        return;
    }

    const i32* gen = zone.generators;
    const u8 effectiveKeyU8 = ResolveForcedKey(noteKey, gen);
    const i32 effectiveKey = static_cast<i32>(effectiveKeyU8);
    ApplyResolvedZoneControllerState(zone, effectiveKey);

    const i32 rootKey = (gen[GEN_OverridingRootKey] >= 0) ? gen[GEN_OverridingRootKey] : sampleHeader->originalPitch;
    const f64 scaleTuningFactor = static_cast<f64>(gen[GEN_ScaleTuning]) / 100.0;
    const f64 fineTune = static_cast<f64>(gen[GEN_FineTune]) - EffectiveSamplePitchCorrection(sampleHeader, compatOptions);
    const f64 coarseTune = static_cast<f64>(gen[GEN_CoarseTune]);
    f64 baseSemitones = static_cast<f64>(effectiveKey - rootKey) * scaleTuningFactor + coarseTune + fineTune / 100.0;
    if (specialRoute.enabled) {
        baseSemitones += specialRoute.detuneSemitones;
    }
    baseSampleStep = std::pow(2.0, baseSemitones / 12.0) *
                     static_cast<f64>(sampleHeader->sampleRate) / static_cast<f64>(outputSampleRate);
    sampleStepFixed = static_cast<i64>(std::llround(
        baseSampleStep * std::pow(2.0, (pitchBendSemitones + perNotePitchSemitones) / 12.0) * 4294967296.0));
    RefreshOutputGains();
}

void Voice::UpdateChannelMix(f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32) {
    channelReverbSend = NormalizeMidiSend(reverbSend32);
    channelChorusSend = NormalizeMidiSend(chorusSend32);

    if (soundBankKind == SoundBankKind::Sf2) {
        channelGainL = volumeFactor;
        channelGainR = volumeFactor;
        reverbSend = compatOptions.multiplySf2MidiEffectsSends
            ? std::clamp(presetReverbSend * channelReverbSend, 0.0f, 1.0f)
            : presetReverbSend;
        chorusSend = compatOptions.multiplySf2MidiEffectsSends
            ? std::clamp(presetChorusSend * channelChorusSend, 0.0f, 1.0f)
            : presetChorusSend;
    } else {
        // 32-bit pan を正規化: 0→-1.0(左全振り), 0xFFFFFFFF→+1.0(右全振り), center≈0.0
        f32 channelPan = static_cast<f32>(pan32) / 4294967295.0f * 2.0f - 1.0f;
        channelPan = std::max(-1.0f, std::min(1.0f, channelPan));

        f32 panGainL = std::sqrt(0.5f * (1.0f - channelPan));
        f32 panGainR = std::sqrt(0.5f * (1.0f + channelPan));
        channelGainL = volumeFactor * panGainL;
        channelGainR = volumeFactor * panGainR;
        reverbSend = MixEffectsSend(presetReverbSend, channelReverbSend, compatOptions);
        chorusSend = MixEffectsSend(presetChorusSend, channelChorusSend, compatOptions);
    }
    RefreshOutputGains();
}

void Voice::ApplyPan(f32 pan) {
    pan = std::clamp(pan, -1.0f, 1.0f);
    baseGainL = std::sqrt(0.5f * (1.0f - pan));
    baseGainR = std::sqrt(0.5f * (1.0f + pan));
}

void Voice::RefreshOutputGains() {
    const f32 baseVoiceGain = static_cast<f32>(attenuation);
    const f32 baseDryGainL = baseVoiceGain * baseGainL * channelGainL;
    const f32 baseDryGainR = baseVoiceGain * baseGainR * channelGainR;
    dryGainL = baseDryGainL;
    dryGainR = baseDryGainR;

    reverbGainL = baseDryGainL * reverbSend;
    reverbGainR = baseDryGainR * reverbSend;
    chorusGainL = baseDryGainL * chorusSend;
    chorusGainR = baseDryGainR * chorusSend;
}

void Voice::NoteOff() {
    if (envPhase != EnvPhase::Off && envPhase != EnvPhase::Release) {
        if (ignoreNoteOffUntilSampleEnd) {
            return;
        }
        if (loopUntilRelease) {
            looping = false;
        }
        envPhase       = EnvPhase::Release;
        envSampleCount = 0;
        const f32 effectiveReleaseTime =
            ((looping || loopUntilRelease) && envReleaseTimeSeconds < kMinimumLoopReleaseSeconds)
                ? kMinimumLoopReleaseSeconds
                : envReleaseTimeSeconds;
        envReleaseRate = ComputeReleaseRate(envLevel, effectiveReleaseTime, outputSampleRate);
        if (modEnvPhase != EnvPhase::Off && modEnvPhase != EnvPhase::Release) {
            modEnvPhase = EnvPhase::Release;
            modEnvSampleCount = 0;
            const f32 effectiveModReleaseTime =
                ((looping || loopUntilRelease) && modEnvReleaseTimeSeconds < kMinimumLoopReleaseSeconds)
                    ? kMinimumLoopReleaseSeconds
                    : modEnvReleaseTimeSeconds;
            modEnvReleaseRate = ComputeReleaseRate(modEnvLevel, effectiveModReleaseTime, outputSampleRate);
        }
    }
}

void Voice::Render(f32& outL, f32& outR, f32& reverbL, f32& reverbR, f32& chorusL, f32& chorusR) {
    RenderBlock(&outL, &outR, &reverbL, &reverbR, &chorusL, &chorusR, 1);
}

void Voice::RenderBlock(f32* outL, f32* outR, f32* reverbL, f32* reverbR, f32* chorusL, f32* chorusR, u32 numFrames) {
    if (!active || (sampleData == nullptr && sampleData24 == nullptr)) return;

    i64 localSamplePosFixed = samplePosFixed;
    const i64 localSampleStepFixed = sampleStepFixed;
    const i64 localLoopStartFixed = loopStartFixed;
    const i64 localLoopEndFixed = loopEndFixed;
    const i64 localSampleEndFixed = sampleEndFixed;
    const bool localLooping = looping;
    const bool localIntegralStep = (localSampleStepFixed & kSamplePosFracMask) == 0;
    const bool localUnitStep = localSampleStepFixed == kSamplePosFracOne;
    const bool localAvx2UnitStep = localUnitStep && Simd::HasAvx2();
    const i32 localFilterBaseFcCents = filterBaseFcCents;
    const i32 localFilterQCb = filterQCb;
    const i32 localFilterModEnvToFcCents = filterModEnvToFcCents;
    const u32 localOutputSampleRate = outputSampleRate;
    const bool localUseModEnv = useModEnv;
    const bool hasReverb = (reverbGainL != 0.0f || reverbGainR != 0.0f);
    const bool hasChorus = (chorusGainL != 0.0f || chorusGainR != 0.0f);
    const size_t nonLoopSampleDataLimit = ClampNonLoopSampleLimit(sampleDataSize, sampleEnd);

    EnvPhase localEnvPhase = envPhase;
    f32 localEnvLevel = envLevel;
    u32 localEnvSampleCount = envSampleCount;
    EnvPhase localModEnvPhase = modEnvPhase;
    f32 localModEnvLevel = modEnvLevel;
    u32 localModEnvSampleCount = modEnvSampleCount;
    u32 localModLfoSampleCount = modLfoSampleCount;
    f32 localModLfoPhase = modLfoPhase;
    u32 localVibLfoSampleCount = vibLfoSampleCount;
    f32 localVibLfoPhase = vibLfoPhase;
    f32 localFilterB0 = filterB0;
    f32 localFilterB1 = filterB1;
    f32 localFilterB2 = filterB2;
    f32 localFilterA1 = filterA1;
    f32 localFilterA2 = filterA2;
    f32 localFilterZ1 = filterZ1;
    f32 localFilterZ2 = filterZ2;
    i32 localFilterCurrentFcCents = filterCurrentFcCents;
    f64 localPortamentoOffsetSemitones = portamentoOffsetSemitones;
    f64 localPortamentoStepSemitones = portamentoStepSemitones;
    u32 localPortamentoSamplesRemaining = portamentoSamplesRemaining;
    const i64 sampleBoundaryFixed = localLooping ? localLoopEndFixed : (localSampleEndFixed - kSamplePosFracOne);

    auto commitState = [&](bool stillActive) {
        active = stillActive;
        envPhase = stillActive ? localEnvPhase : EnvPhase::Off;
        samplePosFixed = localSamplePosFixed;
        envLevel = localEnvLevel;
        envSampleCount = localEnvSampleCount;
        modEnvPhase = localModEnvPhase;
        modEnvLevel = localModEnvLevel;
        modEnvSampleCount = localModEnvSampleCount;
        modLfoSampleCount = localModLfoSampleCount;
        modLfoPhase = localModLfoPhase;
        vibLfoSampleCount = localVibLfoSampleCount;
        vibLfoPhase = localVibLfoPhase;
        filterB0 = localFilterB0;
        filterB1 = localFilterB1;
        filterB2 = localFilterB2;
        filterA1 = localFilterA1;
        filterA2 = localFilterA2;
        filterZ1 = localFilterZ1;
        filterZ2 = localFilterZ2;
        filterCurrentFcCents = localFilterCurrentFcCents;
        portamentoOffsetSemitones = localPortamentoOffsetSemitones;
        portamentoStepSemitones = localPortamentoStepSemitones;
        portamentoSamplesRemaining = localPortamentoSamplesRemaining;
    };
    const bool localUsePortamento = localPortamentoSamplesRemaining > 0 && std::fabs(localPortamentoOffsetSemitones) > 1.0e-6;
    const bool localUseLfo = localUsePortamento ||
                             (modLfoPhaseStep > 0.0f && (modLfoToPitchCents != 0.0f || modLfoToFilterFcCents != 0.0f || modLfoToVolumeCb != 0.0f)) ||
                             (vibLfoPhaseStep > 0.0f && vibLfoToPitchCents != 0.0f);

    if (use24BitSamples && sampleData24 != nullptr) {
        u32 offset = 0;
        while (offset < numFrames) {
            const u32 chunkFrames = numFrames - offset;

            for (u32 i = 0; i < chunkFrames; ++i) {
                while (localSamplePosFixed >= sampleBoundaryFixed) {
                    if (!localLooping) {
                        commitState(false);
                        return;
                    }
                    const i64 loopLenFixed = localLoopEndFixed - localLoopStartFixed;
                    if (loopLenFixed <= 0) {
                        commitState(false);
                        return;
                    }
                    localSamplePosFixed = localLoopStartFixed + ((localSamplePosFixed - localLoopStartFixed) % loopLenFixed);
                }

                AdvanceEnvelope(localEnvPhase, localEnvLevel, localEnvSampleCount, envDelayEnd, envAttackRate,
                                envHoldEnd, envDecayRate, envSustainLevel, envReleaseRate);
                if (localUseModEnv) {
                    AdvanceEnvelope(localModEnvPhase, localModEnvLevel, localModEnvSampleCount, modEnvDelayEnd, modEnvAttackRate,
                                    modEnvHoldEnd, modEnvDecayRate, modEnvSustainLevel, modEnvReleaseRate);
                }
                if (localEnvPhase == EnvPhase::Off) {
                    commitState(false);
                    return;
                }
                if (localEnvPhase == EnvPhase::Decay && envSustainLevel < 1e-9f && localEnvLevel < 1e-5f) {
                    commitState(false);
                    return;
                }

                const f32 normalized = localIntegralStep
                    ? Read24BitSampleNormalized(sampleData24, static_cast<size_t>(localSamplePosFixed >> kSamplePosFracBits))
                    : (localLooping
                        ? CubicInterpFixedLooped24Bit(sampleData24, localSamplePosFixed, sampleDataSize,
                                                      static_cast<size_t>(localLoopStartFixed >> kSamplePosFracBits),
                                                      static_cast<size_t>(localLoopEndFixed >> kSamplePosFracBits))
                        : CubicInterpFixed24Bit(sampleData24, localSamplePosFixed, nonLoopSampleDataLimit));
                const f32 modLfoValue = ComputeLfoValue(modLfoDelayEnd, localModLfoSampleCount, localModLfoPhase, modLfoPhaseStep);
                const f32 vibLfoValue = ComputeLfoValue(vibLfoDelayEnd, localVibLfoSampleCount, localVibLfoPhase, vibLfoPhaseStep);
                const f64 portamentoSemitones = localPortamentoOffsetSemitones;
                const f64 pitchOffsetSemitones =
                    pitchBendSemitones +
                    perNotePitchSemitones +
                    portamentoSemitones +
                    static_cast<f64>(modEnvToPitchCents * localModEnvLevel + modLfoToPitchCents * modLfoValue + vibLfoToPitchCents * vibLfoValue) / 100.0;
                const i64 dynamicSampleStepFixed = static_cast<i64>(std::llround(
                    baseSampleStep * std::pow(2.0, pitchOffsetSemitones / 12.0) * 4294967296.0));
                localSamplePosFixed += dynamicSampleStepFixed;
                if (localPortamentoSamplesRemaining > 0) {
                    --localPortamentoSamplesRemaining;
                    localPortamentoOffsetSemitones += localPortamentoStepSemitones;
                    if (localPortamentoSamplesRemaining == 0 || std::fabs(localPortamentoOffsetSemitones) < 1.0e-6) {
                        localPortamentoSamplesRemaining = 0;
                        localPortamentoOffsetSemitones = 0.0;
                        localPortamentoStepSemitones = 0.0;
                    }
                }

                f32 filteredSample = normalized;
                if (filterEnabled) {
                    const f32 dynamicOffset =
                        static_cast<f32>(localFilterModEnvToFcCents) * localModEnvLevel +
                        modLfoToFilterFcCents * modLfoValue;
                    const i32 dynamicFc = std::clamp(
                        localFilterBaseFcCents +
                        static_cast<i32>(dynamicOffset >= 0.0f ? dynamicOffset + 0.5f : dynamicOffset - 0.5f),
                        kFilterFcMin, kFilterFcMax);
                    if (dynamicFc != localFilterCurrentFcCents) {
                        localFilterCurrentFcCents = dynamicFc;
                        ComputeLowPassCoeffs(dynamicFc, localFilterQCb, localOutputSampleRate,
                                             localFilterB0, localFilterB1, localFilterB2, localFilterA1, localFilterA2);
                    }
                    filteredSample = ProcessFilterSample(
                        filteredSample,
                        localFilterB0, localFilterB1, localFilterB2, localFilterA1, localFilterA2,
                        localFilterZ1, localFilterZ2);
                }

                const i32 tremoloAttenCb = static_cast<i32>(std::max(0.0f, ((modLfoValue + 1.0f) * 0.5f) * modLfoToVolumeCb));
                const f32 tremoloGain = static_cast<f32>(AttenuationToGain(tremoloAttenCb));
                const f32 out = filteredSample * localEnvLevel * tremoloGain;
                const u32 frameIndex = offset + i;
                outL[frameIndex] += out * dryGainL;
                outR[frameIndex] += out * dryGainR;
                if (hasReverb) {
                    reverbL[frameIndex] += out * reverbGainL;
                    reverbR[frameIndex] += out * reverbGainR;
                }
                if (hasChorus) {
                    chorusL[frameIndex] += out * chorusGainL;
                    chorusR[frameIndex] += out * chorusGainR;
                }
            }

            offset += chunkFrames;
        }

        commitState(true);
        return;
    }

    if (localEnvPhase == EnvPhase::Sustain && !filterEnabled && !localUseModEnv && !localUseLfo) {
        const f32 outScale = static_cast<f32>(localEnvLevel) * kInvPcmScale;
        const f32 localDryGainL = outScale * dryGainL;
        const f32 localDryGainR = outScale * dryGainR;
        const f32 localReverbGainL = outScale * reverbGainL;
        const f32 localReverbGainR = outScale * reverbGainR;
        const f32 localChorusGainL = outScale * chorusGainL;
        const f32 localChorusGainR = outScale * chorusGainR;
        u32 offset = 0;
        while (offset < numFrames) {
            const u32 chunkFrames = ComputeRenderableFrames(localSamplePosFixed, localSampleStepFixed, sampleBoundaryFixed, numFrames - offset);
            if (chunkFrames == 0) {
                if (localLooping) {
                    const i64 loopLenFixed = localLoopEndFixed - localLoopStartFixed;
                    if (loopLenFixed <= 0) {
                        commitState(false);
                        return;
                    }
                    localSamplePosFixed = localLoopStartFixed + ((localSamplePosFixed - localLoopStartFixed) % loopLenFixed);
                    continue;
                }
                commitState(false);
                return;
            }

            if (!hasReverb && !hasChorus) {
                if (localAvx2UnitStep && TryMixConstantChunkAvx2<false, false>(
                        outL, outR, reverbL, reverbR, chorusL, chorusR,
                        sampleData, localSamplePosFixed, offset, chunkFrames,
                        localDryGainL, localDryGainR,
                        localReverbGainL, localReverbGainR,
                        localChorusGainL, localChorusGainR)) {
                    offset += chunkFrames;
                    continue;
                }

                if (localIntegralStep) {
                        MixConstantChunkScalar<false, false, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                } else {
                        MixConstantChunkScalar<false, false, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                }
            } else if (hasReverb && hasChorus) {
                if (localAvx2UnitStep && TryMixConstantChunkAvx2<true, true>(
                        outL, outR, reverbL, reverbR, chorusL, chorusR,
                        sampleData, localSamplePosFixed, offset, chunkFrames,
                        localDryGainL, localDryGainR,
                        localReverbGainL, localReverbGainR,
                        localChorusGainL, localChorusGainR)) {
                    offset += chunkFrames;
                    continue;
                }

                if (localIntegralStep) {
                        MixConstantChunkScalar<true, true, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                } else {
                        MixConstantChunkScalar<true, true, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                }
            } else if (hasReverb) {
                if (localAvx2UnitStep && TryMixConstantChunkAvx2<true, false>(
                        outL, outR, reverbL, reverbR, chorusL, chorusR,
                        sampleData, localSamplePosFixed, offset, chunkFrames,
                        localDryGainL, localDryGainR,
                        localReverbGainL, localReverbGainR,
                        localChorusGainL, localChorusGainR)) {
                    offset += chunkFrames;
                    continue;
                }

                if (localIntegralStep) {
                        MixConstantChunkScalar<true, false, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                } else {
                        MixConstantChunkScalar<true, false, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                }
            } else {
                if (localAvx2UnitStep && TryMixConstantChunkAvx2<false, true>(
                        outL, outR, reverbL, reverbR, chorusL, chorusR,
                        sampleData, localSamplePosFixed, offset, chunkFrames,
                        localDryGainL, localDryGainR,
                        localReverbGainL, localReverbGainR,
                        localChorusGainL, localChorusGainR)) {
                    offset += chunkFrames;
                    continue;
                }

                if (localIntegralStep) {
                        MixConstantChunkScalar<false, true, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                } else {
                        MixConstantChunkScalar<false, true, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localLooping ? sampleDataSize : nonLoopSampleDataLimit, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                }
            }

            offset += chunkFrames;
        }

        commitState(true);
        return;
    }

    if (!filterEnabled && !localUseModEnv && !localUseLfo) {
        const f32 pcmDryGainL = dryGainL * kInvPcmScale;
        const f32 pcmDryGainR = dryGainR * kInvPcmScale;
        const f32 pcmReverbGainL = reverbGainL * kInvPcmScale;
        const f32 pcmReverbGainR = reverbGainR * kInvPcmScale;
        const f32 pcmChorusGainL = chorusGainL * kInvPcmScale;
        const f32 pcmChorusGainR = chorusGainR * kInvPcmScale;
        u32 offset = 0;
        while (offset < numFrames) {
            const u32 boundaryFrames = ComputeRenderableFrames(localSamplePosFixed, localSampleStepFixed, sampleBoundaryFixed, numFrames - offset);
            if (boundaryFrames == 0) {
                if (localLooping) {
                    const i64 loopLenFixed = localLoopEndFixed - localLoopStartFixed;
                    if (loopLenFixed <= 0) {
                        commitState(false);
                        return;
                    }
                    localSamplePosFixed = localLoopStartFixed + ((localSamplePosFixed - localLoopStartFixed) % loopLenFixed);
                    continue;
                }
                commitState(false);
                return;
            }

            const u32 phaseFrames = FramesUntilPhaseChange(
                localEnvPhase, localEnvLevel, localEnvSampleCount,
                envDelayEnd, envAttackRate, envHoldEnd,
                envDecayRate, envSustainLevel, envReleaseRate);

            if (phaseFrames == 0) {
                switch (localEnvPhase) {
                case EnvPhase::Delay:
                    localEnvPhase = EnvPhase::Attack;
                    localEnvSampleCount = 0;
                    continue;
                case EnvPhase::Attack:
                    localEnvLevel = 1.0f;
                    localEnvPhase = (envHoldEnd > 0) ? EnvPhase::Hold : EnvPhase::Decay;
                    localEnvSampleCount = 0;
                    continue;
                case EnvPhase::Hold:
                    localEnvPhase = EnvPhase::Decay;
                    localEnvSampleCount = 0;
                    continue;
                case EnvPhase::Decay:
                    if (envSustainLevel < 1e-9f) {
                        commitState(false);
                        return;
                    }
                    localEnvLevel = envSustainLevel;
                    localEnvPhase = EnvPhase::Sustain;
                    continue;
                case EnvPhase::Release:
                    commitState(false);
                    return;
                case EnvPhase::Off:
                    commitState(false);
                    return;
                case EnvPhase::Sustain:
                    break;
                }
            }

            const u32 chunkFrames = std::min(boundaryFrames, phaseFrames);
            switch (localEnvPhase) {
            case EnvPhase::Delay:
                localSamplePosFixed += static_cast<i64>(chunkFrames) * localSampleStepFixed;
                localEnvSampleCount += chunkFrames;
                break;
            case EnvPhase::Hold:
            case EnvPhase::Sustain: {
                const f32 outScale = localEnvLevel * kInvPcmScale;
                const f32 localDryGainL = outScale * dryGainL;
                const f32 localDryGainR = outScale * dryGainR;
                const f32 localReverbGainL = outScale * reverbGainL;
                const f32 localReverbGainR = outScale * reverbGainR;
                const f32 localChorusGainL = outScale * chorusGainL;
                const f32 localChorusGainR = outScale * chorusGainR;
                if (!hasReverb && !hasChorus) {
                    if (localAvx2UnitStep && TryMixConstantChunkAvx2<false, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localSamplePosFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR)) {
                    } else if (localIntegralStep) {
                        MixConstantChunkScalar<false, false, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    } else {
                        MixConstantChunkScalar<false, false, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    }
                } else if (hasReverb && hasChorus) {
                    if (localAvx2UnitStep && TryMixConstantChunkAvx2<true, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localSamplePosFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR)) {
                    } else if (localIntegralStep) {
                        MixConstantChunkScalar<true, true, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    } else {
                        MixConstantChunkScalar<true, true, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    }
                } else if (hasReverb) {
                    if (localAvx2UnitStep && TryMixConstantChunkAvx2<true, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localSamplePosFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR)) {
                    } else if (localIntegralStep) {
                        MixConstantChunkScalar<true, false, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    } else {
                        MixConstantChunkScalar<true, false, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    }
                } else {
                    if (localAvx2UnitStep && TryMixConstantChunkAvx2<false, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, localSamplePosFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR)) {
                    } else if (localIntegralStep) {
                        MixConstantChunkScalar<false, true, true>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    } else {
                        MixConstantChunkScalar<false, true, false>(
                            outL, outR, reverbL, reverbR, chorusL, chorusR,
                            sampleData, sampleDataSize, localSamplePosFixed, localSampleStepFixed,
                            localLooping, localLoopStartFixed, localLoopEndFixed, offset, chunkFrames,
                            localDryGainL, localDryGainR,
                            localReverbGainL, localReverbGainR,
                            localChorusGainL, localChorusGainR);
                    }
                }
                if (localEnvPhase == EnvPhase::Hold) {
                    localEnvSampleCount += chunkFrames;
                }
                break;
            }
            case EnvPhase::Attack:
                if (localIntegralStep) {
                    for (u32 i = 0; i < chunkFrames; ++i) {
                        localEnvLevel = std::min(1.0f, localEnvLevel + envAttackRate);
                        const f32 sample = static_cast<f32>(sampleData[static_cast<size_t>(localSamplePosFixed >> kSamplePosFracBits)]);
                        localSamplePosFixed += localSampleStepFixed;

                        const f32 scaled = sample * localEnvLevel;
                        const u32 frameIndex = offset + i;
                        outL[frameIndex] += scaled * pcmDryGainL;
                        outR[frameIndex] += scaled * pcmDryGainR;
                        if (hasReverb) {
                            reverbL[frameIndex] += scaled * pcmReverbGainL;
                            reverbR[frameIndex] += scaled * pcmReverbGainR;
                        }
                        if (hasChorus) {
                            chorusL[frameIndex] += scaled * pcmChorusGainL;
                            chorusR[frameIndex] += scaled * pcmChorusGainR;
                        }
                    }
                } else {
                    for (u32 i = 0; i < chunkFrames; ++i) {
                        localEnvLevel = std::min(1.0f, localEnvLevel + envAttackRate);
                        const f32 sample = localLooping
                            ? CubicInterpFixedLooped(sampleData, localSamplePosFixed, sampleDataSize,
                                                     static_cast<size_t>(localLoopStartFixed >> kSamplePosFracBits),
                                                     static_cast<size_t>(localLoopEndFixed >> kSamplePosFracBits))
                            : CubicInterpFixed(sampleData, localSamplePosFixed, nonLoopSampleDataLimit);
                        localSamplePosFixed += localSampleStepFixed;

                        const f32 scaled = sample * localEnvLevel;
                        const u32 frameIndex = offset + i;
                        outL[frameIndex] += scaled * pcmDryGainL;
                        outR[frameIndex] += scaled * pcmDryGainR;
                        if (hasReverb) {
                            reverbL[frameIndex] += scaled * pcmReverbGainL;
                            reverbR[frameIndex] += scaled * pcmReverbGainR;
                        }
                        if (hasChorus) {
                            chorusL[frameIndex] += scaled * pcmChorusGainL;
                            chorusR[frameIndex] += scaled * pcmChorusGainR;
                        }
                    }
                }
                break;
            case EnvPhase::Decay:
            case EnvPhase::Release: {
                const f32 rate = (localEnvPhase == EnvPhase::Decay) ? envDecayRate : envReleaseRate;
                if (localIntegralStep) {
                    for (u32 i = 0; i < chunkFrames; ++i) {
                        localEnvLevel *= rate;
                        const f32 sample = static_cast<f32>(sampleData[static_cast<size_t>(localSamplePosFixed >> kSamplePosFracBits)]);
                        localSamplePosFixed += localSampleStepFixed;

                        const f32 scaled = sample * localEnvLevel;
                        const u32 frameIndex = offset + i;
                        outL[frameIndex] += scaled * pcmDryGainL;
                        outR[frameIndex] += scaled * pcmDryGainR;
                        if (hasReverb) {
                            reverbL[frameIndex] += scaled * pcmReverbGainL;
                            reverbR[frameIndex] += scaled * pcmReverbGainR;
                        }
                        if (hasChorus) {
                            chorusL[frameIndex] += scaled * pcmChorusGainL;
                            chorusR[frameIndex] += scaled * pcmChorusGainR;
                        }
                    }
                } else {
                    for (u32 i = 0; i < chunkFrames; ++i) {
                        localEnvLevel *= rate;
                        const f32 sample = localLooping
                            ? CubicInterpFixedLooped(sampleData, localSamplePosFixed, sampleDataSize,
                                                     static_cast<size_t>(localLoopStartFixed >> kSamplePosFracBits),
                                                     static_cast<size_t>(localLoopEndFixed >> kSamplePosFracBits))
                            : CubicInterpFixed(sampleData, localSamplePosFixed, nonLoopSampleDataLimit);
                        localSamplePosFixed += localSampleStepFixed;

                        const f32 scaled = sample * localEnvLevel;
                        const u32 frameIndex = offset + i;
                        outL[frameIndex] += scaled * pcmDryGainL;
                        outR[frameIndex] += scaled * pcmDryGainR;
                        if (hasReverb) {
                            reverbL[frameIndex] += scaled * pcmReverbGainL;
                            reverbR[frameIndex] += scaled * pcmReverbGainR;
                        }
                        if (hasChorus) {
                            chorusL[frameIndex] += scaled * pcmChorusGainL;
                            chorusR[frameIndex] += scaled * pcmChorusGainR;
                        }
                    }
                }
                break;
            }
            case EnvPhase::Off:
                commitState(false);
                return;
            }

            offset += chunkFrames;

            if (chunkFrames == phaseFrames) {
                switch (localEnvPhase) {
                case EnvPhase::Delay:
                    localEnvPhase = EnvPhase::Attack;
                    localEnvSampleCount = 0;
                    break;
                case EnvPhase::Attack:
                    localEnvLevel = 1.0f;
                    localEnvPhase = (envHoldEnd > 0) ? EnvPhase::Hold : EnvPhase::Decay;
                    localEnvSampleCount = 0;
                    break;
                case EnvPhase::Hold:
                    localEnvPhase = EnvPhase::Decay;
                    localEnvSampleCount = 0;
                    break;
                case EnvPhase::Decay:
                    if (envSustainLevel < 1e-9f) {
                        commitState(false);
                        return;
                    }
                    localEnvLevel = envSustainLevel;
                    localEnvPhase = EnvPhase::Sustain;
                    break;
                case EnvPhase::Release:
                    commitState(false);
                    return;
                default:
                    break;
                }
            }
        }

        commitState(true);
        return;
    }

    if (filterEnabled && !localUseModEnv && !localUseLfo) {
        u32 offset = 0;
        while (offset < numFrames) {
            const u32 chunkFrames = ComputeRenderableFrames(localSamplePosFixed, localSampleStepFixed, sampleBoundaryFixed, numFrames - offset);
            if (chunkFrames == 0) {
                if (localLooping) {
                    const i64 loopLenFixed = localLoopEndFixed - localLoopStartFixed;
                    if (loopLenFixed <= 0) {
                        commitState(false);
                        return;
                    }
                    localSamplePosFixed = localLoopStartFixed + ((localSamplePosFixed - localLoopStartFixed) % loopLenFixed);
                    continue;
                }
                commitState(false);
                return;
            }

            if (localIntegralStep) {
                for (u32 i = 0; i < chunkFrames; ++i) {
                    AdvanceEnvelope(localEnvPhase, localEnvLevel, localEnvSampleCount, envDelayEnd, envAttackRate,
                                    envHoldEnd, envDecayRate, envSustainLevel, envReleaseRate);
                    if (localEnvPhase == EnvPhase::Off) {
                        commitState(false);
                        return;
                    }
                    if (localEnvPhase == EnvPhase::Decay && envSustainLevel < 1e-9f && localEnvLevel < 1e-5f) {
                        commitState(false);
                        return;
                    }

                    const f32 sample = static_cast<f32>(sampleData[static_cast<size_t>(localSamplePosFixed >> kSamplePosFracBits)]);
                    localSamplePosFixed += localSampleStepFixed;

                    const f32 normalized = sample * kInvPcmScale;
                    const f32 filtered = ProcessFilterSample(
                        normalized,
                        localFilterB0, localFilterB1, localFilterB2, localFilterA1, localFilterA2,
                        localFilterZ1, localFilterZ2);
                    const f32 out = filtered * localEnvLevel;
                    const u32 frameIndex = offset + i;
                    outL[frameIndex] += out * dryGainL;
                    outR[frameIndex] += out * dryGainR;
                    if (hasReverb) {
                        reverbL[frameIndex] += out * reverbGainL;
                        reverbR[frameIndex] += out * reverbGainR;
                    }
                    if (hasChorus) {
                        chorusL[frameIndex] += out * chorusGainL;
                        chorusR[frameIndex] += out * chorusGainR;
                    }
                }
            } else {
                for (u32 i = 0; i < chunkFrames; ++i) {
                    AdvanceEnvelope(localEnvPhase, localEnvLevel, localEnvSampleCount, envDelayEnd, envAttackRate,
                                    envHoldEnd, envDecayRate, envSustainLevel, envReleaseRate);
                    if (localEnvPhase == EnvPhase::Off) {
                        commitState(false);
                        return;
                    }
                    if (localEnvPhase == EnvPhase::Decay && envSustainLevel < 1e-9f && localEnvLevel < 1e-5f) {
                        commitState(false);
                        return;
                    }

                    const f32 sample = localLooping
                        ? CubicInterpFixedLooped(sampleData, localSamplePosFixed, sampleDataSize,
                                                 static_cast<size_t>(localLoopStartFixed >> kSamplePosFracBits),
                                                 static_cast<size_t>(localLoopEndFixed >> kSamplePosFracBits))
                        : CubicInterpFixed(sampleData, localSamplePosFixed, nonLoopSampleDataLimit);
                    localSamplePosFixed += localSampleStepFixed;

                    const f32 normalized = sample * kInvPcmScale;
                    const f32 filtered = ProcessFilterSample(
                        normalized,
                        localFilterB0, localFilterB1, localFilterB2, localFilterA1, localFilterA2,
                        localFilterZ1, localFilterZ2);
                    const f32 out = filtered * localEnvLevel;
                    const u32 frameIndex = offset + i;
                    outL[frameIndex] += out * dryGainL;
                    outR[frameIndex] += out * dryGainR;
                    if (hasReverb) {
                        reverbL[frameIndex] += out * reverbGainL;
                        reverbR[frameIndex] += out * reverbGainR;
                    }
                    if (hasChorus) {
                        chorusL[frameIndex] += out * chorusGainL;
                        chorusR[frameIndex] += out * chorusGainR;
                    }
                }
            }

            offset += chunkFrames;
        }

        commitState(true);
        return;
    }

    u32 offset = 0;
    while (offset < numFrames) {
        const u32 chunkFrames = numFrames - offset;

        for (u32 i = 0; i < chunkFrames; ++i) {
            while (localSamplePosFixed >= sampleBoundaryFixed) {
                if (!localLooping) {
                    commitState(false);
                    return;
                }
                const i64 loopLenFixed = localLoopEndFixed - localLoopStartFixed;
                if (loopLenFixed <= 0) {
                    commitState(false);
                    return;
                }
                localSamplePosFixed = localLoopStartFixed + ((localSamplePosFixed - localLoopStartFixed) % loopLenFixed);
            }

            AdvanceEnvelope(localEnvPhase, localEnvLevel, localEnvSampleCount, envDelayEnd, envAttackRate,
                            envHoldEnd, envDecayRate, envSustainLevel, envReleaseRate);
            if (localUseModEnv) {
                AdvanceEnvelope(localModEnvPhase, localModEnvLevel, localModEnvSampleCount, modEnvDelayEnd, modEnvAttackRate,
                                modEnvHoldEnd, modEnvDecayRate, modEnvSustainLevel, modEnvReleaseRate);
            }
            if (localEnvPhase == EnvPhase::Off) {
                commitState(false);
                return;
            }
            if (localEnvPhase == EnvPhase::Decay && envSustainLevel < 1e-9f && localEnvLevel < 1e-5f) {
                commitState(false);
                return;
            }

            const f32 sample = localIntegralStep
                ? static_cast<f32>(sampleData[static_cast<size_t>(localSamplePosFixed >> kSamplePosFracBits)])
                : (localLooping
                    ? CubicInterpFixedLooped(sampleData, localSamplePosFixed, sampleDataSize,
                                             static_cast<size_t>(localLoopStartFixed >> kSamplePosFracBits),
                                             static_cast<size_t>(localLoopEndFixed >> kSamplePosFracBits))
                    : CubicInterpFixed(sampleData, localSamplePosFixed, nonLoopSampleDataLimit));
            const f32 modLfoValue = ComputeLfoValue(modLfoDelayEnd, localModLfoSampleCount, localModLfoPhase, modLfoPhaseStep);
            const f32 vibLfoValue = ComputeLfoValue(vibLfoDelayEnd, localVibLfoSampleCount, localVibLfoPhase, vibLfoPhaseStep);
            const f64 portamentoSemitones = localPortamentoOffsetSemitones;
            const f64 pitchOffsetSemitones =
                pitchBendSemitones +
                perNotePitchSemitones +
                portamentoSemitones +
                static_cast<f64>(modEnvToPitchCents * localModEnvLevel + modLfoToPitchCents * modLfoValue + vibLfoToPitchCents * vibLfoValue) / 100.0;
            const i64 dynamicSampleStepFixed = static_cast<i64>(std::llround(
                baseSampleStep * std::pow(2.0, pitchOffsetSemitones / 12.0) * 4294967296.0));
            localSamplePosFixed += dynamicSampleStepFixed;
            if (localPortamentoSamplesRemaining > 0) {
                --localPortamentoSamplesRemaining;
                localPortamentoOffsetSemitones += localPortamentoStepSemitones;
                if (localPortamentoSamplesRemaining == 0 || std::fabs(localPortamentoOffsetSemitones) < 1.0e-6) {
                    localPortamentoSamplesRemaining = 0;
                    localPortamentoOffsetSemitones = 0.0;
                    localPortamentoStepSemitones = 0.0;
                }
            }

            f32 normalized = sample * kInvPcmScale;
            if (filterEnabled) {
                const f32 dynamicOffset =
                    static_cast<f32>(localFilterModEnvToFcCents) * localModEnvLevel +
                    modLfoToFilterFcCents * modLfoValue;
                const i32 dynamicFc = std::clamp(
                    localFilterBaseFcCents +
                    static_cast<i32>(dynamicOffset >= 0.0f ? dynamicOffset + 0.5f : dynamicOffset - 0.5f),
                    kFilterFcMin, kFilterFcMax);
                if (dynamicFc != localFilterCurrentFcCents) {
                    localFilterCurrentFcCents = dynamicFc;
                    ComputeLowPassCoeffs(dynamicFc, localFilterQCb, localOutputSampleRate,
                                         localFilterB0, localFilterB1, localFilterB2, localFilterA1, localFilterA2);
                }
                normalized = ProcessFilterSample(
                    normalized,
                    localFilterB0, localFilterB1, localFilterB2, localFilterA1, localFilterA2,
                    localFilterZ1, localFilterZ2);
            }

            const i32 tremoloAttenCb = static_cast<i32>(std::max(0.0f, ((modLfoValue + 1.0f) * 0.5f) * modLfoToVolumeCb));
            const f32 tremoloGain = static_cast<f32>(AttenuationToGain(tremoloAttenCb));
            const f32 out = normalized * localEnvLevel * tremoloGain;
            const u32 frameIndex = offset + i;
            const f32 voiceL = out * dryGainL;
            const f32 voiceR = out * dryGainR;
            outL[frameIndex] += voiceL;
            outR[frameIndex] += voiceR;
            if (hasReverb) {
                reverbL[frameIndex] += out * reverbGainL;
                reverbR[frameIndex] += out * reverbGainR;
            }
            if (hasChorus) {
                chorusL[frameIndex] += out * chorusGainL;
                chorusR[frameIndex] += out * chorusGainR;
            }
        }

        offset += chunkFrames;
    }

    commitState(true);
}

} // namespace XArkMidi

