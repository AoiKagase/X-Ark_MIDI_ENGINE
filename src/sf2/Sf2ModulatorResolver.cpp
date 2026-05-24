/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "Sf2ModulatorResolver.h"
#include "../soundbank/SoundBank.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace XArkMidi {
namespace {

constexpr u16 kSourceIndexMask = 0x007Fu;
constexpr u16 kSourceCcPalette = 0x0080u;
constexpr u16 kSourceDirectionNegative = 0x0100u;
constexpr u16 kSourceBipolar = 0x0200u;
constexpr u16 kSourceTypeShift = 10u;
constexpr u16 kSourceLink = 127u;

constexpr u16 kSrcVelocityConcaveNegativeUnipolar = 0x0502u;
constexpr u16 kSrcVelocityLinearNegativeUnipolar = 0x0102u;
constexpr u16 kSrcChannelPressure = 0x000Du;
constexpr u16 kSrcCc1 = 0x0081u;
constexpr u16 kSrcCc7ConcaveNegativeUnipolar = 0x0587u;
constexpr u16 kSrcCc10LinearPositiveBipolar = 0x028Au;
constexpr u16 kSrcCc11ConcaveNegativeUnipolar = 0x058Bu;
constexpr u16 kSrcCc91 = 0x00DBu;
constexpr u16 kSrcCc93 = 0x00DDu;
constexpr u16 kSrcPitchWheelLinearPositiveBipolar = 0x020Eu;
constexpr u16 kSrcPitchWheelSensitivity = 0x0010u;

constexpr u16 kTransformLinear = 0u;
constexpr u16 kTransformAbsolute = 2u;
constexpr u16 kInternalInitialPitchDestination = GEN_COUNT;

struct WorkingModulator {
    SFModList mod{};
    Sf2ModulatorLevel level = Sf2ModulatorLevel::InstrumentLocal;
    int rawIndex = -1;
    bool ignored = false;
    bool implicitDefault = false;
    Sf2ModulatorValidity validity = Sf2ModulatorValidity::Valid;
    Sf2ModulatorDependency dependencies = Sf2ModulatorDependency::None;
    std::vector<int> incomingLinks;
};

struct DecodeSourceResult {
    bool valid = false;
    double value = 0.0;
    Sf2ModulatorDependency dependencies = Sf2ModulatorDependency::None;
};

bool IsTerminalModulator(const SFModList& mod) {
    return mod.sfModSrcOper == 0 &&
           mod.sfModDestOper == 0 &&
           mod.modAmount == 0 &&
           mod.sfModAmtSrcOper == 0 &&
           mod.sfModTransOper == 0;
}

bool IsLinkSource(u16 source) {
    return (source & kSourceIndexMask) == kSourceLink && (source & kSourceCcPalette) == 0;
}

bool IsLinkDestination(u16 destination) {
    return (destination & 0x8000u) != 0;
}

bool IsIllegalCcIndex(u16 index) {
    return index == 0 ||
           index == 6 ||
           index == 32 ||
           index == 38 ||
           (index >= 33 && index <= 63) ||
           (index >= 98 && index <= 101) ||
           (index >= 120 && index <= 127);
}

double Clamp01(double x) {
    return std::clamp(x, 0.0, 1.0);
}

double ConcaveCurve(double x) {
    return std::sqrt(Clamp01(x));
}

double ConvexCurve(double x) {
    return 1.0 - std::sqrt(1.0 - Clamp01(x));
}

double ApplySourceShape(double x, u16 sourceOper) {
    const u16 type = (sourceOper >> kSourceTypeShift) & 0x3Fu;
    switch (type) {
    case 0:
        break;
    case 1:
        x = ConcaveCurve(x);
        break;
    case 2:
        x = ConvexCurve(x);
        break;
    case 3:
        x = (x >= 0.5) ? 1.0 : 0.0;
        break;
    default:
        return 0.0;
    }

    const bool negative = (sourceOper & kSourceDirectionNegative) != 0;
    const bool bipolar = (sourceOper & kSourceBipolar) != 0;
    if (!bipolar) {
        return negative ? (1.0 - x) : x;
    }
    return negative ? (1.0 - 2.0 * x) : (2.0 * x - 1.0);
}

double Normalize7Bit(u8 value) {
    return static_cast<double>(value) / 128.0;
}

double Normalize14BitBipolar(i16 value) {
    return std::clamp(static_cast<double>(value) / 8192.0, -1.0, 8191.0 / 8192.0);
}

double NormalizeVelocity(u16 velocity) {
    return std::clamp(static_cast<double>(velocity) / 65536.0, 0.0, 65535.0 / 65536.0);
}

DecodeSourceResult DecodeSource(u16 sourceOper, u8 key, u16 velocity, const ModulatorContext* ctx, bool allowLinkSource) {
    DecodeSourceResult result;
    if (sourceOper == 0) {
        result.valid = true;
        result.value = 1.0;
        return result;
    }
    if (IsLinkSource(sourceOper)) {
        result.valid = allowLinkSource;
        return result;
    }
    if (!IsSf2SpecModulatorSourceDefinition(sourceOper, false)) {
        return result;
    }

    const u16 index = sourceOper & kSourceIndexMask;
    const bool cc = (sourceOper & kSourceCcPalette) != 0;
    double x = 0.0;

    if (cc) {
        if (!ctx) {
            return result;
        }
        x = Normalize7Bit(ctx->ccValues[index]);
        result.dependencies = Sf2ModulatorDependency::ChannelController;
    } else {
        switch (index) {
        case 2:
            x = NormalizeVelocity(velocity);
            result.dependencies = Sf2ModulatorDependency::NoteOn;
            break;
        case 3:
            x = Normalize7Bit(key);
            result.dependencies = Sf2ModulatorDependency::NoteOn;
            break;
        case 10:
            if (!ctx) {
                return result;
            }
            x = Normalize7Bit(ctx->polyPressure[key]);
            result.dependencies = Sf2ModulatorDependency::PolyPressure;
            break;
        case 13:
            if (!ctx) {
                return result;
            }
            x = Normalize7Bit(ctx->channelPressure);
            result.dependencies = Sf2ModulatorDependency::ChannelPressure;
            break;
        case 14:
            if (!ctx) {
                return result;
            }
            x = (Normalize14BitBipolar(ctx->pitchBend) + 1.0) * 0.5;
            result.dependencies = Sf2ModulatorDependency::PitchWheel;
            break;
        case 16:
            if (!ctx) {
                return result;
            }
            x = std::clamp((static_cast<double>(ctx->pitchWheelSensitivitySemitones) +
                            static_cast<double>(ctx->pitchWheelSensitivityCents) / 100.0) / 128.0,
                           0.0, 127.0 / 128.0);
            result.dependencies = Sf2ModulatorDependency::PitchWheelSensitivity;
            break;
        default:
            return result;
        }
    }

    result.valid = true;
    result.value = ApplySourceShape(x, sourceOper);
    return result;
}

double ApplyTransform(double value, u16 transform, bool& valid) {
    valid = true;
    switch (transform) {
    case kTransformLinear:
        return value;
    case kTransformAbsolute:
        return std::fabs(value);
    default:
        valid = false;
        return 0.0;
    }
}

Sf2ModulatorValidity ValidateModulatorDefinition(const SFModList& mod, Sf2ModulatorLevel level) {
    const bool allowInternalDestination = level == Sf2ModulatorLevel::ImplicitInstrumentDefault;
    if (IsLinkDestination(mod.sfModDestOper)) {
        if (!IsSf2SpecModulatorSourceDefinition(mod.sfModSrcOper, false)) {
            return Sf2ModulatorValidity::InvalidSource;
        }
    } else if (!IsSf2SpecValueGeneratorDestination(mod.sfModDestOper) &&
               !(allowInternalDestination && mod.sfModDestOper == kInternalInitialPitchDestination)) {
        return Sf2ModulatorValidity::InvalidDestination;
    } else if (!IsSf2SpecModulatorSourceDefinition(mod.sfModSrcOper, true)) {
        return Sf2ModulatorValidity::InvalidSource;
    }

    if (IsLinkSource(mod.sfModAmtSrcOper) || !IsSf2SpecModulatorSourceDefinition(mod.sfModAmtSrcOper, false)) {
        return Sf2ModulatorValidity::InvalidAmountSource;
    }
    if (!IsSf2SpecModulatorTransform(mod.sfModTransOper)) {
        return Sf2ModulatorValidity::InvalidTransform;
    }
    return Sf2ModulatorValidity::Valid;
}

std::vector<WorkingModulator> NormalizeZone(const Sf2ModulatorZone& zone) {
    std::vector<WorkingModulator> entries;
    entries.reserve(zone.count);
    std::map<std::tuple<u16, u16, u16, u16>, int> duplicateMap;
    std::map<int, int> rawToEntry;

    for (size_t i = 0; i < zone.count; ++i) {
        const SFModList& mod = zone.mods[i];
        if (IsTerminalModulator(mod)) {
            continue;
        }

        WorkingModulator entry;
        entry.mod = mod;
        entry.level = zone.level;
        entry.rawIndex = static_cast<int>(i);
        entry.validity = ValidateModulatorDefinition(mod, zone.level);
        entry.dependencies = Sf2ModulatorDependency::None;
        entries.push_back(entry);
        const int entryIndex = static_cast<int>(entries.size()) - 1;
        rawToEntry[entry.rawIndex] = entryIndex;

        const auto key = std::make_tuple(mod.sfModSrcOper, mod.sfModDestOper,
                                         mod.sfModAmtSrcOper, mod.sfModTransOper);
        const auto duplicate = duplicateMap.find(key);
        if (duplicate != duplicateMap.end()) {
            entries[duplicate->second].ignored = true;
        }
        duplicateMap[key] = entryIndex;
    }

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        WorkingModulator& entry = entries[i];
        if (entry.ignored || entry.validity != Sf2ModulatorValidity::Valid) {
            continue;
        }
        if (!IsLinkDestination(entry.mod.sfModDestOper)) {
            continue;
        }
        const int targetRaw = static_cast<int>(entry.mod.sfModDestOper & 0x7FFFu);
        const auto target = rawToEntry.find(targetRaw);
        if (target == rawToEntry.end()) {
            entry.validity = Sf2ModulatorValidity::InvalidLink;
            entry.ignored = true;
            continue;
        }
        entries[target->second].incomingLinks.push_back(i);
    }

    std::vector<int> state(entries.size(), 0);
    std::vector<bool> inCycle(entries.size(), false);
    std::vector<int> stack;
    std::function<void(int)> visit = [&](int index) {
        if (index < 0 || index >= static_cast<int>(entries.size()) || entries[index].ignored) {
            return;
        }
        if (state[index] == 2) {
            return;
        }
        if (state[index] == 1) {
            for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
                inCycle[*it] = true;
                if (*it == index) {
                    break;
                }
            }
            return;
        }
        state[index] = 1;
        stack.push_back(index);
        for (int incoming : entries[index].incomingLinks) {
            visit(incoming);
        }
        stack.pop_back();
        state[index] = 2;
    };

    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        visit(i);
    }
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if (inCycle[i]) {
            entries[i].validity = Sf2ModulatorValidity::LinkCycle;
            entries[i].ignored = true;
        }
    }

    return entries;
}

void AddOrReplaceInstrument(std::vector<Sf2ResolvedModulator>& out, const WorkingModulator& entry) {
    const Sf2ModulatorIdentity identity = MakeSf2ModulatorIdentity(entry.mod);
    for (auto& existing : out) {
        if (existing.participatesInDefaultSuppression &&
            existing.validity == Sf2ModulatorValidity::Valid &&
            MakeSf2ModulatorIdentity(existing.mod) == identity) {
            existing.mod = entry.mod;
            existing.level = entry.level;
            existing.dependencies = entry.dependencies;
            return;
        }
    }

    Sf2ResolvedModulator resolved;
    resolved.mod = entry.mod;
    resolved.level = entry.level;
    resolved.validity = entry.validity;
    resolved.participatesInDefaultSuppression = entry.validity == Sf2ModulatorValidity::Valid;
    resolved.dependencies = entry.dependencies;
    out.push_back(resolved);
}

void AddPreset(std::vector<Sf2ResolvedModulator>& out, const WorkingModulator& entry) {
    Sf2ResolvedModulator resolved;
    resolved.mod = entry.mod;
    resolved.level = entry.level;
    resolved.validity = entry.validity;
    resolved.participatesInDefaultSuppression = false;
    resolved.dependencies = entry.dependencies;
    out.push_back(resolved);
}

bool IsInstrumentLevel(Sf2ModulatorLevel level) {
    return level == Sf2ModulatorLevel::ImplicitInstrumentDefault ||
           level == Sf2ModulatorLevel::InstrumentGlobal ||
           level == Sf2ModulatorLevel::InstrumentLocal;
}

} // namespace

bool IsSf2SpecValueGeneratorDestination(u16 destination) {
    switch (destination) {
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
    case GEN_InitialAttenuation:
    case GEN_CoarseTune:
    case GEN_FineTune:
    case GEN_ScaleTuning:
        return true;
    default:
        return false;
    }
}

bool IsSf2SpecModulatorSourceDefinition(u16 source, bool allowLinkSource) {
    if (source == 0) {
        return true;
    }
    if (IsLinkSource(source)) {
        return allowLinkSource;
    }

    const u16 index = source & kSourceIndexMask;
    const bool cc = (source & kSourceCcPalette) != 0;
    const u16 type = (source >> kSourceTypeShift) & 0x3Fu;
    if (type > 3) {
        return false;
    }
    if (cc) {
        return !IsIllegalCcIndex(index);
    }

    switch (index) {
    case 2:
    case 3:
    case 10:
    case 13:
    case 14:
    case 16:
        return true;
    default:
        return false;
    }
}

bool IsSf2SpecModulatorTransform(u16 transform) {
    return transform == kTransformLinear || transform == kTransformAbsolute;
}

Sf2ModulatorIdentity MakeSf2ModulatorIdentity(const SFModList& mod) {
    Sf2ModulatorIdentity identity;
    identity.source = mod.sfModSrcOper;
    identity.destination = mod.sfModDestOper;
    identity.amountSource = mod.sfModAmtSrcOper;
    identity.transform = mod.sfModTransOper;
    return identity;
}

std::vector<SFModList> GetSf2ImplicitDefaultModulators() {
    return {
        { kSrcVelocityConcaveNegativeUnipolar, GEN_InitialAttenuation, 960, 0, kTransformLinear },
        { kSrcVelocityLinearNegativeUnipolar, GEN_InitialFilterFc, -2400, 0, kTransformLinear },
        { kSrcChannelPressure, GEN_VibLfoToPitch, 50, 0, kTransformLinear },
        { kSrcCc1, GEN_VibLfoToPitch, 50, 0, kTransformLinear },
        { kSrcCc7ConcaveNegativeUnipolar, GEN_InitialAttenuation, 960, 0, kTransformLinear },
        { kSrcCc10LinearPositiveBipolar, GEN_Pan, 1000, 0, kTransformLinear },
        { kSrcCc11ConcaveNegativeUnipolar, GEN_InitialAttenuation, 960, 0, kTransformLinear },
        { kSrcCc91, GEN_ReverbEffectsSend, 200, 0, kTransformLinear },
        { kSrcCc93, GEN_ChorusEffectsSend, 200, 0, kTransformLinear },
        { kSrcPitchWheelLinearPositiveBipolar, kInternalInitialPitchDestination, 12700, kSrcPitchWheelSensitivity, kTransformLinear },
    };
}

std::vector<Sf2ResolvedModulator> BuildSf2EffectiveModulators(const std::vector<Sf2ModulatorZone>& zones,
                                                              bool includeImplicitDefaults) {
    std::vector<Sf2ResolvedModulator> result;

    std::vector<SFModList> implicitDefaults;
    if (includeImplicitDefaults) {
        implicitDefaults = GetSf2ImplicitDefaultModulators();
        Sf2ModulatorZone defaultZone;
        defaultZone.level = Sf2ModulatorLevel::ImplicitInstrumentDefault;
        defaultZone.mods = implicitDefaults.data();
        defaultZone.count = implicitDefaults.size();
        for (const auto& entry : NormalizeZone(defaultZone)) {
            if (!entry.ignored && entry.validity == Sf2ModulatorValidity::Valid) {
                AddOrReplaceInstrument(result, entry);
            }
        }
    }

    std::vector<Sf2ModulatorZone> presetZones;
    for (const auto& zone : zones) {
        std::vector<WorkingModulator> normalized = NormalizeZone(zone);
        if (IsInstrumentLevel(zone.level)) {
            for (const auto& entry : normalized) {
                if (!entry.ignored && entry.validity == Sf2ModulatorValidity::Valid) {
                    AddOrReplaceInstrument(result, entry);
                }
            }
        } else {
            presetZones.push_back(zone);
        }
    }

    std::vector<Sf2ResolvedModulator> effectivePreset;
    for (const auto& zone : presetZones) {
        std::vector<WorkingModulator> normalized = NormalizeZone(zone);
        for (const auto& entry : normalized) {
            if (entry.ignored || entry.validity != Sf2ModulatorValidity::Valid) {
                continue;
            }
            const Sf2ModulatorIdentity identity = MakeSf2ModulatorIdentity(entry.mod);
            bool replaced = false;
            if (zone.level == Sf2ModulatorLevel::PresetLocal) {
                for (auto& existing : effectivePreset) {
                    if (existing.level == Sf2ModulatorLevel::PresetGlobal &&
                        MakeSf2ModulatorIdentity(existing.mod) == identity) {
                        existing.mod = entry.mod;
                        existing.level = entry.level;
                        replaced = true;
                        break;
                    }
                }
            }
            if (!replaced) {
                AddPreset(effectivePreset, entry);
            }
        }
    }

    result.insert(result.end(), effectivePreset.begin(), effectivePreset.end());
    return result;
}

std::vector<Sf2ModulatorEvaluation> EvaluateSf2Modulators(const std::vector<Sf2ResolvedModulator>& modulators,
                                                          u8 key, u16 velocity,
                                                          const ModulatorContext* ctx) {
    std::vector<Sf2ModulatorEvaluation> result;
    for (const auto& modulator : modulators) {
        if (modulator.validity != Sf2ModulatorValidity::Valid ||
            IsLinkDestination(modulator.mod.sfModDestOper) ||
            (!IsSf2SpecValueGeneratorDestination(modulator.mod.sfModDestOper) &&
             modulator.mod.sfModDestOper != kInternalInitialPitchDestination)) {
            continue;
        }

        DecodeSourceResult source = DecodeSource(modulator.mod.sfModSrcOper, key, velocity, ctx, false);
        DecodeSourceResult amountSource = DecodeSource(modulator.mod.sfModAmtSrcOper, key, velocity, ctx, false);
        if (!source.valid || !amountSource.valid) {
            continue;
        }

        bool transformValid = false;
        const double transformed = ApplyTransform(
            static_cast<double>(modulator.mod.modAmount) * source.value * amountSource.value,
            modulator.mod.sfModTransOper,
            transformValid);
        if (!transformValid) {
            continue;
        }

        Sf2ModulatorEvaluation evaluation;
        evaluation.destination = modulator.mod.sfModDestOper;
        evaluation.amount = static_cast<i32>(std::lround(transformed));
        evaluation.dependencies = source.dependencies | amountSource.dependencies | modulator.dependencies;
        result.push_back(evaluation);
    }
    return result;
}

} // namespace XArkMidi
