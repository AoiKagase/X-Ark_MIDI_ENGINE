/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "Sf2File.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <tuple>
namespace XArkMidi {

// RIFF FourCC ヘルパー
static u32 MakeFourCC(const char* s) {
    return (static_cast<u32>(s[0])      )
         | (static_cast<u32>(s[1]) <<  8)
         | (static_cast<u32>(s[2]) << 16)
         | (static_cast<u32>(s[3]) << 24);
}

namespace {

enum class TopLevelListOrder : u8 {
    ExpectInfo = 0,
    ExpectSdta = 1,
    ExpectPdta = 2,
    Done = 3,
};

enum class PdtaSubchunkOrder : u8 {
    ExpectPhdr = 0,
    ExpectPbag = 1,
    ExpectPmod = 2,
    ExpectPgen = 3,
    ExpectInst = 4,
    ExpectIbag = 5,
    ExpectImod = 6,
    ExpectIgen = 7,
    ExpectShdr = 8,
    Done = 9,
};

constexpr u16 kModSrcVelocity = 2u;
constexpr u16 kModSrcChannelPressure = 13u;
constexpr u16 kModSrcPitchWheel = 0x020Eu;
constexpr u16 kModSrcPitchWheelSensitivity = 16u;
constexpr u16 kModSrcCc1 = 0x0081u;
constexpr u16 kModSrcCc7 = 0x0587u;
constexpr u16 kModSrcCc10 = 0x028Au;
constexpr u16 kModSrcCc11 = 0x058Bu;
constexpr u16 kModSrcCc91 = 0x00DBu;
constexpr u16 kModSrcCc93 = 0x00DDu;
constexpr u16 kModSrcLink = 127u;
constexpr u16 kModDestInitialPitch = 59u;
constexpr i16 kDefaultCc1ToVibLfoPitchCents = 50;
constexpr i16 kDefaultChannelPressureToVibLfoPitchCents = 50;
constexpr i16 kDefaultCc7ToInitialAttenuationCb = 960;
constexpr i16 kDefaultCc10ToPan = 1000;
constexpr i16 kDefaultCc11ToInitialAttenuationCb = 960;
constexpr i16 kDefaultCc91ToReverbSend = 200;
constexpr i16 kDefaultCc93ToChorusSend = 200;
// sfModTransOper と sfModSrcOper の curve type は別物。
// sfModSrcOper の concave / convex / switch は DecodeModSourceValue() が処理し、
// sfModTransOper は SF2 2.01 で定義されている Linear / Absolute のみ扱う。
constexpr u16 kModTransformLinear = 0u;
constexpr u16 kModTransformAbsolute = 2u;

bool IsValidInfoAsciiZstr(BinaryReader& r, u32 subSize, std::string* outValue = nullptr) {
    if (subSize == 0 || subSize > 256) {
        return false;
    }

    std::string value;
    value.reserve(subSize);
    bool foundTerminator = false;
    for (u32 i = 0; i < subSize; ++i) {
        const u8 ch = r.ReadU8();
        if (!foundTerminator) {
            if (ch == 0) {
                foundTerminator = true;
            } else {
                if (ch < 0x20u || ch > 0x7Eu) {
                    return false;
                }
                value.push_back(static_cast<char>(ch));
            }
        } else if (ch != 0) {
            return false;
        }
    }

    if (!foundTerminator) {
        return false;
    }
    if (outValue) {
        *outValue = std::move(value);
    }
    return true;
}

inline u8 ResolveForcedKey(u8 key, const ResolvedZone& zone) {
    const i32 forcedKey = zone.generators[GEN_Keynum];
    if (forcedKey >= 0 && forcedKey <= 127) {
        return static_cast<u8>(forcedKey);
    }
    return key;
}

inline u16 ResolveForcedVelocity(u16 velocity, const ResolvedZone& zone) {
    const i32 forcedVelocity = zone.generators[GEN_Velocity];
    if (forcedVelocity >= 0 && forcedVelocity <= 127) {
        return static_cast<u16>((forcedVelocity * 65535 + 63) / 127);
    }
    return velocity;
}

// SF2 default modulator 1: velocity → InitialAttenuation
// 400 * log10(65535/vel) centibels (16-bit velocity, square law互換)
inline i32 ComputeDefaultVelocityAttenuationCb(u16 velocity) {
    if (velocity == 0) return 960;
    if (velocity >= 65535) return 0;
    return static_cast<i32>(std::lround(400.0 * std::log10(65535.0 / velocity)));
}

inline i32 ComputeDefaultVelocityFilterCutoffDelta(u16 velocity) {
    return static_cast<i32>(std::lround(-2400.0 * (1.0 - static_cast<double>(velocity) / 65535.0)));
}

bool ValidateChunkElementCount(u32 count, u32 maxCount, const char* chunkName, std::string& outError) {
    if (count > maxCount) {
        outError = std::string("SF2 chunk too large: ") + chunkName;
        return false;
    }
    return true;
}

bool ValidateChunkSizeMultiple(u32 size, u32 recordSize, const char* chunkName, std::string& outError) {
    if (recordSize == 0 || (size % recordSize) != 0) {
        outError = std::string("Invalid SF2 chunk size: ") + chunkName;
        return false;
    }
    return true;
}

bool IsIllegalPresetSampleGenerator(u16 oper) {
    switch (oper) {
    case GEN_StartAddrsOffset:
    case GEN_EndAddrsOffset:
    case GEN_StartloopAddrsOffset:
    case GEN_EndloopAddrsOffset:
    case GEN_StartAddrsCoarseOffset:
    case GEN_EndAddrsCoarseOffset:
    case GEN_StartloopAddrsCoarse:
    case GEN_EndloopAddrsCoarse:
    case GEN_SampleID:
    case GEN_SampleModes:
    case GEN_ExclusiveClass:
    case GEN_OverridingRootKey:
        return true;
    default:
        return false;
    }
}

bool IsPlainVelocitySource(u16 oper) {
    return oper == kModSrcVelocity;
}

bool IsLinkModSource(u16 oper) {
    return (oper & 0x7Fu) == kModSrcLink && (oper & 0x80u) == 0;
}

bool IsVelocityToInitialAttenuationMod(const SFModList& mod) {
    if (mod.sfModDestOper != GEN_InitialAttenuation || mod.sfModTransOper != kModTransformLinear) {
        return false;
    }
    // 暗黙の default velocity modulator を抑止するのは、
    // 単純な velocity source を直接使うケースに限定する。
    if (!IsPlainVelocitySource(mod.sfModSrcOper)) {
        return false;
    }
    return mod.sfModAmtSrcOper == 0;
}

bool IsVelocityToInitialFilterFcMod(const SFModList& mod) {
    if (mod.sfModDestOper != GEN_InitialFilterFc || mod.sfModTransOper != kModTransformLinear) {
        return false;
    }
    if (!IsPlainVelocitySource(mod.sfModSrcOper)) {
        return false;
    }
    return mod.sfModAmtSrcOper == 0;
}

bool IsCc1ToVibLfoPitchMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcCc1 &&
           mod.sfModDestOper == GEN_VibLfoToPitch &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsChannelPressureToVibLfoPitchMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcChannelPressure &&
           mod.sfModDestOper == GEN_VibLfoToPitch &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsCc7ToInitialAttenuationMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcCc7 &&
           mod.sfModDestOper == GEN_InitialAttenuation &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsCc10ToPanMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcCc10 &&
           mod.sfModDestOper == GEN_Pan &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsCc11ToInitialAttenuationMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcCc11 &&
           mod.sfModDestOper == GEN_InitialAttenuation &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsCc91ToReverbSendMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcCc91 &&
           mod.sfModDestOper == GEN_ReverbEffectsSend &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsCc93ToChorusSendMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcCc93 &&
           mod.sfModDestOper == GEN_ChorusEffectsSend &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == kModTransformLinear;
}

bool IsPitchWheelToInitialPitchMod(const SFModList& mod) {
    return mod.sfModSrcOper == kModSrcPitchWheel &&
           mod.sfModDestOper == kModDestInitialPitch &&
           mod.sfModAmtSrcOper == kModSrcPitchWheelSensitivity &&
           mod.sfModTransOper == kModTransformLinear;
}

struct ZoneModEntry {
    SFModList mod{};
    bool ignored = false;
    std::vector<int> incomingLinks;
};

std::vector<ZoneModEntry> BuildEffectiveZoneModEntries(const std::vector<SFModList>& mods, int modStart, int modEnd) {
    const int modMax = static_cast<int>(mods.size());
    if (modStart < 0 || modStart > modMax) modStart = modMax;
    if (modEnd < modStart) modEnd = modStart;
    if (modEnd > modMax) modEnd = modMax;

    std::vector<ZoneModEntry> entries;
    entries.reserve(modEnd - modStart);
    std::map<std::tuple<u16, u16, u16>, int> duplicateMap;

    for (int i = modStart; i < modEnd; ++i) {
        const SFModList& mod = mods[i];
        const bool isTerminal =
            mod.sfModSrcOper == 0 &&
            mod.sfModDestOper == 0 &&
            mod.modAmount == 0 &&
            mod.sfModAmtSrcOper == 0 &&
            mod.sfModTransOper == 0;
        if (isTerminal) {
            continue;
        }

        ZoneModEntry entry;
        entry.mod = mod;
        entry.ignored = IsLinkModSource(mod.sfModAmtSrcOper);
        entries.push_back(entry);

        const auto key = std::make_tuple(mod.sfModSrcOper, mod.sfModDestOper, mod.sfModAmtSrcOper);
        auto it = duplicateMap.find(key);
        if (it != duplicateMap.end()) {
            entries[it->second].ignored = true;
        }
        duplicateMap[key] = static_cast<int>(entries.size()) - 1;
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (entries[i].ignored) {
            continue;
        }
        const u16 dest = entries[i].mod.sfModDestOper;
        if ((dest & 0x8000u) == 0) {
            continue;
        }
        const int targetIndex = static_cast<int>(dest & 0x7FFFu);
        if (targetIndex < 0 || targetIndex >= static_cast<int>(entries.size())) {
            entries[i].ignored = true;
            continue;
        }
        entries[targetIndex].incomingLinks.push_back(i);
    }

    return entries;
}

double ConcaveCurve(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return std::sqrt(x);
}

double ConvexCurve(double x) {
    x = std::clamp(x, 0.0, 1.0);
    return 1.0 - std::sqrt(1.0 - x);
}

double DecodeModSourceValue(u16 oper, u8 key, u16 velocity, const ModulatorContext* ctx, bool& supported) {
    supported = false;
    if (oper == 0) {
        supported = true;
        return 1.0;
    }

    const u16 index = oper & 0x7Fu;
    const bool isCC = (oper & 0x80u) != 0;
    const bool directionNegative = (oper & 0x100u) != 0;
    const bool bipolar = (oper & 0x200u) != 0;
    const u16 type = (oper >> 10) & 0x3Fu;
    double x = 0.0;
    if (isCC) {
        if (!ctx) return 0.0;
        x = static_cast<double>(ctx->ccValues[index]) / 127.0;
    } else {
        switch (index) {
        case 2: x = static_cast<double>(velocity) / 65535.0; break;
        case 3: x = static_cast<double>(key) / 127.0; break;
        case 10:
            if (!ctx) return 0.0;
            x = static_cast<double>(ctx->polyPressure[key]) / 127.0;
            break;
        case 13:
            if (!ctx) return 0.0;
            x = static_cast<double>(ctx->channelPressure) / 127.0;
            break;
        case 14:
            if (!ctx) return 0.0;
            x = std::clamp((static_cast<double>(ctx->pitchBend) + 8192.0) / 16383.0, 0.0, 1.0);
            break;
        case 16:
            if (!ctx) return 0.0;
            x = std::clamp((static_cast<double>(ctx->pitchWheelSensitivitySemitones) +
                            static_cast<double>(ctx->pitchWheelSensitivityCents) / 100.0) / 24.0,
                           0.0, 1.0);
            break;
        default:
            return 0.0;
        }
    }

    switch (type) {
    case 0:
        break;
    case 1: // concave
        x = ConcaveCurve(x);
        break;
    case 2: // convex
        x = ConvexCurve(x);
        break;
    case 3: // switch
        x = (x >= 0.5) ? 1.0 : 0.0;
        break;
    default:
        return 0.0;
    }

    supported = true;
    if (!bipolar) {
        return directionNegative ? (1.0 - x) : x;
    }
    return directionNegative ? (1.0 - 2.0 * x) : (2.0 * x - 1.0);
}

bool IsSupportedModSourceOperDefinition(u16 oper) {
    if (oper == 0) {
        return true;
    }

    const u16 index = oper & 0x7Fu;
    const bool isCC = (oper & 0x80u) != 0;
    const u16 type = (oper >> 10) & 0x3Fu;
    if (type > 3) {
        return false;
    }
    if (isCC) {
        return index <= 127;
    }
    switch (index) {
    case 2:
    case 3:
    case 10:
    case 13:
    case 14:
    case 16:
    case 127:
        return true;
    default:
        return false;
    }
}

bool IsSupportedModTransform(u16 oper) {
    return oper == kModTransformLinear || oper == kModTransformAbsolute;
}

double ApplyModSourceTransform(double value, u16 oper, bool& supported) {
    supported = true;
    switch (oper) {
    case kModTransformLinear:
        return value;
    case kModTransformAbsolute:
        return std::fabs(value);
    default:
        supported = false;
        return 0.0;
    }
}

i32 ClampGeneratorValue(u16 oper, i32 value) {
    switch (oper) {
    case GEN_StartAddrsOffset:
    case GEN_EndAddrsOffset:
    case GEN_StartloopAddrsOffset:
    case GEN_EndloopAddrsOffset:
    case GEN_StartAddrsCoarseOffset:
    case GEN_EndAddrsCoarseOffset:
    case GEN_StartloopAddrsCoarse:
    case GEN_EndloopAddrsCoarse:
        return std::clamp(value, -32768, 32767);
    case GEN_ModLfoToPitch:
    case GEN_VibLfoToPitch:
    case GEN_ModEnvToPitch:
    case GEN_ModLfoToFilterFc:
    case GEN_ModEnvToFilterFc:
        return std::clamp(value, -12000, 12000);
    case GEN_InitialFilterFc:
        return std::clamp(value, 1500, 13500);
    case GEN_InitialFilterQ:
        return std::clamp(value, 0, 960);
    case GEN_ModLfoToVolume:
        return std::clamp(value, -960, 960);
    case GEN_ChorusEffectsSend:
    case GEN_ReverbEffectsSend:
        return std::clamp(value, 0, 1000);
    case GEN_Pan:
        return std::clamp(value, -500, 500);
    case GEN_DelayModLFO:
    case GEN_DelayVibLFO:
    case GEN_DelayModEnv:
    case GEN_HoldModEnv:
    case GEN_DelayVolEnv:
    case GEN_HoldVolEnv:
        return std::clamp(value, -12000, 5000);
    case GEN_FreqModLFO:
    case GEN_FreqVibLFO:
        return std::clamp(value, -16000, 4500);
    case GEN_AttackModEnv:
    case GEN_DecayModEnv:
    case GEN_ReleaseModEnv:
    case GEN_AttackVolEnv:
    case GEN_DecayVolEnv:
    case GEN_ReleaseVolEnv:
        return std::clamp(value, -12000, 8000);
    case GEN_SustainModEnv:
        return std::clamp(value, 0, 1000);
    case GEN_KeynumToModEnvHold:
    case GEN_KeynumToModEnvDecay:
    case GEN_KeynumToVolEnvHold:
    case GEN_KeynumToVolEnvDecay:
        return std::clamp(value, -1200, 1200);
    case GEN_SustainVolEnv:
        return std::clamp(value, 0, 1440);
    case GEN_InitialAttenuation:
        return std::clamp(value, 0, 1440);
    case GEN_Keynum:
    case GEN_Velocity:
    case GEN_OverridingRootKey:
        return (value < 0) ? value : std::clamp(value, 0, 127);
    case GEN_ExclusiveClass:
        return std::clamp(value, 0, 127);
    case GEN_CoarseTune:
        return std::clamp(value, -120, 120);
    case GEN_FineTune:
        return std::clamp(value, -99, 99);
    case GEN_SampleModes:
        return std::clamp(value, 0, 3);
    case GEN_ScaleTuning:
        return std::clamp(value, 0, 1200);
    default:
        return value;
    }
}

void ApplyInitialPitchDelta(ResolvedZone& zone, i32 deltaCents) {
    i32 totalCents = zone.generators[GEN_CoarseTune] * 100 + zone.generators[GEN_FineTune] + deltaCents;
    totalCents = std::clamp(totalCents, -12099, 12099);
    const i32 coarse = totalCents / 100;
    const i32 fine = totalCents % 100;
    zone.generators[GEN_CoarseTune] = ClampGeneratorValue(GEN_CoarseTune, coarse);
    zone.generators[GEN_FineTune] = ClampGeneratorValue(GEN_FineTune, fine);
}

void ApplyModulatorDelta(ResolvedZone& zone, u16 dest, i32 delta) {
    switch (dest) {
    case GEN_ModLfoToPitch:
    case GEN_VibLfoToPitch:
    case GEN_ModEnvToPitch:
    case GEN_InitialFilterFc:
    case GEN_InitialFilterQ:
    case GEN_ModLfoToFilterFc:
    case GEN_ModEnvToFilterFc:
    case GEN_ModLfoToVolume:
    case GEN_ChorusEffectsSend:
    case GEN_ReverbEffectsSend:
    case GEN_Pan:
    case GEN_DelayModLFO:
    case GEN_FreqModLFO:
    case GEN_DelayVibLFO:
    case GEN_FreqVibLFO:
    case GEN_DelayModEnv:
    case GEN_AttackModEnv:
    case GEN_HoldModEnv:
    case GEN_DecayModEnv:
    case GEN_SustainModEnv:
    case GEN_ReleaseModEnv:
    case GEN_KeynumToModEnvHold:
    case GEN_KeynumToModEnvDecay:
    case GEN_DelayVolEnv:
    case GEN_AttackVolEnv:
    case GEN_HoldVolEnv:
    case GEN_DecayVolEnv:
    case GEN_SustainVolEnv:
    case GEN_ReleaseVolEnv:
    case GEN_KeynumToVolEnvHold:
    case GEN_KeynumToVolEnvDecay:
    case GEN_Keynum:
    case GEN_Velocity:
    case GEN_InitialAttenuation:
    case GEN_CoarseTune:
    case GEN_FineTune:
    case GEN_ScaleTuning:
    case GEN_ExclusiveClass:
    case GEN_OverridingRootKey:
        zone.generators[dest] = ClampGeneratorValue(dest, zone.generators[dest] + delta);
        break;
    case kModDestInitialPitch:
        ApplyInitialPitchDelta(zone, delta);
        break;
    default:
        break;
    }
}

bool IsSupportedModulatorDestination(u16 dest) {
    switch (dest) {
    case GEN_ModLfoToPitch:
    case GEN_VibLfoToPitch:
    case GEN_ModEnvToPitch:
    case GEN_InitialFilterFc:
    case GEN_InitialFilterQ:
    case GEN_ModLfoToFilterFc:
    case GEN_ModEnvToFilterFc:
    case GEN_ModLfoToVolume:
    case GEN_ChorusEffectsSend:
    case GEN_ReverbEffectsSend:
    case GEN_Pan:
    case GEN_DelayModLFO:
    case GEN_FreqModLFO:
    case GEN_DelayVibLFO:
    case GEN_FreqVibLFO:
    case GEN_DelayModEnv:
    case GEN_AttackModEnv:
    case GEN_HoldModEnv:
    case GEN_DecayModEnv:
    case GEN_SustainModEnv:
    case GEN_ReleaseModEnv:
    case GEN_KeynumToModEnvHold:
    case GEN_KeynumToModEnvDecay:
    case GEN_DelayVolEnv:
    case GEN_AttackVolEnv:
    case GEN_HoldVolEnv:
    case GEN_DecayVolEnv:
    case GEN_SustainVolEnv:
    case GEN_ReleaseVolEnv:
    case GEN_KeynumToVolEnvHold:
    case GEN_KeynumToVolEnvDecay:
    case GEN_Keynum:
    case GEN_Velocity:
    case GEN_InitialAttenuation:
    case GEN_CoarseTune:
    case GEN_FineTune:
    case GEN_ScaleTuning:
    case GEN_ExclusiveClass:
    case GEN_OverridingRootKey:
    case kModDestInitialPitch:
        return true;
    default:
        return false;
    }
}

} // namespace

void Sf2File::SetResourceLimits(size_t maxSampleDataBytes, u32 maxPdtaEntries) {
    if (maxSampleDataBytes != 0)
        maxSampleDataBytes_ = maxSampleDataBytes;
    if (maxPdtaEntries != 0)
        maxPdtaEntries_ = maxPdtaEntries;
}

bool Sf2File::LoadFromMemory(const u8* data, size_t size) {
    sampleData_.clear();
    sampleData24_.clear();
    presets_.clear(); presetBags_.clear(); presetGens_.clear(); presetMods_.clear();
    instruments_.clear(); instBags_.clear(); instGens_.clear(); instMods_.clear();
    samples_.clear();
    sampleHeaders_.clear();
    presetIndexMap_.clear();
    romBank_.reset();
    errorMsg_.clear();
    unsupportedModulatorCount_ = 0;
    unsupportedModulatorTransformCount_ = 0;
    hasIgnoredSm24_ = false;
    hasSmpl_ = false;
    hasSm24Chunk_ = false;
    sm24ChunkSize_ = 0;
    sm24Data_.clear();
    hasIfil_ = false;
    ifilMajor_ = 0;
    ifilMinor_ = 0;
    hasIsng_ = false;
    hasInam_ = false;
    hasIrom_ = false;
    hasIver_ = false;
    hasValidIsng_ = false;
    hasValidInam_ = false;
    hasValidIrom_ = false;
    hasValidIver_ = false;
    hasInfoList_ = false;
    hasSdtaList_ = false;
    hasPdtaList_ = false;
    hasPhdr_ = false;
    hasPbag_ = false;
    hasPmod_ = false;
    hasPgen_ = false;
    hasInst_ = false;
    hasIbag_ = false;
    hasImod_ = false;
    hasIgen_ = false;
    hasShdr_ = false;

    try {
        BinaryReader r(data, size);
        if (!ParseRiff(r)) return false;
        if (!ValidateInfoAndSdtaConsistency()) return false;
        if (!ValidatePdtaStructures()) return false;
        if (!ValidateSampleHeaders()) return false;
        BuildPresetIndex();
        ComputeSampleLoudnessGains();
        ScanUnsupportedModulators();
    }
    catch (const std::exception& e) {
        errorMsg_ = e.what();
        return false;
    }
    return true;
}

bool Sf2File::LoadFromFile(const std::wstring& path) {
    std::vector<u8> data;
    if (!ReadFileBytes(path, data, errorMsg_))
        return false;
    return LoadFromMemory(data.data(), data.size());
}

bool Sf2File::LoadRomSampleSourceFromMemory(const u8* data, size_t size) {
    auto romBank = std::make_shared<Sf2File>();
    romBank->SetResourceLimits(maxSampleDataBytes_, maxPdtaEntries_);
    if (!romBank->LoadFromMemory(data, size)) {
        errorMsg_ = "SF2 ROM source parse error: " + romBank->ErrorMessage();
        return false;
    }
    romBank_ = std::move(romBank);
    return true;
}

bool Sf2File::LoadRomSampleSourceFromFile(const std::wstring& path) {
    std::vector<u8> data;
    if (!ReadFileBytes(path, data, errorMsg_))
        return false;
    return LoadRomSampleSourceFromMemory(data.data(), data.size());
}

bool Sf2File::ParseRiff(BinaryReader& r) {
    // "RIFF" チャンク
    u32 riffId   = r.ReadU32LE();
    u32 riffSize = r.ReadU32LE();
    u32 riffType = r.ReadU32LE();

    if (riffId != MakeFourCC("RIFF")) {
        errorMsg_ = "Not an SF2 file: missing RIFF header";
        return false;
    }
    if (riffType != MakeFourCC("sfbk")) {
        errorMsg_ = "Not an SF2 file: RIFF type is not 'sfbk'";
        return false;
    }

    // sfbk 内のサブチャンクを走査
    size_t end = r.Tell() + (riffSize - 4); // -4 for riffType
    TopLevelListOrder expectedOrder = TopLevelListOrder::ExpectInfo;
    while (r.Tell() < end && !r.IsEof()) {
        u32 chunkId   = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();

        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            if (chunkSize < 4) {
                errorMsg_ = "Invalid SF2 LIST chunk size";
                return false;
            }
            auto listData = r.ReadSlice(chunkSize - 4);

            if (listType == MakeFourCC("INFO")) {
                if (expectedOrder != TopLevelListOrder::ExpectInfo) {
                    errorMsg_ = "SF2 top-level LIST chunks out of order";
                    return false;
                }
                expectedOrder = TopLevelListOrder::ExpectSdta;
                if (hasInfoList_) {
                    errorMsg_ = "SF2 duplicate INFO LIST chunk";
                    return false;
                }
                hasInfoList_ = true;
                if (!ParseInfo(listData, chunkSize - 4)) return false;
            }
            else if (listType == MakeFourCC("sdta")) {
                if (expectedOrder != TopLevelListOrder::ExpectSdta) {
                    errorMsg_ = "SF2 top-level LIST chunks out of order";
                    return false;
                }
                expectedOrder = TopLevelListOrder::ExpectPdta;
                if (hasSdtaList_) {
                    errorMsg_ = "SF2 duplicate sdta LIST chunk";
                    return false;
                }
                hasSdtaList_ = true;
                if (!ParseSdta(listData, chunkSize - 4)) return false;
            }
            else if (listType == MakeFourCC("pdta")) {
                if (expectedOrder != TopLevelListOrder::ExpectPdta) {
                    errorMsg_ = "SF2 top-level LIST chunks out of order";
                    return false;
                }
                expectedOrder = TopLevelListOrder::Done;
                if (hasPdtaList_) {
                    errorMsg_ = "SF2 duplicate pdta LIST chunk";
                    return false;
                }
                hasPdtaList_ = true;
                if (!ParsePdta(listData, chunkSize - 4)) return false;
            } else {
                errorMsg_ = "SF2 contains unknown top-level LIST chunk";
                return false;
            }
        }
        else {
            errorMsg_ = "SF2 contains unknown top-level chunk";
            return false;
        }
    }
    if (!hasIfil_) {
        errorMsg_ = "SF2 missing mandatory ifil chunk";
        return false;
    }
    if (!hasInfoList_ || !hasSdtaList_ || !hasPdtaList_) {
        errorMsg_ = "SF2 missing mandatory top-level LIST chunk";
        return false;
    }
    return true;
}

bool Sf2File::ParseInfo(BinaryReader& r, u32 /*chunkSize*/) {
    while (!r.IsEof()) {
        if (r.Remaining() < 8) break;
        const u32 subId = r.ReadU32LE();
        const u32 subSize = r.ReadU32LE();
        if (static_cast<size_t>(subSize) > r.Remaining()) {
            errorMsg_ = "SF2 INFO subchunk size exceeds containing chunk";
            return false;
        }
        auto sub = r.ReadSlice(subSize);
        if (subSize & 1 && !r.IsEof()) r.Skip(1);

        if (subId == MakeFourCC("ifil")) {
            if (hasIfil_) {
                errorMsg_ = "SF2 duplicate ifil chunk";
                return false;
            }
            if (subSize != 4) {
                errorMsg_ = "SF2 invalid ifil chunk size";
                return false;
            }
            ifilMajor_ = sub.ReadU16LE();
            ifilMinor_ = sub.ReadU16LE();
            hasIfil_ = true;
        } else if (subId == MakeFourCC("isng")) {
            if (hasIsng_) {
                errorMsg_ = "SF2 duplicate isng chunk";
                return false;
            }
            hasIsng_ = true;
            std::string engineName;
            if (IsValidInfoAsciiZstr(sub, subSize, &engineName) && engineName == "EMU8000") {
                hasValidIsng_ = true;
            }
        } else if (subId == MakeFourCC("INAM")) {
            if (hasInam_) {
                errorMsg_ = "SF2 duplicate INAM chunk";
                return false;
            }
            hasInam_ = true;
            hasValidInam_ = IsValidInfoAsciiZstr(sub, subSize);
        } else if (subId == MakeFourCC("irom")) {
            if (hasIrom_) {
                errorMsg_ = "SF2 duplicate irom chunk";
                return false;
            }
            hasIrom_ = true;
            hasValidIrom_ = IsValidInfoAsciiZstr(sub, subSize);
        } else if (subId == MakeFourCC("iver")) {
            if (hasIver_) {
                errorMsg_ = "SF2 duplicate iver chunk";
                return false;
            }
            if (subSize == 4) {
                sub.ReadU16LE();
                sub.ReadU16LE();
                hasValidIver_ = true;
            }
            hasIver_ = true;
        }
    }
    return true;
}

bool Sf2File::ParseSdta(BinaryReader& r, u32 /*chunkSize*/) {
    while (!r.IsEof()) {
        if (r.Remaining() < 8) break;
        u32 subId   = r.ReadU32LE();
        u32 subSize = r.ReadU32LE();
        if (static_cast<size_t>(subSize) > r.Remaining()) {
            errorMsg_ = "SF2 sdta subchunk size exceeds containing chunk";
            return false;
        }

        if (subId == MakeFourCC("smpl")) {
            if (hasSmpl_) {
                errorMsg_ = "SF2 duplicate smpl chunk";
                return false;
            }
            if ((subSize & 1u) != 0u) {
                errorMsg_ = "SF2 smpl chunk size must be even";
                return false;
            }
            hasSmpl_ = true;
            size_t count = subSize / 2;
            const size_t maxSampleCount = maxSampleDataBytes_ / sizeof(i16);
            if (count > maxSampleCount) {
                errorMsg_ = "SF2 sample data exceeds configured limit";
                return false;
            }
            sampleData_.resize(count);
            if (count > 0) {
                // バルクコピー（1サンプルずつ読むと大きなSF2でハングする）
                // x86/x64 はリトルエンディアンなので i16 は直接コピー可能
                std::memcpy(sampleData_.data(), r.CurrentPtr(), count * sizeof(i16));
                r.Skip(subSize);
            }
        }
        else if (subId == MakeFourCC("sm24")) {
            if (hasSm24Chunk_) {
                errorMsg_ = "SF2 duplicate sm24 chunk";
                return false;
            }
            hasSm24Chunk_ = true;
            sm24ChunkSize_ = subSize;
            sm24Data_.resize(subSize);
            if (subSize > 0) {
                std::memcpy(sm24Data_.data(), r.CurrentPtr(), subSize);
            }
            r.Skip(subSize);
            if ((subSize & 1) && !r.IsEof()) r.Skip(1);
        }
        else {
            errorMsg_ = "SF2 contains unknown sdta subchunk";
            return false;
        }
    }
    return true;
}

bool Sf2File::ParsePdta(BinaryReader& r, u32 /*chunkSize*/) {
    PdtaSubchunkOrder expectedOrder = PdtaSubchunkOrder::ExpectPhdr;
    while (!r.IsEof()) {
        if (r.Remaining() < 8) break;
        u32 subId   = r.ReadU32LE();
        u32 subSize = r.ReadU32LE();
        if (static_cast<size_t>(subSize) > r.Remaining()) {
            errorMsg_ = "SF2 pdta subchunk size exceeds containing chunk";
            return false;
        }
        auto sub = r.ReadSlice(subSize);
        if (subSize & 1 && !r.IsEof()) r.Skip(1);

        if (subId == MakeFourCC("phdr")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectPhdr) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectPbag;
            ParsePhdr(sub, subSize);
        }
        else if (subId == MakeFourCC("pbag")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectPbag) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectPmod;
            ParsePbag(sub, subSize);
        }
        else if (subId == MakeFourCC("pmod")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectPmod) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectPgen;
            ParsePmod(sub, subSize);
        }
        else if (subId == MakeFourCC("pgen")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectPgen) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectInst;
            ParsePgen(sub, subSize);
        }
        else if (subId == MakeFourCC("inst")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectInst) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectIbag;
            ParseInst(sub, subSize);
        }
        else if (subId == MakeFourCC("ibag")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectIbag) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectImod;
            ParseIbag(sub, subSize);
        }
        else if (subId == MakeFourCC("imod")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectImod) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectIgen;
            ParseImod(sub, subSize);
        }
        else if (subId == MakeFourCC("igen")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectIgen) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::ExpectShdr;
            ParseIgen(sub, subSize);
        }
        else if (subId == MakeFourCC("shdr")) {
            if (expectedOrder != PdtaSubchunkOrder::ExpectShdr) {
                errorMsg_ = "SF2 pdta subchunks out of order";
                return false;
            }
            expectedOrder = PdtaSubchunkOrder::Done;
            ParseShdr(sub, subSize);
        }
        else {
            errorMsg_ = "SF2 contains unknown pdta subchunk";
            return false;
        }
    }
    return true;
}

void Sf2File::ParsePhdr(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 38, "phdr", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 38; // sizeof(SFPresetHeader) = 38
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "phdr", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasPhdr_ = true;
    presets_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        auto& p = presets_[i];
        for (int c = 0; c < 20; ++c) p.achPresetName[c] = static_cast<char>(r.ReadU8());
        p.wPreset       = r.ReadU16LE();
        p.wBank         = r.ReadU16LE();
        p.wPresetBagNdx = r.ReadU16LE();
        p.dwLibrary     = r.ReadU32LE();
        p.dwGenre       = r.ReadU32LE();
        p.dwMorphology  = r.ReadU32LE();
    }
}

void Sf2File::ParsePbag(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 4, "pbag", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 4;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "pbag", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasPbag_ = true;
    presetBags_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        presetBags_[i].wGenNdx = r.ReadU16LE();
        presetBags_[i].wModNdx = r.ReadU16LE();
    }
}

void Sf2File::ParsePmod(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 10, "pmod", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 10;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "pmod", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasPmod_ = true;
    presetMods_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        presetMods_[i].sfModSrcOper  = r.ReadU16LE();
        presetMods_[i].sfModDestOper = r.ReadU16LE();
        presetMods_[i].modAmount     = static_cast<i16>(r.ReadU16LE());
        presetMods_[i].sfModAmtSrcOper = r.ReadU16LE();
        presetMods_[i].sfModTransOper  = r.ReadU16LE();
    }
}

void Sf2File::ParsePgen(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 4, "pgen", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 4;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "pgen", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasPgen_ = true;
    presetGens_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        presetGens_[i].sfGenOper          = r.ReadU16LE();
        presetGens_[i].genAmount.wAmount  = r.ReadU16LE();
    }
}

void Sf2File::ParseInst(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 22, "inst", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 22;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "inst", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasInst_ = true;
    instruments_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        for (int c = 0; c < 20; ++c) instruments_[i].achInstName[c] = static_cast<char>(r.ReadU8());
        instruments_[i].wInstBagNdx = r.ReadU16LE();
    }
}

void Sf2File::ParseIbag(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 4, "ibag", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 4;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "ibag", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasIbag_ = true;
    instBags_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        instBags_[i].wInstGenNdx = r.ReadU16LE();
        instBags_[i].wInstModNdx = r.ReadU16LE();
    }
}

void Sf2File::ParseImod(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 10, "imod", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 10;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "imod", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasImod_ = true;
    instMods_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        instMods_[i].sfModSrcOper    = r.ReadU16LE();
        instMods_[i].sfModDestOper   = r.ReadU16LE();
        instMods_[i].modAmount       = static_cast<i16>(r.ReadU16LE());
        instMods_[i].sfModAmtSrcOper = r.ReadU16LE();
        instMods_[i].sfModTransOper  = r.ReadU16LE();
    }
}

void Sf2File::ParseIgen(BinaryReader& r, u32 size) {
    if (!ValidateChunkSizeMultiple(size, 4, "igen", errorMsg_))
        throw std::runtime_error(errorMsg_);
    u32 count = size / 4;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "igen", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasIgen_ = true;
    instGens_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        instGens_[i].sfGenOper         = r.ReadU16LE();
        instGens_[i].genAmount.wAmount = r.ReadU16LE();
    }
}

void Sf2File::ParseShdr(BinaryReader& r, u32 size) {
    static_assert(sizeof(SFSample) == 46, "SFSample must be 46 bytes");
    if (!ValidateChunkSizeMultiple(size, 46, "shdr", errorMsg_))
        throw std::runtime_error(errorMsg_);
    const u32 count = size / 46;
    if (!ValidateChunkElementCount(count, maxPdtaEntries_, "shdr", errorMsg_))
        throw std::runtime_error(errorMsg_);
    hasShdr_ = true;
    samples_.resize(count);
    sampleHeaders_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        // SFSample は #pragma pack(1) で 46 バイト ─ 一括 memcpy で個別読込を排除
        SFSample& s = samples_[i];
        std::memcpy(&s, r.CurrentPtr(), sizeof(SFSample));
        r.Skip(sizeof(SFSample));

        SampleHeader& h = sampleHeaders_[i];
        std::memcpy(h.sampleName, s.achSampleName, 20);
        h.start           = s.dwStart;
        h.end             = s.dwEnd;
        h.loopStart       = s.dwStartloop;
        h.loopEnd         = s.dwEndloop;
        h.sampleRate      = s.dwSampleRate;
        h.originalPitch   = s.byOriginalPitch;
        h.pitchCorrection = s.chPitchCorrection;
        h.sampleLink      = s.wSampleLink;
        h.sampleType      = s.sfSampleType;
    }
}

bool Sf2File::ValidatePdtaStructures() {
    if (!hasPhdr_ || !hasPbag_ || !hasPmod_ || !hasPgen_ ||
        !hasInst_ || !hasIbag_ || !hasImod_ || !hasIgen_ || !hasShdr_) {
        errorMsg_ = "SF2 missing mandatory pdta chunk";
        return false;
    }

    if (presets_.empty() || presetBags_.empty() || presetMods_.empty() || presetGens_.empty() ||
        instruments_.empty() || instBags_.empty() || instMods_.empty() || instGens_.empty() || sampleHeaders_.empty()) {
        errorMsg_ = "SF2 pdta chunk missing terminal record";
        return false;
    }

    for (size_t i = 1; i < presets_.size(); ++i) {
        if (presets_[i - 1].wPresetBagNdx > presets_[i].wPresetBagNdx) {
            errorMsg_ = "SF2 non-monotonic phdr preset bag index";
            return false;
        }
    }
    for (size_t i = 1; i < presetBags_.size(); ++i) {
        if (presetBags_[i - 1].wGenNdx > presetBags_[i].wGenNdx ||
            presetBags_[i - 1].wModNdx > presetBags_[i].wModNdx) {
            errorMsg_ = "SF2 non-monotonic pbag index";
            return false;
        }
    }
    for (size_t i = 1; i < instruments_.size(); ++i) {
        if (instruments_[i - 1].wInstBagNdx > instruments_[i].wInstBagNdx) {
            errorMsg_ = "SF2 non-monotonic inst bag index";
            return false;
        }
    }
    for (size_t i = 1; i < instBags_.size(); ++i) {
        if (instBags_[i - 1].wInstGenNdx > instBags_[i].wInstGenNdx ||
            instBags_[i - 1].wInstModNdx > instBags_[i].wInstModNdx) {
            errorMsg_ = "SF2 non-monotonic ibag index";
            return false;
        }
    }

    if (static_cast<size_t>(presets_.back().wPresetBagNdx) + 1 != presetBags_.size()) {
        errorMsg_ = "SF2 terminal phdr does not match pbag size";
        return false;
    }
    if (static_cast<size_t>(presetBags_.back().wGenNdx) + 1 != presetGens_.size()) {
        errorMsg_ = "SF2 terminal pbag does not match pgen size: term=" +
                    std::to_string(presetBags_.back().wGenNdx) +
                    " pgen=" + std::to_string(presetGens_.size());
        return false;
    }
    if (static_cast<size_t>(presetBags_.back().wModNdx) + 1 != presetMods_.size()) {
        errorMsg_ = "SF2 terminal pbag does not match pmod size";
        return false;
    }
    if (static_cast<size_t>(instruments_.back().wInstBagNdx) + 1 != instBags_.size()) {
        errorMsg_ = "SF2 terminal inst does not match ibag size";
        return false;
    }
    if (static_cast<size_t>(instBags_.back().wInstGenNdx) + 1 != instGens_.size()) {
        errorMsg_ = "SF2 terminal ibag does not match igen size";
        return false;
    }
    if (static_cast<size_t>(instBags_.back().wInstModNdx) + 1 != instMods_.size()) {
        errorMsg_ = "SF2 terminal ibag does not match imod size";
        return false;
    }

    const size_t terminalInstrumentIndex = instruments_.size() - 1;
    const size_t terminalSampleIndex = sampleHeaders_.size() - 1;
    for (size_t bagIdx = 0; bagIdx + 1 < presetBags_.size(); ++bagIdx) {
        bool isGlobal = false;
        u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int instrumentIdx = -1;
        if (!AnalyzePresetBag(static_cast<int>(bagIdx), isGlobal, keyLo, keyHi, velLo, velHi, instrumentIdx) || isGlobal) {
            continue;
        }
        if (instrumentIdx < 0 || static_cast<size_t>(instrumentIdx) >= terminalInstrumentIndex) {
            errorMsg_ = "SF2 preset bag references invalid instrument terminal record";
            return false;
        }
    }
    for (size_t bagIdx = 0; bagIdx + 1 < instBags_.size(); ++bagIdx) {
        bool isGlobal = false;
        u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int sampleIdx = -1;
        if (!AnalyzeInstrumentBag(static_cast<int>(bagIdx), isGlobal, keyLo, keyHi, velLo, velHi, sampleIdx) || isGlobal) {
            continue;
        }
        if (sampleIdx < 0 || static_cast<size_t>(sampleIdx) >= terminalSampleIndex) {
            errorMsg_ = "SF2 instrument bag references invalid sample terminal record";
            return false;
        }
    }

    return true;
}

bool Sf2File::ValidateInfoAndSdtaConsistency() {
    if (!hasSmpl_) {
        errorMsg_ = "SF2 missing mandatory smpl chunk";
        return false;
    }
    if (hasSm24Chunk_) {
        const bool is204OrLater = (ifilMajor_ > 2) || (ifilMajor_ == 2 && ifilMinor_ >= 4);
        const u32 expectedSm24Size = static_cast<u32>(sampleData_.size());
        if (!is204OrLater || sampleData_.empty() || sm24ChunkSize_ != expectedSm24Size) {
            hasIgnoredSm24_ = true;
            hasSm24Chunk_ = false;
            sm24Data_.clear();
        } else {
            hasIgnoredSm24_ = false;
            sampleData24_.resize(sampleData_.size());
            for (size_t i = 0; i < sampleData_.size(); ++i) {
                const i32 hi16 = static_cast<i32>(sampleData_[i]);
                const i32 lo8 = static_cast<i32>(sm24Data_[i]);
                sampleData24_[i] = (hi16 << 8) | lo8;
            }
        }
    }

    return true;
}

bool Sf2File::ValidateSampleHeaders() {
    const u32 sampleDataCount = static_cast<u32>(sampleData_.size());
    for (size_t i = 0; i < sampleHeaders_.size(); ++i) {
        auto& h = sampleHeaders_[i];
        const bool isTerminalSample = (i + 1 == sampleHeaders_.size());
        const bool isRomSample = (h.sampleType & 0x8000u) != 0;
        if (h.start > h.end || (!isRomSample && h.end > sampleDataCount)) {
            errorMsg_ = "SF2 sample header points outside sample data";
            return false;
        }
        if (isTerminalSample) {
            if (h.sampleRate == 0) {
                h.sampleRate = 44100;
            }
            if (h.originalPitch == 255 || (h.originalPitch >= 128 && h.originalPitch <= 254)) {
                h.originalPitch = 60;
            }
            continue;
        }
        if (h.loopStart < h.start || h.loopStart > h.end) {
            h.loopStart = h.start;
        }
        if (h.loopEnd < h.loopStart || h.loopEnd > h.end) {
            h.loopEnd = h.end;
        }
        if (!isRomSample) {
        }
        // The minimum sample/loop lengths in SF2 2.01 are portability guidance
        // for simpler hardware. Real banks frequently violate them, and the
        // engine can still render those samples, so keep loading instead of
        // treating them as structurally invalid.
        // SF2 2.01 describes the 8-point loop guard region as a portability
        // constraint for artifact-free playback on simpler hardware, not as a
        // structural validity requirement. Our looped interpolation wraps
        // around the loop boundaries directly, so rejecting these banks would
        // be unnecessarily strict and breaks many real-world SF2 files.
        if (h.sampleRate == 0) {
            // Some banks leave the terminal EOS record or even sparse/unused sample
            // headers at 0 Hz. Keep parsing and fall back to a sane rate so we don't
            // reject otherwise playable banks.
            h.sampleRate = 44100;
        }
        if (h.originalPitch == 255 || (h.originalPitch >= 128 && h.originalPitch <= 254)) {
            h.originalPitch = 60;
        }
    }
    return true;
}

bool Sf2File::AnalyzePresetBag(int bagIdx, bool& outIsGlobal, u8& outKeyLo, u8& outKeyHi,
                               u8& outVelLo, u8& outVelHi, int& outInstrumentIdx) const {
    outIsGlobal = false;
    outKeyLo = 0;
    outKeyHi = 127;
    outVelLo = 0;
    outVelHi = 127;
    outInstrumentIdx = -1;

    if (bagIdx < 0 || bagIdx + 1 >= static_cast<int>(presetBags_.size())) {
        return false;
    }

    int genStart = presetBags_[bagIdx].wGenNdx;
    int genEnd = presetBags_[bagIdx + 1].wGenNdx;
    const int genMax = static_cast<int>(presetGens_.size());
    if (genStart < 0 || genStart > genMax) genStart = genMax;
    if (genEnd < genStart) genEnd = genStart;
    if (genEnd > genMax) genEnd = genMax;

    bool hasInstrument = false;
    for (int g = genStart; g < genEnd; ++g) {
        const u16 oper = presetGens_[g].sfGenOper;
        if (oper == GEN_Instrument) {
            outInstrumentIdx = presetGens_[g].genAmount.wAmount;
            hasInstrument = true;
            break;
        }
        if (oper == GEN_KeyRange) {
            outKeyLo = presetGens_[g].genAmount.ranges.lo;
            outKeyHi = presetGens_[g].genAmount.ranges.hi;
        } else if (oper == GEN_VelRange) {
            outVelLo = presetGens_[g].genAmount.ranges.lo;
            outVelHi = presetGens_[g].genAmount.ranges.hi;
        }
    }

    outIsGlobal = !hasInstrument;
    return true;
}

bool Sf2File::AnalyzeInstrumentBag(int bagIdx, bool& outIsGlobal, u8& outKeyLo, u8& outKeyHi,
                                   u8& outVelLo, u8& outVelHi, int& outSampleIdx) const {
    outIsGlobal = false;
    outKeyLo = 0;
    outKeyHi = 127;
    outVelLo = 0;
    outVelHi = 127;
    outSampleIdx = -1;

    if (bagIdx < 0 || bagIdx + 1 >= static_cast<int>(instBags_.size())) {
        return false;
    }

    int genStart = instBags_[bagIdx].wInstGenNdx;
    int genEnd = instBags_[bagIdx + 1].wInstGenNdx;
    const int genMax = static_cast<int>(instGens_.size());
    if (genStart < 0 || genStart > genMax) genStart = genMax;
    if (genEnd < genStart) genEnd = genStart;
    if (genEnd > genMax) genEnd = genMax;

    bool hasSampleId = false;
    for (int g = genStart; g < genEnd; ++g) {
        const u16 oper = instGens_[g].sfGenOper;
        if (oper == GEN_SampleID) {
            outSampleIdx = instGens_[g].genAmount.wAmount;
            hasSampleId = true;
            break;
        }
        if (oper == GEN_KeyRange) {
            outKeyLo = instGens_[g].genAmount.ranges.lo;
            outKeyHi = instGens_[g].genAmount.ranges.hi;
        } else if (oper == GEN_VelRange) {
            outVelLo = instGens_[g].genAmount.ranges.lo;
            outVelHi = instGens_[g].genAmount.ranges.hi;
        }
    }

    outIsGlobal = !hasSampleId;
    return true;
}

void Sf2File::ComputeSampleLoudnessGains() {
    // SF2仕様にはサンプル間ラウドネス正規化は存在しないため何もしない。
    // normCompensation_ = 1.0f, 各 loudnessGain = 1.0f のまま（デフォルト値）。
    normCompensation_ = 1.0f;
}

// ジェネレーターマージ（インストグローバル→インストゾーン→プリセットゾーン）
void Sf2File::ResolveZone(int globalPresetBagIdx, int globalInstBagIdx, int instBagIdx, int presetBagIdx,
                           const SampleHeader* sample, u8 key, u16 velocity,
                           const ModulatorContext* ctx,
                           ResolvedZone& outZone) const {
    outZone.sample = sample;
    outZone.sampleDataOverride = nullptr;
    outZone.sampleData24Override = nullptr;
    outZone.sampleDataOverrideCount = 0;
    outZone.presetBagIndex = presetBagIdx;
    outZone.instrumentBagIndex = instBagIdx;
    outZone.sampleId = -1;
    const i32* defaults = GetSF2GeneratorDefaults();
    constexpr i32 kUnset = std::numeric_limits<i32>::min();

    // Step 1: デフォルト値で初期化（デフォルト値は仕様範囲内なのでクランプ不要 → memcpy）
    std::memcpy(outZone.generators, defaults, GEN_COUNT * sizeof(i32));

    // std::fill はコンパイラが SIMD 化するため for ループより高速
    auto clearLayer = [](i32* layer) {
        std::fill(layer, layer + GEN_COUNT, std::numeric_limits<i32>::min());
    };

    auto applyPresetLayer = [&](i32* layer, int bagIdx) {
        if (bagIdx < 0 || bagIdx + 1 >= static_cast<int>(presetBags_.size())) return;
        int genStart = presetBags_[bagIdx].wGenNdx;
        int genEnd   = presetBags_[bagIdx + 1].wGenNdx;
        int pgenMax  = static_cast<int>(presetGens_.size());
        if (genStart < 0 || genStart > pgenMax) genStart = pgenMax;
        if (genEnd   > pgenMax) genEnd = pgenMax;
        for (int g = genStart; g < genEnd; ++g) {
            u16 oper = presetGens_[g].sfGenOper;
            if (oper == GEN_Instrument) break;
            if (oper >= GEN_COUNT) continue;
            if (IsIllegalPresetSampleGenerator(oper)) continue;
            if (oper == GEN_SampleID || oper == GEN_KeyRange || oper == GEN_VelRange || oper == GEN_Instrument) {
                layer[oper] = ClampGeneratorValue(oper, static_cast<i32>(presetGens_[g].genAmount.wAmount));
            } else {
                layer[oper] = ClampGeneratorValue(oper, static_cast<i32>(presetGens_[g].genAmount.shAmount));
            }
        }
    };

    // Step 1.5: インストグローバルゾーンを適用（SF2 spec: 個別ゾーンのデフォルトを上書き）
    // グローバルゾーン = SampleID を持たない最初のインストバッグ
    auto applyInstLayer = [&](i32* layer, int bagIdx) {
        if (bagIdx < 0 || bagIdx + 1 >= static_cast<int>(instBags_.size())) return;
        int genStart = instBags_[bagIdx].wInstGenNdx;
        int genEnd   = instBags_[bagIdx + 1].wInstGenNdx;
        int igenMax  = static_cast<int>(instGens_.size());
        if (genStart < 0 || genStart > igenMax) genStart = igenMax;
        if (genEnd   > igenMax) genEnd = igenMax;
        for (int g = genStart; g < genEnd; ++g) {
            u16 oper = instGens_[g].sfGenOper;
            if (oper == GEN_SampleID) break;
            if (oper >= GEN_COUNT) continue;
            if (oper == GEN_SampleID || oper == GEN_SampleModes ||
                oper == GEN_KeyRange || oper == GEN_VelRange) {
                layer[oper] = ClampGeneratorValue(oper, static_cast<i32>(instGens_[g].genAmount.wAmount));
            } else {
                layer[oper] = ClampGeneratorValue(oper, static_cast<i32>(instGens_[g].genAmount.shAmount));
            }
        }
    };

    i32 presetLayer[GEN_COUNT];
    i32 instLayer[GEN_COUNT];
    clearLayer(presetLayer);
    clearLayer(instLayer);

    // 記事の説明どおり、同レベルではグローバルゾーンを先に反映し、
    // ローカルゾーンが同じジェネレータを上書きする。
    applyPresetLayer(presetLayer, globalPresetBagIdx);
    applyPresetLayer(presetLayer, presetBagIdx);
    applyInstLayer(instLayer, globalInstBagIdx);
    applyInstLayer(instLayer, instBagIdx);

    // まずインストルメント層をサンプルへ反映
    for (int g = 0; g < GEN_COUNT; ++g) {
        if (instLayer[g] == kUnset) continue;
            outZone.generators[g] = ClampGeneratorValue(static_cast<u16>(g), instLayer[g]);
    }

    // 次にプリセット層を加算/反映
    for (int g = 0; g < GEN_COUNT; ++g) {
        if (presetLayer[g] == kUnset) continue;
        switch (g) {
        case GEN_KeyRange:
        case GEN_VelRange:
        case GEN_Instrument:
            outZone.generators[g] = ClampGeneratorValue(static_cast<u16>(g), presetLayer[g]);
            break;
        default:
            outZone.generators[g] = ClampGeneratorValue(static_cast<u16>(g), outZone.generators[g] + presetLayer[g]);
            break;
        }
    }

    DefaultModulatorState defaultState;
    if (globalInstBagIdx >= 0 && globalInstBagIdx + 1 < static_cast<int>(instBags_.size())) {
        const u8 effectiveKey = ResolveForcedKey(key, outZone);
        const u16 effectiveVelocity = ResolveForcedVelocity(velocity, outZone);
        ApplyModulatorEntries(instMods_,
                              instBags_[globalInstBagIdx].wInstModNdx,
                              instBags_[globalInstBagIdx + 1].wInstModNdx,
                              effectiveKey, effectiveVelocity, ctx, outZone, &defaultState);
    }
    if (instBagIdx >= 0 && instBagIdx + 1 < static_cast<int>(instBags_.size())) {
        const u8 effectiveKey = ResolveForcedKey(key, outZone);
        const u16 effectiveVelocity = ResolveForcedVelocity(velocity, outZone);
        ApplyModulatorEntries(instMods_,
                              instBags_[instBagIdx].wInstModNdx,
                              instBags_[instBagIdx + 1].wInstModNdx,
                              effectiveKey, effectiveVelocity, ctx, outZone, &defaultState);
    }

    const u8 effectiveKey = ResolveForcedKey(key, outZone);
    const u16 effectiveVelocity = ResolveForcedVelocity(velocity, outZone);

    if (!defaultState.hasVelocityToAttenuationMod) {
        const i32 delta = ComputeDefaultVelocityAttenuationCb(effectiveVelocity);
        outZone.generators[GEN_InitialAttenuation] =
            ClampGeneratorValue(GEN_InitialAttenuation, outZone.generators[GEN_InitialAttenuation] + delta);
    }
    if (!defaultState.hasVelocityToFilterFcMod) {
        ApplyModulatorDelta(outZone, GEN_InitialFilterFc, ComputeDefaultVelocityFilterCutoffDelta(effectiveVelocity));
    }
    if (ctx && !defaultState.hasChannelPressureToVibLfoPitchMod && ctx->channelPressure != 0) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultChannelPressureToVibLfoPitchCents) *
            (static_cast<double>(ctx->channelPressure) / 127.0)));
        ApplyModulatorDelta(outZone, GEN_VibLfoToPitch, delta);
    }
    if (ctx && !defaultState.hasCc1ToVibLfoPitchMod && ctx->ccValues[1] != 0) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultCc1ToVibLfoPitchCents) *
            (static_cast<double>(ctx->ccValues[1]) / 127.0)));
        ApplyModulatorDelta(outZone, GEN_VibLfoToPitch, delta);
    }
    if (ctx && ctx->applySf2ChannelDefaults && !defaultState.hasCc7ToInitialAttenuationMod) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultCc7ToInitialAttenuationCb) *
            std::sin((1.0 - static_cast<double>(ctx->ccValues[7]) / 127.0) * (3.14159265358979323846 / 2.0))));
        ApplyModulatorDelta(outZone, GEN_InitialAttenuation, delta);
    }
    if (ctx && ctx->applySf2ChannelDefaults && !defaultState.hasCc10ToPanMod) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultCc10ToPan) *
            (2.0 * (static_cast<double>(ctx->ccValues[10]) / 127.0) - 1.0)));
        ApplyModulatorDelta(outZone, GEN_Pan, delta);
    }
    if (ctx && ctx->applySf2ChannelDefaults && !defaultState.hasCc11ToInitialAttenuationMod) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultCc11ToInitialAttenuationCb) *
            std::sin((1.0 - static_cast<double>(ctx->ccValues[11]) / 127.0) * (3.14159265358979323846 / 2.0))));
        ApplyModulatorDelta(outZone, GEN_InitialAttenuation, delta);
    }
    if (ctx && !defaultState.hasCc91ToReverbSendMod && ctx->ccValues[91] != 0) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultCc91ToReverbSend) *
            (static_cast<double>(ctx->ccValues[91]) / 127.0)));
        ApplyModulatorDelta(outZone, GEN_ReverbEffectsSend, delta);
    }
    if (ctx && !defaultState.hasCc93ToChorusSendMod && ctx->ccValues[93] != 0) {
        const i32 delta = static_cast<i32>(std::lround(
            static_cast<double>(kDefaultCc93ToChorusSend) *
            (static_cast<double>(ctx->ccValues[93]) / 127.0)));
        ApplyModulatorDelta(outZone, GEN_ChorusEffectsSend, delta);
    }
    if (ctx && !defaultState.hasPitchWheelToInitialPitchMod && ctx->pitchBend != 0) {
        const i32 rangeCents = static_cast<i32>(ctx->pitchWheelSensitivitySemitones) * 100 +
                               static_cast<i32>(ctx->pitchWheelSensitivityCents);
        const double bend = std::clamp(static_cast<double>(ctx->pitchBend) / 8192.0, -1.0, 8191.0 / 8192.0);
        ApplyModulatorDelta(outZone, kModDestInitialPitch,
                            static_cast<i32>(std::lround(static_cast<double>(rangeCents) * bend)));
    }

    if (globalPresetBagIdx >= 0 && globalPresetBagIdx + 1 < static_cast<int>(presetBags_.size())) {
        ApplyModulatorEntries(presetMods_,
                              presetBags_[globalPresetBagIdx].wModNdx,
                              presetBags_[globalPresetBagIdx + 1].wModNdx,
                              effectiveKey, effectiveVelocity, ctx, outZone, nullptr);
    }
    if (presetBagIdx >= 0 && presetBagIdx + 1 < static_cast<int>(presetBags_.size())) {
        ApplyModulatorEntries(presetMods_,
                              presetBags_[presetBagIdx].wModNdx,
                              presetBags_[presetBagIdx + 1].wModNdx,
                              effectiveKey, effectiveVelocity, ctx, outZone, nullptr);
    }
    if (ctx && ctx->nrpnOffsets) {
        for (int g = 0; g < GEN_COUNT; ++g) {
            if (ctx->nrpnOffsets[g] == 0) {
                continue;
            }
            outZone.generators[g] = ClampGeneratorValue(
                static_cast<u16>(g), outZone.generators[g] + ctx->nrpnOffsets[g]);
        }
    }
}

bool Sf2File::ApplyModulators(const std::vector<SFModList>& mods, int modStart, int modEnd,
                              u8 key, u16 velocity, const ModulatorContext* ctx, ResolvedZone& zone) const {
    DefaultModulatorState defaultState;
    ApplyModulatorEntries(mods, modStart, modEnd, key, velocity, ctx, zone, &defaultState);
    return defaultState.hasVelocityToAttenuationMod;
}

void Sf2File::ApplyModulatorEntries(const std::vector<SFModList>& mods, int modStart, int modEnd,
                                    u8 key, u16 velocity, const ModulatorContext* ctx, ResolvedZone& zone,
                                    DefaultModulatorState* outDefaultState) const {
    std::vector<ZoneModEntry> entries = BuildEffectiveZoneModEntries(mods, modStart, modEnd);
    std::vector<int> state(entries.size(), 0);
    std::vector<bool> cycle(entries.size(), false);
    std::vector<double> output(entries.size(), 0.0);

    std::function<bool(int)> evalOutput = [&](int index) -> bool {
        if (index < 0 || index >= static_cast<int>(entries.size()) || entries[index].ignored) {
            return false;
        }
        if (state[index] == 2) {
            return !cycle[index];
        }
        if (state[index] == 1) {
            cycle[index] = true;
            entries[index].ignored = true;
            return false;
        }

        state[index] = 1;
        const SFModList& mod = entries[index].mod;

        bool sourceSupported = false;
        double source = 0.0;
        if (IsLinkModSource(mod.sfModSrcOper)) {
            double linkedSum = 0.0;
            bool hasLinkedInput = false;
            for (int incoming : entries[index].incomingLinks) {
                if (!evalOutput(incoming)) {
                    continue;
                }
                hasLinkedInput = true;
                linkedSum += output[incoming];
            }
            if (!hasLinkedInput || cycle[index]) {
                entries[index].ignored = true;
                state[index] = 2;
                return false;
            }
            source = linkedSum;
            sourceSupported = true;
        } else {
            source = DecodeModSourceValue(mod.sfModSrcOper, key, velocity, ctx, sourceSupported);
            if (!sourceSupported) {
                entries[index].ignored = true;
                state[index] = 2;
                return false;
            }
        }

        bool transformSupported = false;
        const double transformedSource = ApplyModSourceTransform(source, mod.sfModTransOper, transformSupported);
        if (!transformSupported) {
            entries[index].ignored = true;
            state[index] = 2;
            return false;
        }

        bool amountSupported = false;
        const double amountSource = DecodeModSourceValue(mod.sfModAmtSrcOper, key, velocity, ctx, amountSupported);
        if (!amountSupported) {
            entries[index].ignored = true;
            state[index] = 2;
            return false;
        }
        const double amountScale = amountSource;
        output[index] = static_cast<double>(mod.modAmount) * transformedSource * amountScale;
        state[index] = 2;
        return !cycle[index];
    };

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const auto& mod = entries[i].mod;
        if (entries[i].ignored) {
            continue;
        }
        if (outDefaultState) {
            outDefaultState->hasVelocityToAttenuationMod |= IsVelocityToInitialAttenuationMod(mod);
            outDefaultState->hasVelocityToFilterFcMod |= IsVelocityToInitialFilterFcMod(mod);
            outDefaultState->hasChannelPressureToVibLfoPitchMod |= IsChannelPressureToVibLfoPitchMod(mod);
            outDefaultState->hasCc1ToVibLfoPitchMod |= IsCc1ToVibLfoPitchMod(mod);
            outDefaultState->hasCc7ToInitialAttenuationMod |= IsCc7ToInitialAttenuationMod(mod);
            outDefaultState->hasCc10ToPanMod |= IsCc10ToPanMod(mod);
            outDefaultState->hasCc11ToInitialAttenuationMod |= IsCc11ToInitialAttenuationMod(mod);
            outDefaultState->hasCc91ToReverbSendMod |= IsCc91ToReverbSendMod(mod);
            outDefaultState->hasCc93ToChorusSendMod |= IsCc93ToChorusSendMod(mod);
            outDefaultState->hasPitchWheelToInitialPitchMod |= IsPitchWheelToInitialPitchMod(mod);
        }
        if ((mod.sfModDestOper & 0x8000u) != 0) continue;
        if (!IsSupportedModulatorDestination(mod.sfModDestOper)) continue;
        if (!evalOutput(i) || entries[i].ignored || cycle[i]) continue;

        const i32 delta = static_cast<i32>(std::lround(output[i]));
        if (delta == 0) continue;
        ApplyModulatorDelta(zone, mod.sfModDestOper, delta);
    }
}

void Sf2File::ScanUnsupportedModulators() {
    unsupportedModulatorCount_ = 0;
    unsupportedModulatorTransformCount_ = 0;

    auto scan = [&](const std::vector<SFModList>& mods) {
        for (const auto& mod : mods) {
            const bool isTerminal =
                mod.sfModSrcOper == 0 &&
                mod.sfModDestOper == 0 &&
                mod.modAmount == 0 &&
                mod.sfModAmtSrcOper == 0 &&
                mod.sfModTransOper == 0;
            if (isTerminal) {
                continue;
            }
            const bool unsupportedTransform = !IsSupportedModTransform(mod.sfModTransOper);
            const bool unsupportedSource = !IsSupportedModSourceOperDefinition(mod.sfModSrcOper);
            const bool unsupportedAmountSource = !IsSupportedModSourceOperDefinition(mod.sfModAmtSrcOper);
            const bool unsupportedDestination =
                ((mod.sfModDestOper & 0x8000u) == 0) &&
                ((mod.sfModDestOper >= GEN_COUNT) || !IsSupportedModulatorDestination(mod.sfModDestOper));
            if (unsupportedTransform) {
                ++unsupportedModulatorTransformCount_;
            }
            if (unsupportedTransform || unsupportedSource || unsupportedAmountSource || unsupportedDestination) {
                ++unsupportedModulatorCount_;
            }
        }
    };

    scan(presetMods_);
    scan(instMods_);
}


void Sf2File::BuildPresetIndex() {
    presetIndexMap_.clear();
    if (presets_.size() < 2) {
        return;
    }

    presetIndexMap_.reserve(presets_.size());
    for (int pi = 0; pi + 1 < static_cast<int>(presets_.size()); ++pi) {
        const auto& ph = presets_[pi];
        const u32 key = (static_cast<u32>(ph.wBank) << 8) | static_cast<u32>(ph.wPreset);
        presetIndexMap_[key].push_back(pi);
    }
}

bool Sf2File::FindZones(u16 bank, u8 program, u8 key, u16 velocity,
                         std::vector<ResolvedZone>& outZones,
                         const ModulatorContext* ctx) const {
    outZones.clear();

    // SF2 ゾーン範囲は 0-127 (u8) — 16-bit velocity を 7-bit に変換して比較。
    // velocity >> 9 は端数誤差が生じるため四捨五入スケーリングを使用する。
    // velocity=0→0, velocity=65535→127 が保証される。
    const u8 vel7 = static_cast<u8>(
        (static_cast<u32>(velocity) * 127u + 32767u) / 65535u);

    const u32 presetKey = (static_cast<u32>(bank) << 8) | static_cast<u32>(program);
    const auto presetIt = presetIndexMap_.find(presetKey);
    if (presetIt == presetIndexMap_.end()) {
        return false;
    }

    for (const int pi : presetIt->second) {
        const auto& ph = presets_[pi];

        // このプリセットのプリセットバッグを走査
        int pbagStart = ph.wPresetBagNdx;
        int pbagEnd   = presets_[pi + 1].wPresetBagNdx;

        // 境界チェック
        int pbagMax = static_cast<int>(presetBags_.size());
        if (pbagStart < 0 || pbagStart >= pbagMax) continue;
        if (pbagEnd   > pbagMax) pbagEnd = pbagMax;

        int globalPresetBag = -1;
        if (pbagStart < pbagEnd) {
            bool isGlobal = false;
            u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
            int instrumentIdx = -1;
            if (AnalyzePresetBag(pbagStart, isGlobal, keyLo, keyHi, velLo, velHi, instrumentIdx) && isGlobal) {
                globalPresetBag = pbagStart;
            }
        }

        for (int pb = pbagStart; pb < pbagEnd; ++pb) {
            bool presetIsGlobal = false;
            u8 pkeyLo = 0, pkeyHi = 127, pvelLo = 0, pvelHi = 127;
            int instrumentIdx = -1;
            if (!AnalyzePresetBag(pb, presetIsGlobal, pkeyLo, pkeyHi, pvelLo, pvelHi, instrumentIdx) ||
                presetIsGlobal) {
                continue;
            }

            if (key < pkeyLo || key > pkeyHi) continue;
            if (vel7 < pvelLo || vel7 > pvelHi) continue;
            if (instrumentIdx < 0 || instrumentIdx + 1 >= static_cast<int>(instruments_.size())) continue;

            // インスト層のバッグを走査
            int ibagStart = instruments_[instrumentIdx    ].wInstBagNdx;
            int ibagEnd   = instruments_[instrumentIdx + 1].wInstBagNdx;

            // ibag 境界チェック
            int ibagMax = static_cast<int>(instBags_.size());
            if (ibagStart < 0 || ibagStart >= ibagMax) continue;
            if (ibagEnd   > ibagMax) ibagEnd = ibagMax;

            // グローバルゾーン検出: SampleID を持たない最初のバッグ（SF2 spec 7.7）
            int globalInstBag = -1;
            if (ibagStart < ibagEnd) {
                bool isGlobal = false;
                u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
                int sampleIdx = -1;
                if (AnalyzeInstrumentBag(ibagStart, isGlobal, keyLo, keyHi, velLo, velHi, sampleIdx) && isGlobal) {
                    globalInstBag = ibagStart;
                }
            }

            for (int ib = ibagStart; ib < ibagEnd; ++ib) {
                bool instIsGlobal = false;
                u8  ikeyLo = 0, ikeyHi = 127, ivelLo = 0, ivelHi = 127;
                int sampleIdx = -1;
                if (!AnalyzeInstrumentBag(ib, instIsGlobal, ikeyLo, ikeyHi, ivelLo, ivelHi, sampleIdx) ||
                    instIsGlobal) {
                    continue;
                }

                if (key < ikeyLo || key > ikeyHi) continue;
                if (vel7 < ivelLo || vel7 > ivelHi) continue;
                if (sampleIdx < 0 || sampleIdx >= static_cast<int>(samples_.size())) continue;

                const SFSample* smp = &samples_[sampleIdx];
                ResolvedZone zone;
                ResolveZone(globalPresetBag, globalInstBag, ib, pb, &sampleHeaders_[sampleIdx], key, velocity, ctx, zone);
                if ((smp->sfSampleType & 0x8000u) != 0u) {
                    if (!hasValidIrom_ || !hasValidIver_ || !romBank_ || romBank_->SampleDataCount() == 0) {
                        continue;
                    }
                    zone.sampleDataOverride = romBank_->SampleData();
                    zone.sampleData24Override = romBank_->SampleData24();
                    zone.sampleDataOverrideCount = romBank_->SampleDataCount();
                }
                // SampleID を確定値で設定
                zone.generators[GEN_SampleID] = sampleIdx;
                zone.sampleId = sampleIdx;
                outZones.push_back(zone);
            }
        }
    }
    return !outZones.empty();
}

bool Sf2File::GetPresetBagIndices(u16 bank, u8 program, int& outGlobalBagIdx, int& outLocalBagIdx) const {
    outGlobalBagIdx = -1;
    outLocalBagIdx = -1;

    const u32 presetKey = (static_cast<u32>(bank) << 8) | static_cast<u32>(program);
    const auto it = presetIndexMap_.find(presetKey);
    if (it == presetIndexMap_.end() || it->second.empty()) {
        return false;
    }

    const int pi = it->second[0];
    const auto& ph = presets_[pi];

    int pbagStart = ph.wPresetBagNdx;
    int pbagEnd = presets_[pi + 1].wPresetBagNdx;

    int pbagMax = static_cast<int>(presetBags_.size());
    if (pbagStart < 0 || pbagStart >= pbagMax) return false;
    if (pbagEnd > pbagMax) pbagEnd = pbagMax;

    if (pbagStart < pbagEnd) {
        bool isGlobal = false;
        u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int instrumentIdx = -1;
        if (AnalyzePresetBag(pbagStart, isGlobal, keyLo, keyHi, velLo, velHi, instrumentIdx) && isGlobal) {
            outGlobalBagIdx = pbagStart;
        }
    }

    const int localStart = (outGlobalBagIdx == pbagStart) ? (pbagStart + 1) : pbagStart;
    for (int pb = localStart; pb < pbagEnd; ++pb) {
        bool isGlobal = false;
        u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int instrumentIdx = -1;
        if (!AnalyzePresetBag(pb, isGlobal, keyLo, keyHi, velLo, velHi, instrumentIdx) || isGlobal) {
            continue;
        }
        outLocalBagIdx = pb;
        return true;
    }
    return false;
}

bool Sf2File::GetInstrumentBagIndices(int instrumentIdx, int localBagIdx, int& outGlobalBagIdx) const {
    outGlobalBagIdx = -1;

    if (instrumentIdx < 0 || instrumentIdx + 1 >= static_cast<int>(instruments_.size())) {
        return false;
    }

    int ibagStart = instruments_[instrumentIdx].wInstBagNdx;
    int ibagEnd = instruments_[instrumentIdx + 1].wInstBagNdx;

    int ibagMax = static_cast<int>(instBags_.size());
    if (ibagStart < 0 || ibagStart >= ibagMax) return false;
    if (ibagEnd > ibagMax) ibagEnd = ibagMax;

    if (ibagStart < ibagEnd) {
        bool isGlobal = false;
        u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int sampleIdx = -1;
        if (AnalyzeInstrumentBag(ibagStart, isGlobal, keyLo, keyHi, velLo, velHi, sampleIdx) && isGlobal) {
            outGlobalBagIdx = ibagStart;
        }
    }

    if (localBagIdx < ibagStart || localBagIdx >= ibagEnd) {
        return false;
    }
    bool isGlobal = false;
    u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    int sampleIdx = -1;
    return AnalyzeInstrumentBag(localBagIdx, isGlobal, keyLo, keyHi, velLo, velHi, sampleIdx) &&
           !isGlobal &&
           localBagIdx != outGlobalBagIdx;
}

void Sf2File::GetGeneratorLayer(int genStart, int genEnd, i32 outGens[GEN_COUNT]) const {
    const i32* defaults = GetSF2GeneratorDefaults();
    std::memcpy(outGens, defaults, GEN_COUNT * sizeof(i32));

    if (genStart < 0 || genEnd <= genStart) return;

    int genMax = static_cast<int>(presetGens_.size());
    if (genStart > genMax) return;
    if (genEnd > genMax) genEnd = genMax;

    for (int g = genStart; g < genEnd; ++g) {
        u16 oper = presetGens_[g].sfGenOper;
        if (oper == GEN_Instrument) break;
        if (oper >= GEN_COUNT) continue;
        if (IsIllegalPresetSampleGenerator(oper)) continue;
        outGens[oper] = ClampGeneratorValue(oper, static_cast<i32>(presetGens_[g].genAmount.shAmount));
    }
}

void Sf2File::GetPresetGeneratorLayer(int bagIdx, i32 outGens[GEN_COUNT]) const {
    const i32* defaults = GetSF2GeneratorDefaults();
    std::memcpy(outGens, defaults, GEN_COUNT * sizeof(i32));

    int pbagMax = static_cast<int>(presetBags_.size());
    if (bagIdx < 0 || bagIdx >= pbagMax) return;

    int genStart = presetBags_[bagIdx].wGenNdx;
    int genEnd = (bagIdx + 1 < pbagMax) ? presetBags_[bagIdx + 1].wGenNdx : static_cast<int>(presetGens_.size());

    int pgenMax = static_cast<int>(presetGens_.size());
    if (genStart < 0 || genStart > pgenMax) genStart = pgenMax;
    if (genEnd > pgenMax) genEnd = pgenMax;

    for (int g = genStart; g < genEnd; ++g) {
        u16 oper = presetGens_[g].sfGenOper;
        if (oper == GEN_Instrument) break;
        if (oper >= GEN_COUNT) continue;
        if (IsIllegalPresetSampleGenerator(oper)) continue;
        if (oper == GEN_SampleID || oper == GEN_KeyRange || oper == GEN_VelRange || oper == GEN_Instrument) {
            outGens[oper] = ClampGeneratorValue(oper, static_cast<i32>(presetGens_[g].genAmount.wAmount));
        } else {
            outGens[oper] = ClampGeneratorValue(oper, static_cast<i32>(presetGens_[g].genAmount.shAmount));
        }
    }
}

void Sf2File::GetInstrumentGeneratorLayer(int bagIdx, i32 outGens[GEN_COUNT]) const {
    const i32* defaults = GetSF2GeneratorDefaults();
    std::memcpy(outGens, defaults, GEN_COUNT * sizeof(i32));

    int ibagMax = static_cast<int>(instBags_.size());
    if (bagIdx < 0 || bagIdx >= ibagMax) return;

    int genStart = instBags_[bagIdx].wInstGenNdx;
    int genEnd = (bagIdx + 1 < ibagMax) ? instBags_[bagIdx + 1].wInstGenNdx : static_cast<int>(instGens_.size());

    int igenMax = static_cast<int>(instGens_.size());
    if (genStart < 0 || genStart > igenMax) genStart = igenMax;
    if (genEnd > igenMax) genEnd = igenMax;

    for (int g = genStart; g < genEnd; ++g) {
        u16 oper = instGens_[g].sfGenOper;
        if (oper == GEN_SampleID) break;
        if (oper >= GEN_COUNT) continue;
        if (oper == GEN_SampleID || oper == GEN_SampleModes || oper == GEN_KeyRange || oper == GEN_VelRange) {
            outGens[oper] = ClampGeneratorValue(oper, static_cast<i32>(instGens_[g].genAmount.wAmount));
        } else {
            outGens[oper] = ClampGeneratorValue(oper, static_cast<i32>(instGens_[g].genAmount.shAmount));
        }
    }
}

int Sf2File::GetInstrumentIndex(int presetInstrumentGenValue) const {
    if (presetInstrumentGenValue < 0 || presetInstrumentGenValue >= static_cast<int>(instruments_.size())) {
        return -1;
    }
    return presetInstrumentGenValue;
}

bool Sf2File::GetInstrumentLocalZones(int instrumentIdx, std::vector<ZoneInfo>& outZones) const {
    outZones.clear();
    if (instrumentIdx < 0 || instrumentIdx + 1 >= static_cast<int>(instruments_.size())) {
        return false;
    }

    int ibagStart = instruments_[instrumentIdx    ].wInstBagNdx;
    int ibagEnd   = instruments_[instrumentIdx + 1].wInstBagNdx;
    int ibagMax = static_cast<int>(instBags_.size());
    if (ibagStart < 0 || ibagStart >= ibagMax) return false;
    if (ibagEnd > ibagMax) ibagEnd = ibagMax;

    for (int ib = ibagStart; ib < ibagEnd; ++ib) {
        ZoneInfo zi;
        zi.bagIndex = ib;
        zi.keyLo = 0; zi.keyHi = 127;
        zi.velLo = 0; zi.velHi = 127;
        zi.sampleId = -1;
        memset(zi.generators, 0, sizeof(zi.generators));

        bool isGlobal = false;
        u8 keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
        int sampleIdx = -1;
        if (!AnalyzeInstrumentBag(ib, isGlobal, keyLo, keyHi, velLo, velHi, sampleIdx) || isGlobal) {
            continue;
        }
        zi.keyLo = keyLo;
        zi.keyHi = keyHi;
        zi.velLo = velLo;
        zi.velHi = velHi;
        zi.sampleId = sampleIdx;

        int igenStart = instBags_[ib].wInstGenNdx;
        int igenEnd   = instBags_[ib + 1].wInstGenNdx;
        int igenMax = static_cast<int>(instGens_.size());
        if (igenStart < 0 || igenStart > igenMax) igenStart = igenMax;
        if (igenEnd < igenStart) igenEnd = igenStart;
        if (igenEnd > igenMax) igenEnd = igenMax;

        for (int ig = igenStart; ig < igenEnd; ++ig) {
            u16 oper = instGens_[ig].sfGenOper;
            if (oper == GEN_SampleID) {
                break;
            }
            if (oper == GEN_KeyRange || oper == GEN_VelRange) {
                continue;
            }
            if (oper < GEN_COUNT) {
                zi.generators[oper] = static_cast<i32>(instGens_[ig].genAmount.shAmount);
            }
        }
        outZones.push_back(zi);
    }
    return !outZones.empty();
}

} // namespace XArkMidi

