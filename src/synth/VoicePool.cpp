/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "VoicePool.h"
#include "SimdKernels.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <array>

namespace XArkMidi {

namespace {

constexpr const char* kProgramDebugLogPath = "./diagnostics/program_focus.log";
constexpr bool kEnableSpecialSf2Route = true;
constexpr f64 kSpecialLayerCenterPanThreshold = 0.05;
constexpr f64 kSpecialLayerFifthSemitones = 7.0;
constexpr f64 kSpecialLayerFifthTolerance = 0.35;
constexpr f64 kSpecialLayerDetuneSemitones = 0.035;
constexpr f32 kSpecialLayerPanSpread = 0.58f;
constexpr u16 kSf2SampleTypeMono = 1u;
constexpr u16 kSf2SampleTypeRight = 2u;
constexpr u16 kSf2SampleTypeLeft = 4u;
constexpr u16 kSf2SampleTypeLinked = 8u;

f64 EffectiveSamplePitchCorrection(const SampleHeader* sample, const SynthCompatOptions& compatOptions) {
    if (!sample || !compatOptions.enableSf2SamplePitchCorrection) {
        return 0.0;
    }
    return static_cast<f64>(sample->pitchCorrection);
}

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

bool IsTrackedRootVoice(const Voice& voice) {
    return !voice.ownedByParent;
}

f64 ResolveRealtimeChannelPitch(const Voice& voice, const ChannelState& state) {
    if (voice.soundBankKind == SoundBankKind::Sf2) {
        return state.ChannelTuningSemitones() + state.NoteTuningSemitones(voice.noteKey);
    }
    return state.TotalPitchSemitonesForKey(voice.noteKey);
}

void SynchronizeAggregatedLinkedVoice(Voice& root, Voice& linked) {
    if (root.specialRoute.preserveSampleTimeline || linked.specialRoute.preserveSampleTimeline) {
        return;
    }
    linked.samplePosFixed = root.samplePosFixed;
    linked.loopStart = root.loopStart;
    linked.loopEnd = root.loopEnd;
    linked.sampleEnd = root.sampleEnd;
    linked.loopStartFixed = root.loopStartFixed;
    linked.loopEndFixed = root.loopEndFixed;
    linked.sampleEndFixed = root.sampleEndFixed;
    linked.looping = root.looping;
    linked.loopUntilRelease = root.loopUntilRelease;
    linked.usesLoopFallback = root.usesLoopFallback;
    linked.ignoreNoteOffUntilSampleEnd = root.ignoreNoteOffUntilSampleEnd;
}

void KillLinkedVoiceIfPresent(Voice& voice, Voice (&voices)[MAX_VOICES]) {
    if (!voice.HasLinkedVoice()) {
        return;
    }
    voices[voice.linkedVoiceIndex].Kill();
    voice.ClearLinkedVoice();
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
        const char* value = std::getenv("XARKMIDI_ENABLE_PROGRAM_LOG");
        if (value && value[0] != '\0' && value[0] != '0') {
            return true;
        }
        value = std::getenv("XARKMIDI_ENABLE_SF2_PROGRAM_LOG");
        return value && value[0] != '\0' && value[0] != '0';
#endif
    }();
    return enabled;
}

const char* SoundBankKindName(SoundBankKind kind) {
    switch (kind) {
    case SoundBankKind::Sf2: return "sf2";
    case SoundBankKind::Dls: return "dls";
    default: return "auto";
    }
}

const char* EnvPhaseName(EnvPhase phase) {
    switch (phase) {
    case EnvPhase::Delay: return "delay";
    case EnvPhase::Attack: return "attack";
    case EnvPhase::Hold: return "hold";
    case EnvPhase::Decay: return "decay";
    case EnvPhase::Sustain: return "sustain";
    case EnvPhase::Release: return "release";
    case EnvPhase::Off: return "off";
    default: return "unknown";
    }
}

std::string SampleNameString(const SampleHeader* sample) {
    if (!sample) {
        return "(null)";
    }
    size_t length = 0;
    while (length < sizeof(sample->sampleName) && sample->sampleName[length] != '\0') {
        ++length;
    }
    while (length > 0 && sample->sampleName[length - 1] == ' ') {
        --length;
    }
    return std::string(sample->sampleName, sample->sampleName + length);
}

f64 ComputeZoneBaseSemitones(const ResolvedZone& zone, u8 key, const SynthCompatOptions& compatOptions) {
    if (!zone.sample) {
        return 0.0;
    }
    const i32* gen = zone.generators;
    const i32 rootKey = (gen[GEN_OverridingRootKey] >= 0) ? gen[GEN_OverridingRootKey] : zone.sample->originalPitch;
    const f64 scaleTuningFactor = static_cast<f64>(gen[GEN_ScaleTuning]) / 100.0;
    const f64 fineTune = static_cast<f64>(gen[GEN_FineTune]) - EffectiveSamplePitchCorrection(zone.sample, compatOptions);
    const f64 coarseTune = static_cast<f64>(gen[GEN_CoarseTune]);
    return static_cast<f64>(key - rootKey) * scaleTuningFactor + coarseTune + fineTune / 100.0;
}

bool HasPitchAnimation(const ResolvedZone& zone) {
    const i32* gen = zone.generators;
    return gen[GEN_ModLfoToPitch] != 0 ||
           gen[GEN_VibLfoToPitch] != 0 ||
           gen[GEN_ModEnvToPitch] != 0;
}

f32 ZonePanNormalized(const ResolvedZone& zone) {
    return std::clamp(static_cast<f32>(zone.generators[GEN_Pan]) / 500.0f, -1.0f, 1.0f);
}

i32 ZoneRootKey(const ResolvedZone& zone) {
    return (zone.generators[GEN_OverridingRootKey] >= 0)
        ? zone.generators[GEN_OverridingRootKey]
        : zone.sample->originalPitch;
}

u8 ZoneKeyHigh(const ResolvedZone& zone) {
    return zone.generators[GEN_KeyRange] < 0
        ? 127
        : static_cast<u8>((zone.generators[GEN_KeyRange] >> 8) & 0xFF);
}

struct SpecialRouteDecision {
    bool enabled = false;
    std::array<SpecialVoiceRoute, 2> routes{};
    f64 intervalSemitones = 0.0;
};

struct ProgramLayerEntry {
    const ResolvedZone* zone = nullptr;
    SpecialVoiceRoute route{};
};

struct ProgramLayerPlan {
    std::vector<ProgramLayerEntry> entries;
    bool aggregated = false;
};

bool IsExplicitStereoSampleType(u16 sampleType) {
    const u16 type = static_cast<u16>(sampleType & 0x7FFFu);
    return type == kSf2SampleTypeLeft || type == kSf2SampleTypeRight;
}

bool IsExplicitSf2StereoPair(const ResolvedZone& a, const ResolvedZone& b) {
    if (!a.sample || !b.sample) {
        return false;
    }

    const int sampleIdA = a.generators[GEN_SampleID];
    const int sampleIdB = b.generators[GEN_SampleID];
    if (sampleIdA < 0 || sampleIdB < 0 || sampleIdA == sampleIdB) {
        return false;
    }

    const u16 typeA = static_cast<u16>(a.sample->sampleType & 0x7FFFu);
    const u16 typeB = static_cast<u16>(b.sample->sampleType & 0x7FFFu);
    if (!IsExplicitStereoSampleType(typeA) || !IsExplicitStereoSampleType(typeB) || typeA == typeB) {
        return false;
    }

    if (a.sample->sampleLink != static_cast<u16>(sampleIdB) ||
        b.sample->sampleLink != static_cast<u16>(sampleIdA)) {
        return false;
    }

    return true;
}

bool TryAppendExplicitSf2StereoPairPlan(const ResolvedZone& a,
                                        const ResolvedZone& b,
                                        ProgramLayerPlan& outPlan) {
    if (!IsExplicitSf2StereoPair(a, b)) {
        return false;
    }

    outPlan.aggregated = true;
    outPlan.entries.reserve(2);
    if ((a.sample->sampleType & 0x7FFFu) == kSf2SampleTypeLeft) {
        outPlan.entries.push_back({&a, {true, 0.0, -1.0f, false, -1, true}});
        outPlan.entries.push_back({&b, {true, 0.0, 1.0f, false, -1, true}});
    } else {
        outPlan.entries.push_back({&b, {true, 0.0, -1.0f, false, -1, true}});
        outPlan.entries.push_back({&a, {true, 0.0, 1.0f, false, -1, true}});
    }
    return true;
}

bool PlanTargetsVoice(const ProgramLayerPlan& plan, const Voice& voice) {
    if (!voice.sampleHeader) {
        return false;
    }
    for (const auto& entry : plan.entries) {
        if (entry.zone && voice.MatchesResolvedZone(*entry.zone)) {
            return true;
        }
    }
    return false;
}

SpecialRouteDecision DetectSpecialSf2LayerRoute(const std::vector<ResolvedZone>& zones,
                                                SoundBankKind soundBankKind,
                                                u8 key,
                                                const SynthCompatOptions& compatOptions) {
    SpecialRouteDecision decision;
    if (!kEnableSpecialSf2Route) {
        return decision;
    }
    if (soundBankKind != SoundBankKind::Sf2 || zones.size() != 2) {
        return decision;
    }

    const ResolvedZone& zone0 = zones[0];
    const ResolvedZone& zone1 = zones[1];
    if (!zone0.sample || zone0.sample != zone1.sample) {
        return decision;
    }
    if (HasPitchAnimation(zone0) || HasPitchAnimation(zone1)) {
        return decision;
    }

    const f32 pan0 = ZonePanNormalized(zone0);
    const f32 pan1 = ZonePanNormalized(zone1);
    if (std::fabs(pan0) > kSpecialLayerCenterPanThreshold ||
        std::fabs(pan1) > kSpecialLayerCenterPanThreshold) {
        return decision;
    }

    const f64 semitones0 = ComputeZoneBaseSemitones(zone0, key, compatOptions);
    const f64 semitones1 = ComputeZoneBaseSemitones(zone1, key, compatOptions);
    const f64 interval = std::fabs(semitones1 - semitones0);
    if (std::fabs(interval - kSpecialLayerFifthSemitones) > kSpecialLayerFifthTolerance) {
        return decision;
    }

    const size_t lowIndex = (semitones0 <= semitones1) ? 0 : 1;
    const size_t highIndex = (lowIndex == 0) ? 1 : 0;
    decision.enabled = true;
    decision.intervalSemitones = interval;
    decision.routes[lowIndex] = {true, -kSpecialLayerDetuneSemitones, -kSpecialLayerPanSpread};
    decision.routes[highIndex] = {true, kSpecialLayerDetuneSemitones - interval, kSpecialLayerPanSpread};

    const i32 rootKey0 = ZoneRootKey(zone0);
    const i32 rootKey1 = ZoneRootKey(zone1);
    const u8 keyHigh0 = ZoneKeyHigh(zone0);
    const u8 keyHigh1 = ZoneKeyHigh(zone1);
    if (rootKey0 == rootKey1 && keyHigh0 == 127 && keyHigh1 == 127) {
        decision.routes[0].clampAboveRoot = true;
        decision.routes[0].clampRootKey = rootKey0;
        decision.routes[1].clampAboveRoot = true;
        decision.routes[1].clampRootKey = rootKey1;
    }
    return decision;
}

void AppendSpecialRouteDebugLog(u8 channel,
                                u8 program,
                                u8 key,
                                u32 noteId,
                                const std::vector<ResolvedZone>& zones,
                                const SpecialRouteDecision& decision) {
    if (!IsProgramLoggingEnabled() || !decision.enabled || zones.size() != 2) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(kProgramDebugLogPath).parent_path());
    std::ofstream log(kProgramDebugLogPath, std::ios::app);
    if (!log) {
        return;
    }

    log << "  special_sf2_route"
        << " ch=" << static_cast<int>(channel)
        << " program=" << static_cast<int>(program)
        << " key=" << static_cast<int>(key)
        << " note_id=" << noteId
        << " sample_name=\"" << SampleNameString(zones[0].sample) << "\""
        << " interval_semitones=" << decision.intervalSemitones
        << " route0_detune=" << decision.routes[0].detuneSemitones
        << " route0_pan=" << decision.routes[0].pan
        << " route1_detune=" << decision.routes[1].detuneSemitones
        << " route1_pan=" << decision.routes[1].pan
        << '\n';
}

std::vector<ProgramLayerPlan> BuildProgramLayerPlans(const std::vector<ResolvedZone>& zones,
                                                     SoundBankKind soundBankKind,
                                                     u8 key,
                                                     const SynthCompatOptions& compatOptions) {
    std::vector<ProgramLayerPlan> plans;
    if (zones.empty()) {
        return plans;
    }

    std::vector<bool> paired(zones.size(), false);
    if (soundBankKind == SoundBankKind::Sf2) {
        for (size_t i = 0; i < zones.size(); ++i) {
            if (paired[i]) {
                continue;
            }
            for (size_t j = i + 1; j < zones.size(); ++j) {
                if (paired[j]) {
                    continue;
                }
                ProgramLayerPlan explicitStereoPlan;
                if (!TryAppendExplicitSf2StereoPairPlan(zones[i], zones[j], explicitStereoPlan)) {
                    continue;
                }
                paired[i] = true;
                paired[j] = true;
                plans.push_back(std::move(explicitStereoPlan));
                break;
            }
        }
    }

    bool hasExplicitStereoPlan = false;
    for (const auto& plan : plans) {
        if (plan.aggregated && plan.entries.size() == 2) {
            hasExplicitStereoPlan = true;
            break;
        }
    }

    if (!hasExplicitStereoPlan) {
        const SpecialRouteDecision specialRouteDecision =
            DetectSpecialSf2LayerRoute(zones, soundBankKind, key, compatOptions);
        if (specialRouteDecision.enabled) {
            ProgramLayerPlan aggregatedPlan;
            aggregatedPlan.aggregated = true;
            aggregatedPlan.entries.reserve(zones.size());
            for (size_t i = 0; i < zones.size(); ++i) {
                aggregatedPlan.entries.push_back({
                    &zones[i],
                    (i < specialRouteDecision.routes.size()) ? specialRouteDecision.routes[i] : SpecialVoiceRoute{}
                });
            }
            plans.push_back(std::move(aggregatedPlan));
            return plans;
        }
    }

    plans.reserve(zones.size());
    for (size_t i = 0; i < zones.size(); ++i) {
        if (paired[i]) {
            continue;
        }
        ProgramLayerPlan plan;
        plan.entries.push_back({&zones[i], {}});
        plans.push_back(std::move(plan));
    }
    return plans;
}

void AppendProgramLayerDebugLog(u8 channel,
                                u8 program,
                                u8 key,
                                u32 noteId,
                                const std::vector<ProgramLayerPlan>& plans) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(kProgramDebugLogPath).parent_path());
    std::ofstream log(kProgramDebugLogPath, std::ios::app);
    if (!log) {
        return;
    }

    log << "  program_layer_plan"
        << " ch=" << static_cast<int>(channel)
        << " program=" << static_cast<int>(program)
        << " key=" << static_cast<int>(key)
        << " note_id=" << noteId
        << " layer_count=" << plans.size()
        << '\n';

    for (size_t layerIndex = 0; layerIndex < plans.size(); ++layerIndex) {
        const auto& plan = plans[layerIndex];
        log << "    layer[" << layerIndex << "]"
            << " aggregated=" << (plan.aggregated ? 1 : 0)
            << " zone_count=" << plan.entries.size();
        if (!plan.entries.empty() && plan.entries[0].zone) {
            log << " sample_name=\"" << SampleNameString(plan.entries[0].zone->sample) << "\"";
        }
        log << '\n';
    }
}

void AppendActiveProgramNoteSummaryLog(const Voice (&voices)[MAX_VOICES],
                                       const std::array<u16, MAX_VOICES>& activeIndices,
                                       u16 activeCount,
                                       u8 channel,
                                       u8 program,
                                       u8 key,
                                       u32 noteId) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(kProgramDebugLogPath).parent_path());
    std::ofstream log(kProgramDebugLogPath, std::ios::app);
    if (!log) {
        return;
    }

    log << "  active_program_notes"
        << " ch=" << static_cast<int>(channel)
        << " program=" << static_cast<int>(program)
        << " trigger_key=" << static_cast<int>(key)
        << " trigger_note_id=" << noteId;

    bool any = false;
    for (u16 i = 0; i < activeCount; ++i) {
        const Voice& voice = voices[activeIndices[i]];
        if (!IsTrackedRootVoice(voice) || !voice.active) {
            continue;
        }
        if (voice.channel != channel || voice.program != program) {
            continue;
        }
        if (!any) {
            log << " notes=";
            any = true;
        } else {
            log << ",";
        }
        log << "{note_id=" << voice.noteId
            << " key=" << static_cast<int>(voice.noteKey)
        << " slot=" << activeIndices[i];
        if (voice.HasLinkedVoice()) {
            log << " linked_slot=" << voice.linkedVoiceIndex;
        }
        log << "}";
    }
    if (!any) {
        log << " notes=(none)";
    }
    log << '\n';
}

void AppendVoiceDebugLog(size_t zoneIndex,
                         u16 voiceIndex,
                         const Voice& voice) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(kProgramDebugLogPath).parent_path());
    std::ofstream log(kProgramDebugLogPath, std::ios::app);
    if (!log) {
        return;
    }

    log << "  voice[" << zoneIndex << "]"
        << " slot=" << voiceIndex
        << " bank_kind=" << SoundBankKindName(voice.soundBankKind)
        << " ch=" << static_cast<int>(voice.channel)
        << " program=" << static_cast<int>(voice.program)
        << " key=" << static_cast<int>(voice.noteKey)
        << " vel=" << static_cast<int>(voice.velocity)
        << " note_id=" << voice.noteId
        << " sample_name=\"" << SampleNameString(voice.sampleHeader) << "\""
        << " sample_start=" << voice.samplePosFixed
        << " loop_start=" << voice.loopStart
        << " loop_end=" << voice.loopEnd
        << " sample_end=" << voice.sampleEnd
        << " looping=" << (voice.looping ? 1 : 0)
        << " loop_until_release=" << (voice.loopUntilRelease ? 1 : 0)
        << " loop_fallback=" << (voice.usesLoopFallback ? 1 : 0)
        << " no_noteoff_until_end=" << (voice.ignoreNoteOffUntilSampleEnd ? 1 : 0)
        << " base_sample_step=" << voice.baseSampleStep
        << " sample_step_fixed=" << voice.sampleStepFixed
        << " pitch_bend_semitones=" << voice.pitchBendSemitones
        << " per_note_pitch_semitones=" << voice.perNotePitchSemitones
        << " portamento_offset_semitones=" << voice.portamentoOffsetSemitones
        << " portamento_step_semitones=" << voice.portamentoStepSemitones
        << " portamento_samples_remaining=" << voice.portamentoSamplesRemaining
        << " mod_env_to_pitch_cents=" << voice.modEnvToPitchCents
        << " mod_lfo_to_pitch_cents=" << voice.modLfoToPitchCents
        << " vib_lfo_to_pitch_cents=" << voice.vibLfoToPitchCents
        << " mod_lfo_phase_step=" << voice.modLfoPhaseStep
        << " vib_lfo_phase_step=" << voice.vibLfoPhaseStep
        << " env_phase=" << EnvPhaseName(voice.envPhase)
        << " env_level=" << voice.envLevel
        << " attenuation=" << voice.attenuation
        << " filter_enabled=" << (voice.filterEnabled ? 1 : 0)
        << " filter_fc_cents=" << voice.filterCurrentFcCents
        << " filter_q_cb=" << voice.filterQCb
        << " base_gain_l=" << voice.baseGainL
        << " base_gain_r=" << voice.baseGainR
        << " special_route=" << (voice.specialRoute.enabled ? 1 : 0)
        << " special_detune=" << voice.specialRoute.detuneSemitones
        << " special_pan=" << voice.specialRoute.pan
        << " special_clamp_above_root=" << (voice.specialRoute.clampAboveRoot ? 1 : 0)
        << " special_clamp_root=" << voice.specialRoute.clampRootKey
        << " owned_by_parent=" << (voice.ownedByParent ? 1 : 0)
        << " linked_voice_index=" << voice.linkedVoiceIndex
        << " channel_gain_l=" << voice.channelGainL
        << " channel_gain_r=" << voice.channelGainR
        << " dry_gain_l=" << voice.dryGainL
        << " dry_gain_r=" << voice.dryGainR
        << " reverb_send=" << voice.reverbSend
        << " chorus_send=" << voice.chorusSend
        << " reverb_gain_l=" << voice.reverbGainL
        << " reverb_gain_r=" << voice.reverbGainR
        << " chorus_gain_l=" << voice.chorusGainL
        << " chorus_gain_r=" << voice.chorusGainR
        << " audibility=" << EstimateVoiceAudibility(voice)
        << '\n';
}

void AppendPitchDebugLog(const char* eventName,
                         u16 voiceIndex,
                         const Voice& voice,
                         f64 previousChannelPitchSemitones,
                         f64 previousPerNotePitchSemitones,
                         i64 previousSampleStepFixed,
                         f64 targetSemitones,
                         u32 pitchBend32,
                         u8 pitchBendRangeSemitones,
                         u8 pitchBendRangeCents,
                         i16 noteTuningCents) {
    if (!IsProgramLoggingEnabled()) {
        return;
    }

    std::filesystem::create_directories(std::filesystem::path(kProgramDebugLogPath).parent_path());
    std::ofstream log(kProgramDebugLogPath, std::ios::app);
    if (!log) {
        return;
    }

    log << "  " << eventName
        << " slot=" << voiceIndex
        << " bank_kind=" << SoundBankKindName(voice.soundBankKind)
        << " ch=" << static_cast<int>(voice.channel)
        << " program=" << static_cast<int>(voice.program)
        << " key=" << static_cast<int>(voice.noteKey)
        << " note_id=" << voice.noteId
        << " sample_name=\"" << SampleNameString(voice.sampleHeader) << "\""
        << " pitch_bend32=0x" << std::hex << pitchBend32 << std::dec
        << " bend_range=" << static_cast<int>(pitchBendRangeSemitones)
        << "." << static_cast<int>(pitchBendRangeCents)
        << " note_tuning_cents=" << noteTuningCents
        << " target_semitones=" << targetSemitones
        << " prev_channel_pitch=" << previousChannelPitchSemitones
        << " new_channel_pitch=" << voice.pitchBendSemitones
        << " prev_per_note_pitch=" << previousPerNotePitchSemitones
        << " new_per_note_pitch=" << voice.perNotePitchSemitones
        << " base_sample_step=" << voice.baseSampleStep
        << " prev_sample_step_fixed=" << previousSampleStepFixed
        << " new_sample_step_fixed=" << voice.sampleStepFixed
        << '\n';
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
        if (!voices_[i].active && !voices_[i].ownedByParent) return &voices_[i];
    }

    auto pickVoice = [&](auto&& predicate) -> Voice* {
        Voice* steal = nullptr;
        f32 minAudibility = std::numeric_limits<f32>::max();
        u32 oldestNoteId = std::numeric_limits<u32>::max();
        for (u16 i = 0; i < activeCount_; ++i) {
            auto& v = voices_[activeIndices_[i]];
            if (!IsTrackedRootVoice(v) || !predicate(v)) {
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
        KillLinkedVoiceIfPresent(*steal, voices_);
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
            if (!IsTrackedRootVoice(v) ||
                !IsSameNote(v, channel, key) || v.envPhase == EnvPhase::Release || v.envPhase == EnvPhase::Off) {
                continue;
            }
            if (!steal || v.noteId < oldestNoteId) {
                steal = &v;
                oldestNoteId = v.noteId;
            }
        }
        if (steal) {
            const u16 index = static_cast<u16>(steal - voices_);
            KillLinkedVoiceIfPresent(*steal, voices_);
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

void VoicePool::NoteOn(const std::vector<ResolvedZone>& zones, const i16* sampleData, const i32* sampleData24, size_t sampleDataSize,
                        u16 bank, u8 channel, u8 program, u8 key, u16 velocity, u32 sampleRate,
                        f64 pitchBendSemitones,
                        f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32, SoundBankKind soundBankKind,
                        const SynthCompatOptions& compatOptions,
                        i32 portamentoSourceKey, u8 portamentoTime, bool softPedalActive) {
    if (zones.empty()) {
        return;
    }
    if (compatOptions.sf2ZeroLengthLoopRetrigger) {
        for (u16 i = 0; i < activeCount_;) {
            const u16 voiceIndex = activeIndices_[i];
            auto& v = voices_[voiceIndex];
            if (v.channel != channel || v.noteKey != key) {
                ++i;
                continue;
            }
            if (v.HasLinkedVoice()) {
                voices_[v.linkedVoiceIndex].Kill();
                v.ClearLinkedVoice();
            }
            v.Kill();
            UntrackVoice(voiceIndex);
        }
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
            if (v.HasLinkedVoice()) {
                voices_[v.linkedVoiceIndex].NoteOff();
            }
        }
    }
    u32 noteId = nextNoteId_++;
    noteQueue_[channel][key].push_back(noteId);
    const SpecialRouteDecision specialRouteDecision = DetectSpecialSf2LayerRoute(zones, soundBankKind, key, compatOptions);
    if (specialRouteDecision.enabled) {
        AppendSpecialRouteDebugLog(channel, program, key, noteId, zones, specialRouteDecision);
    }
    const std::vector<ProgramLayerPlan> layerPlans = BuildProgramLayerPlans(zones, soundBankKind, key, compatOptions);

    for (const auto& layerPlan : layerPlans) {
        if (!layerPlan.aggregated || layerPlan.entries.size() != 2 ||
            !layerPlan.entries[0].zone || !layerPlan.entries[1].zone) {
            continue;
        }
        if (!layerPlan.entries[0].route.clampAboveRoot ||
            !layerPlan.entries[1].route.clampAboveRoot) {
            continue;
        }
        for (u16 i = 0; i < activeCount_; ++i) {
            auto& v = voices_[activeIndices_[i]];
            if (!IsTrackedRootVoice(v) || !v.active) {
                continue;
            }
            if (v.channel != channel || v.program != program || v.noteKey != key) {
                continue;
            }
            if (v.envPhase == EnvPhase::Release || v.envPhase == EnvPhase::Off) {
                continue;
            }
            if (!PlanTargetsVoice(layerPlan, v)) {
                continue;
            }
            v.noteId = noteId;
            v.velocity = velocity;
            v.sustainHeld = false;
            v.sostenutoHeld = false;
            v.sostenutoLatched = false;
            if (v.HasLinkedVoice()) {
                auto& linked = voices_[v.linkedVoiceIndex];
                linked.noteId = noteId;
                linked.velocity = velocity;
                linked.sustainHeld = false;
                linked.sostenutoHeld = false;
                linked.sostenutoLatched = false;
            }
            AppendProgramLayerDebugLog(channel, program, key, noteId, layerPlans);
            AppendActiveProgramNoteSummaryLog(voices_, activeIndices_, activeCount_, channel, program, key, noteId);
            return;
        }
    }

    bool hasAggregatedPlan = false;
    for (const auto& layerPlan : layerPlans) {
        if (!layerPlan.aggregated) {
            continue;
        }
        hasAggregatedPlan = true;
        for (u16 i = 0; i < activeCount_; ++i) {
            auto& v = voices_[activeIndices_[i]];
            if (!IsTrackedRootVoice(v) || !v.active) {
                continue;
            }
            if (v.channel != channel || v.program != program) {
                continue;
            }
            if (!PlanTargetsVoice(layerPlan, v)) {
                continue;
            }
            const bool sameKeyRetrigger = (v.noteKey == key);
            if (!sameKeyRetrigger &&
                (v.envPhase == EnvPhase::Release || v.envPhase == EnvPhase::Off)) {
                continue;
            }
            const bool hasLinkedVoice = v.HasLinkedVoice();
            const u16 linkedVoiceIndex = v.linkedVoiceIndex;
            v.sustainHeld = false;
            v.sostenutoHeld = false;
            v.sostenutoLatched = false;
            if (sameKeyRetrigger) {
                if (hasLinkedVoice) {
                    auto& linked = voices_[linkedVoiceIndex];
                    linked.sustainHeld = false;
                    linked.sostenutoHeld = false;
                    linked.sostenutoLatched = false;
                    linked.Kill();
                }
                v.Kill();
            } else {
                v.NoteOff();
                if (hasLinkedVoice) {
                    auto& linked = voices_[linkedVoiceIndex];
                    linked.sustainHeld = false;
                    linked.sostenutoHeld = false;
                    linked.sostenutoLatched = false;
                    linked.NoteOff();
                }
            }
        }
    }

    AppendProgramLayerDebugLog(channel, program, key, noteId, layerPlans);
    AppendActiveProgramNoteSummaryLog(voices_, activeIndices_, activeCount_, channel, program, key, noteId);

    size_t zoneLogIndex = 0;
    for (const auto& layerPlan : layerPlans) {
        if (layerPlan.aggregated && layerPlan.entries.size() == 2 &&
            layerPlan.entries[0].zone && layerPlan.entries[1].zone) {
            const bool collapseHighClampedPair =
                layerPlan.entries[0].route.clampAboveRoot &&
                layerPlan.entries[1].route.clampAboveRoot;
            if (collapseHighClampedPair) {
                const size_t primaryIndex =
                    (std::fabs(layerPlan.entries[0].route.detuneSemitones) <=
                     std::fabs(layerPlan.entries[1].route.detuneSemitones))
                        ? 0
                        : 1;
                const auto& primaryEntry = layerPlan.entries[primaryIndex];
                const u8 entryExclusiveClass = static_cast<u8>(primaryEntry.zone->generators[GEN_ExclusiveClass]);
                if (entryExclusiveClass != 0) {
                    KillExclusiveClass(channel, entryExclusiveClass);
                }
                Voice* v = AllocVoice(channel, key);
                if (!v) return;
                const u16 voiceIndex = static_cast<u16>(v - voices_);
                v->NoteOn(*primaryEntry.zone, sampleData, sampleData24, sampleDataSize, bank, channel, program, key, velocity, noteId, sampleRate,
                          pitchBendSemitones, soundBankKind, compatOptions, primaryEntry.route,
                          portamentoSourceKey, portamentoTime, softPedalActive);
                if (v->active) {
                    v->UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
                    AppendVoiceDebugLog(zoneLogIndex, voiceIndex, *v);
                    TrackVoice(voiceIndex);
                }
                ++zoneLogIndex;
                continue;
            }

            const u8 rootExclusiveClass = static_cast<u8>(layerPlan.entries[0].zone->generators[GEN_ExclusiveClass]);
            const u8 linkedExclusiveClass = static_cast<u8>(layerPlan.entries[1].zone->generators[GEN_ExclusiveClass]);
            if (rootExclusiveClass != 0) {
                KillExclusiveClass(channel, rootExclusiveClass);
            }
            if (linkedExclusiveClass != 0 && linkedExclusiveClass != rootExclusiveClass) {
                KillExclusiveClass(channel, linkedExclusiveClass);
            }
            Voice* root = AllocVoice(channel, key);
            if (!root) return;
            const u16 rootIndex = static_cast<u16>(root - voices_);
            root->NoteOn(*layerPlan.entries[0].zone, sampleData, sampleData24, sampleDataSize, bank, channel, program, key, velocity, noteId, sampleRate,
                         pitchBendSemitones, soundBankKind, compatOptions, layerPlan.entries[0].route,
                         portamentoSourceKey, portamentoTime, softPedalActive);
            if (!root->active) {
                root->Kill();
                return;
            }

            Voice* linked = AllocVoice(channel, key);
            if (!linked) {
                root->Kill();
                return;
            }

            const u16 linkedIndex = static_cast<u16>(linked - voices_);
            linked->NoteOn(*layerPlan.entries[1].zone, sampleData, sampleData24, sampleDataSize, bank, channel, program, key, velocity, noteId, sampleRate,
                           pitchBendSemitones, soundBankKind, compatOptions, layerPlan.entries[1].route,
                           portamentoSourceKey, portamentoTime, softPedalActive);
            if (!linked->active) {
                root->Kill();
                linked->Kill();
                return;
            }

            root->ownedByParent = false;
            root->ClearLinkedVoice();
            root->UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
            linked->UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
            linked->ownedByParent = true;
            root->LinkVoice(linkedIndex);
            SynchronizeAggregatedLinkedVoice(*root, *linked);
            AppendVoiceDebugLog(zoneLogIndex, rootIndex, *root);
            AppendVoiceDebugLog(zoneLogIndex + 1, linkedIndex, *linked);
            TrackVoice(rootIndex);
            zoneLogIndex += 2;
            continue;
        }

        for (const auto& entry : layerPlan.entries) {
            if (!entry.zone) {
                continue;
            }
            const u8 entryExclusiveClass = static_cast<u8>(entry.zone->generators[GEN_ExclusiveClass]);
            if (entryExclusiveClass != 0) {
                KillExclusiveClass(channel, entryExclusiveClass);
            }
            Voice* v = AllocVoice(channel, key);
            if (!v) return; // ボイス確保失敗
            const u16 voiceIndex = static_cast<u16>(v - voices_);
            v->NoteOn(*entry.zone, sampleData, sampleData24, sampleDataSize, bank, channel, program, key, velocity, noteId, sampleRate,
                      pitchBendSemitones, soundBankKind, compatOptions, entry.route,
                      portamentoSourceKey, portamentoTime, softPedalActive);
            if (v->active) {
                v->UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
                AppendVoiceDebugLog(zoneLogIndex, voiceIndex, *v);
                TrackVoice(voiceIndex);
            }
            ++zoneLogIndex;
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
            if (v.HasLinkedVoice()) {
                voices_[v.linkedVoiceIndex].NoteOff();
            }
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
        if (v.HasLinkedVoice()) {
            voices_[v.linkedVoiceIndex].NoteOff();
        }
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
            if (v.HasLinkedVoice()) {
                voices_[v.linkedVoiceIndex].NoteOff();
            }
        }
    }
}

void VoicePool::KillExclusiveClass(u8 channel, u8 excClass) {
    for (u16 i = 0; i < activeCount_;) {
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (!IsTrackedRootVoice(v) || v.channel != channel || v.exclusiveClass != excClass || excClass == 0) {
            ++i;
            continue;
        }
        if (v.HasLinkedVoice()) {
            voices_[v.linkedVoiceIndex].Kill();
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
        if (v.HasLinkedVoice()) {
            voices_[v.linkedVoiceIndex].NoteOff();
        }
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
        if (v.HasLinkedVoice()) {
            voices_[v.linkedVoiceIndex].Kill();
            v.ClearLinkedVoice();
        }
        v.Kill();
        UntrackVoice(voiceIndex);
    }
    for (auto& queue : noteQueue_[channel]) {
        queue.clear();
    }
}

bool VoicePool::IsChannelAudible(u32 audibleChannelMask, u8 channel) {
    return (audibleChannelMask & (1u << channel)) != 0;
}

int VoicePool::RenderSample(f32& outL, f32& outR, f32& reverbL, f32& reverbR, f32& chorusL, f32& chorusR,
                            u32 audibleChannelMask) {
    u16 write = 0;
    for (u16 read = 0; read < activeCount_; ++read) {
        const u16 voiceIndex = activeIndices_[read];
        auto& v = voices_[voiceIndex];
        if (!v.active) {
            activeSlots_[voiceIndex] = -1;
            continue;
        }
        if (IsChannelAudible(audibleChannelMask, v.channel)) {
            v.Render(outL, outR, reverbL, reverbR, chorusL, chorusR);
        }
        if (v.HasLinkedVoice()) {
            auto& linked = voices_[v.linkedVoiceIndex];
            if (linked.active && IsChannelAudible(audibleChannelMask, linked.channel)) {
                linked.Render(outL, outR, reverbL, reverbR, chorusL, chorusR);
            }
            if (!linked.active) {
                linked.Kill();
                v.ClearLinkedVoice();
            }
        }
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

int VoicePool::RenderBlock(f32* outL, f32* outR, f32* reverbL, f32* reverbR, f32* chorusL, f32* chorusR, u32 numFrames,
                           u32 audibleChannelMask) {
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
            workerAudibleChannelMask_ = audibleChannelMask;
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
            if (IsChannelAudible(audibleChannelMask, v.channel)) {
                v.RenderBlock(outL, outR, reverbL, reverbR, chorusL, chorusR, numFrames);
            }
            if (v.HasLinkedVoice()) {
                auto& linked = voices_[v.linkedVoiceIndex];
                if (linked.active && IsChannelAudible(audibleChannelMask, linked.channel)) {
                    linked.RenderBlock(outL, outR, reverbL, reverbR, chorusL, chorusR, numFrames);
                }
                if (!linked.active) {
                    linked.Kill();
                    v.ClearLinkedVoice();
                }
            }
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
            if (IsChannelAudible(audibleChannelMask, v.channel)) {
                v.RenderBlock(outL, outR, reverbL, reverbR, chorusL, chorusR, numFrames);
            }
            if (v.HasLinkedVoice()) {
                auto& linked = voices_[v.linkedVoiceIndex];
                if (linked.active && IsChannelAudible(audibleChannelMask, linked.channel)) {
                    linked.RenderBlock(outL, outR, reverbL, reverbR, chorusL, chorusR, numFrames);
                }
                if (!linked.active) {
                    linked.Kill();
                    v.ClearLinkedVoice();
                }
            }
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
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (v.channel != channel) continue;
        const f64 previousChannelPitch = v.pitchBendSemitones;
        const f64 previousPerNotePitch = v.perNotePitchSemitones;
        const i64 previousSampleStepFixed = v.sampleStepFixed;
        v.UpdatePitchBend(pitchBendSemitones);
        if (v.HasLinkedVoice()) {
            auto& linked = voices_[v.linkedVoiceIndex];
            linked.UpdatePitchBend(pitchBendSemitones);
            SynchronizeAggregatedLinkedVoice(v, linked);
        }
        AppendPitchDebugLog("channel_pitch_update", voiceIndex, v,
                            previousChannelPitch, previousPerNotePitch, previousSampleStepFixed,
                            pitchBendSemitones, 0x80000000u, 0, 0, 0);
    }
}

void VoicePool::UpdateChannelPitch(u8 channel, const ChannelState& state) {
    for (u16 i = 0; i < activeCount_; ++i) {
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (v.channel != channel) continue;
        const f64 targetSemitones = state.isDrum ? 0.0 : ResolveRealtimeChannelPitch(v, state);
        const f64 previousChannelPitch = v.pitchBendSemitones;
        const f64 previousPerNotePitch = v.perNotePitchSemitones;
        const i64 previousSampleStepFixed = v.sampleStepFixed;
        v.UpdatePitchBend(targetSemitones);
        if (v.HasLinkedVoice()) {
            auto& linked = voices_[v.linkedVoiceIndex];
            linked.UpdatePitchBend(targetSemitones);
            SynchronizeAggregatedLinkedVoice(v, linked);
        }
        AppendPitchDebugLog("channel_pitch_update", voiceIndex, v,
                            previousChannelPitch, previousPerNotePitch, previousSampleStepFixed,
                            targetSemitones, state.pitchBend32, state.pitchBendRangeSemitones,
                            state.pitchBendRangeCents, state.noteTuningCents[v.noteKey]);
    }
}

void VoicePool::UpdatePerNotePitchBend(u8 channel, u8 key, f64 perNoteSemitones) {
    for (u16 i = 0; i < activeCount_; ++i) {
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (v.channel == channel && v.noteKey == key) {
            const f64 previousChannelPitch = v.pitchBendSemitones;
            const f64 previousPerNotePitch = v.perNotePitchSemitones;
            const i64 previousSampleStepFixed = v.sampleStepFixed;
            v.UpdatePerNotePitchBend(perNoteSemitones);
            if (v.HasLinkedVoice()) {
                auto& linked = voices_[v.linkedVoiceIndex];
                linked.UpdatePerNotePitchBend(perNoteSemitones);
                SynchronizeAggregatedLinkedVoice(v, linked);
            }
            AppendPitchDebugLog("per_note_pitch_update", voiceIndex, v,
                                previousChannelPitch, previousPerNotePitch, previousSampleStepFixed,
                                perNoteSemitones, 0x80000000u, 0, 0, 0);
        }
    }
}

void VoicePool::ResetPerNoteState(u8 channel, u8 key) {
    for (u16 i = 0; i < activeCount_; ++i) {
        const u16 voiceIndex = activeIndices_[i];
        auto& v = voices_[voiceIndex];
        if (v.channel == channel && v.noteKey == key) {
            const f64 previousChannelPitch = v.pitchBendSemitones;
            const f64 previousPerNotePitch = v.perNotePitchSemitones;
            const i64 previousSampleStepFixed = v.sampleStepFixed;
            v.UpdatePerNotePitchBend(0.0);
            if (v.HasLinkedVoice()) {
                auto& linked = voices_[v.linkedVoiceIndex];
                linked.UpdatePerNotePitchBend(0.0);
                SynchronizeAggregatedLinkedVoice(v, linked);
            }
            AppendPitchDebugLog("per_note_pitch_reset", voiceIndex, v,
                                previousChannelPitch, previousPerNotePitch, previousSampleStepFixed,
                                0.0, 0x80000000u, 0, 0, 0);
        }
    }
}

void VoicePool::UpdateChannelMix(u8 channel, f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32) {
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel) continue;
        v.UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
        if (v.HasLinkedVoice()) {
            auto& linked = voices_[v.linkedVoiceIndex];
            linked.UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
            SynchronizeAggregatedLinkedVoice(v, linked);
        }
    }
}

void VoicePool::RefreshSf2Controllers(u8 channel, const SoundBank& soundBank, const ModulatorContext& ctx,
                                      f32 volumeFactor, u32 pan32, u32 reverbSend32, u32 chorusSend32) {
    std::vector<ResolvedZone> zones;
    for (u16 i = 0; i < activeCount_; ++i) {
        auto& v = voices_[activeIndices_[i]];
        if (v.channel != channel || !v.active) continue;
        zones.clear();
        if (!soundBank.FindZones(v.bank, v.program, v.noteKey, v.velocity, zones, &ctx)) {
            v.UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
            continue;
        }
        for (const auto& zone : zones) {
            if (!v.MatchesResolvedZone(zone)) {
                if (v.HasLinkedVoice()) {
                    auto& linked = voices_[v.linkedVoiceIndex];
                    if (linked.MatchesResolvedZone(zone)) {
                        linked.RefreshResolvedZoneControllers(zone);
                    }
                }
                continue;
            }
            v.RefreshResolvedZoneControllers(zone);
        }
        v.UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
        if (v.HasLinkedVoice()) {
            auto& linked = voices_[v.linkedVoiceIndex];
            linked.UpdateChannelMix(volumeFactor, pan32, reverbSend32, chorusSend32);
            SynchronizeAggregatedLinkedVoice(v, linked);
        }
    }
}

int VoicePool::ActiveCount() const {
    return activeCount_;
}

void VoicePool::GetActiveRootNoteCountsPerChannel(std::array<u32, MIDI_CHANNEL_COUNT>& counts) const {
    counts.fill(0);
    for (u16 i = 0; i < activeCount_; ++i) {
        const auto& v = voices_[activeIndices_[i]];
        if (!v.active || !IsTrackedRootVoice(v)) {
            continue;
        }
        if (v.channel < MIDI_CHANNEL_COUNT && v.envPhase != EnvPhase::Off) {
            ++counts[v.channel];
        }
    }
}

void VoicePool::GetActiveRootKeyMasksPerChannel(std::array<std::array<u32, 4>, MIDI_CHANNEL_COUNT>& masks) const {
    for (auto& channelMasks : masks) {
        channelMasks.fill(0);
    }
    for (u16 i = 0; i < activeCount_; ++i) {
        const auto& v = voices_[activeIndices_[i]];
        if (!v.active || !IsTrackedRootVoice(v) || v.envPhase == EnvPhase::Off) {
            continue;
        }
        if (v.channel >= MIDI_CHANNEL_COUNT || v.noteKey >= 128) {
            continue;
        }
        const u32 wordIndex = static_cast<u32>(v.noteKey >> 5);
        const u32 bitMask = 1u << (v.noteKey & 31);
        masks[v.channel][wordIndex] |= bitMask;
    }
}

bool VoicePool::HasActiveNote(u8 channel, u8 key, u32 noteId) const {
    for (u16 i = 0; i < activeCount_; ++i) {
        const auto& v = voices_[activeIndices_[i]];
        if (!IsTrackedRootVoice(v)) continue;
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
        const u32 audibleChannelMask = workerAudibleChannelMask_;

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
            if (IsChannelAudible(audibleChannelMask, v.channel)) {
                v.RenderBlock(
                    scratch.outL.data(), scratch.outR.data(),
                    scratch.reverbL.data(), scratch.reverbR.data(),
                    scratch.chorusL.data(), scratch.chorusR.data(),
                    numFrames);
            }
            if (v.HasLinkedVoice()) {
                auto& linked = voices_[v.linkedVoiceIndex];
                if (linked.active && IsChannelAudible(audibleChannelMask, linked.channel)) {
                    linked.RenderBlock(
                        scratch.outL.data(), scratch.outR.data(),
                        scratch.reverbL.data(), scratch.reverbR.data(),
                        scratch.chorusL.data(), scratch.chorusR.data(),
                        numFrames);
                }
            }
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

} // namespace XArkMidi

