/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/Types.h"
#include "../sf2/Sf2Types.h"
#include <string>
#include <vector>

namespace XArkMidi {

enum class SoundBankKind : u32 {
    Auto = 0,
    Sf2  = 1,
    Dls  = 2,
};

struct ModulatorContext {
    u8 ccValues[128] = {};
    u8 channelPressure = 0;
    u8 polyPressure[128] = {};
    i16 pitchBend = 0;
    u8 pitchWheelSensitivitySemitones = 2;
    u8 pitchWheelSensitivityCents = 0;
};

class SoundBank {
public:
    virtual ~SoundBank() = default;

    virtual SoundBankKind Kind() const = 0;

    virtual bool LoadFromMemory(const u8* data, size_t size) = 0;
    virtual bool FindZones(u16 bank, u8 program, u8 key, u16 velocity,
                           std::vector<ResolvedZone>& outZones,
                           const ModulatorContext* ctx = nullptr) const = 0;

    virtual const i16* SampleData() const = 0;
    virtual size_t SampleDataCount() const = 0;
    virtual const std::string& ErrorMessage() const = 0;

    // SF2ロード時のサンプルPCM正規化により全体が下がった分の補正スケール
    // Synthesizer が Init 時にマスターゲインへ乗算する
    virtual f32 GetLoudnessNormCompensation() const { return 1.0f; }
};

} // namespace XArkMidi


