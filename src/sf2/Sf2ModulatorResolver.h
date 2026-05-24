/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "Sf2Types.h"
#include <vector>

namespace XArkMidi {

struct ModulatorContext;

enum class Sf2ModulatorLevel : u8 {
    ImplicitInstrumentDefault,
    InstrumentGlobal,
    InstrumentLocal,
    PresetGlobal,
    PresetLocal,
};

enum class Sf2ModulatorValidity : u8 {
    Valid,
    InvalidSource,
    InvalidAmountSource,
    InvalidTransform,
    InvalidDestination,
    InvalidLink,
    LinkCycle,
};

enum class Sf2ModulatorDependency : u16 {
    None = 0,
    NoteOn = 1u << 0,
    ChannelController = 1u << 1,
    ChannelPressure = 1u << 2,
    PolyPressure = 1u << 3,
    PitchWheel = 1u << 4,
    PitchWheelSensitivity = 1u << 5,
};

enum class Sf2ModulatorDestinationClass : u8 {
    Ignored,
    Mix,
    Filter,
    Pitch,
    Envelope,
    Lfo,
};

inline Sf2ModulatorDependency operator|(Sf2ModulatorDependency a, Sf2ModulatorDependency b) {
    return static_cast<Sf2ModulatorDependency>(static_cast<u16>(a) | static_cast<u16>(b));
}

inline Sf2ModulatorDependency& operator|=(Sf2ModulatorDependency& a, Sf2ModulatorDependency b) {
    a = a | b;
    return a;
}

struct Sf2ModulatorIdentity {
    u16 source = 0;
    u16 destination = 0;
    u16 amountSource = 0;
    u16 transform = 0;

    bool operator==(const Sf2ModulatorIdentity& other) const {
        return source == other.source &&
               destination == other.destination &&
               amountSource == other.amountSource &&
               transform == other.transform;
    }
};

struct Sf2ResolvedModulator {
    SFModList mod{};
    Sf2ModulatorLevel level = Sf2ModulatorLevel::InstrumentLocal;
    Sf2ModulatorValidity validity = Sf2ModulatorValidity::Valid;
    bool participatesInDefaultSuppression = false;
    Sf2ModulatorDependency dependencies = Sf2ModulatorDependency::None;
    std::vector<Sf2ResolvedModulator> linkedInputs;
};

struct Sf2ModulatorEvaluation {
    u16 destination = 0;
    i32 amount = 0;
    Sf2ModulatorDependency dependencies = Sf2ModulatorDependency::None;
    Sf2ModulatorDestinationClass destinationClass = Sf2ModulatorDestinationClass::Ignored;
};

struct Sf2ModulatorZone {
    Sf2ModulatorLevel level = Sf2ModulatorLevel::InstrumentLocal;
    const SFModList* mods = nullptr;
    size_t count = 0;
};

bool IsSf2SpecValueGeneratorDestination(u16 destination);
Sf2ModulatorDestinationClass ClassifySf2ModulatorDestination(u16 destination);
bool IsSf2SpecModulatorSourceDefinition(u16 source, bool allowLinkSource);
bool IsSf2SpecModulatorTransform(u16 transform);
Sf2ModulatorIdentity MakeSf2ModulatorIdentity(const SFModList& mod);

std::vector<SFModList> GetSf2ImplicitDefaultModulators();
std::vector<Sf2ResolvedModulator> BuildSf2EffectiveModulators(const std::vector<Sf2ModulatorZone>& zones,
                                                              bool includeImplicitDefaults);
std::vector<Sf2ModulatorEvaluation> EvaluateSf2Modulators(const std::vector<Sf2ResolvedModulator>& modulators,
                                                          u8 key, u16 velocity,
                                                          const ModulatorContext* ctx);

} // namespace XArkMidi
