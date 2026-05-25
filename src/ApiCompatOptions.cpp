/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "ApiCompatOptions.h"
#include <cstddef>

namespace XArkMidi {
namespace {

bool HasCreateOptionField(const XAmeCreateOptions* options, size_t offset, size_t fieldSize) {
    return options && options->structSize >= offset + fieldSize;
}

} // namespace

SynthCompatOptions ResolveCompatOptionsForCreateOptions(const XAmeCreateOptions* options) {
    SynthCompatOptions compatOptions;
    XAmeCompatibilityMode compatMode = XAME_COMPAT_MODE_ENGINE_DEFAULT;
    if (HasCreateOptionField(options, offsetof(XAmeCreateOptions, compatibilityFlags), sizeof(options->compatibilityFlags))) {
        const u32 flags = options->compatibilityFlags;
        compatOptions.sf2ZeroLengthLoopRetrigger =
            (flags & XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER) != 0;
        compatOptions.enableSf2SamplePitchCorrection =
            (flags & XAME_COMPAT_ENABLE_SF2_SAMPLE_PITCH_CORRECTION) != 0;
        compatOptions.multiplySf2MidiEffectsSends =
            (flags & XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS) != 0;
        compatOptions.applySf2ChannelDefaults =
            (flags & XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS) != 0;
        compatOptions.enableEnhancedOutputStage =
            (flags & XAME_COMPAT_ENABLE_ENHANCED_OUTPUT_STAGE) != 0;
        compatOptions.useNaturalOutputStage =
            (flags & XAME_COMPAT_ENHANCED_OUTPUT_STAGE_NATURAL) != 0;
        compatOptions.useWarmOutputStage =
            (flags & XAME_COMPAT_ENHANCED_OUTPUT_STAGE_WARM) != 0;
        compatOptions.disableInternalEffects =
            (flags & XAME_COMPAT_DISABLE_INTERNAL_EFFECTS) != 0;
        compatOptions.useSf2SpecModulatorResolver =
            (flags & XAME_COMPAT_USE_SF2_SPEC_MODULATOR_RESOLVER) != 0;
    }

    if (HasCreateOptionField(options, offsetof(XAmeCreateOptions, compatibilityMode), sizeof(options->compatibilityMode))) {
        compatMode = static_cast<XAmeCompatibilityMode>(options->compatibilityMode);
    }

    switch (compatMode) {
    case XAME_COMPAT_MODE_SF2_LEGACY:
        compatOptions.useSf2SpecModulatorResolver = false;
        break;
    case XAME_COMPAT_MODE_SF2_SPEC_204:
        compatOptions.useSf2SpecModulatorResolver = true;
        // Deprecated legacy default-modulator flag is intentionally ignored in explicit SF2 spec mode.
        compatOptions.applySf2ChannelDefaults = false;
        break;
    case XAME_COMPAT_MODE_ENGINE_DEFAULT:
    default:
        break;
    }

    if (compatOptions.useSf2SpecModulatorResolver) {
        // Spec resolver owns implicit SF2 defaults. Keep legacy injection disabled.
        compatOptions.applySf2ChannelDefaults = false;
    }

    return compatOptions;
}

} // namespace XArkMidi

