/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/BinaryReader.h"
#include "../common/FileUtil.h"
#include "../soundbank/SoundBank.h"
#include <vector>
#include <string>

namespace XArkMidi {

class DlsFile : public SoundBank {
public:
    SoundBankKind Kind() const override { return SoundBankKind::Dls; }

    bool LoadFromMemory(const u8* data, size_t size) override;
    bool LoadFromFile(const std::wstring& path);
    void SetResourceLimits(size_t maxSampleDataBytes, u32 maxPoolTableEntries);

    bool FindZones(u16 bank, u8 program, u8 key, u16 velocity,
                   std::vector<ResolvedZone>& outZones,
                   const ModulatorContext* ctx = nullptr) const override;

    const i16* SampleData() const override { return sampleData_.data(); }
    size_t SampleDataCount() const override { return sampleData_.size(); }
    const std::string& ErrorMessage() const override { return errorMsg_; }

private:
    struct DlsConnection {
        u16 source = 0;
        u16 control = 0;
        u16 destination = 0;
        u16 transform = 0;
        i32 scale = 0;
    };

    struct DlsRegion {
        u16 keyLow = 0;
        u16 keyHigh = 127;
        u16 velLow = 0;
        u16 velHigh = 127;
        u16 keyGroup = 0;
        u32 waveIndex = 0;
        bool hasWsmp = false;
        u16 unityNote = 60;
        i16 fineTune = 0;
        i32 attenuation = 0;
        bool noTruncation = false;
        bool looping = false;
        u32 loopStart = 0;
        u32 loopLength = 0;
        std::vector<i32> articulators;
        std::vector<DlsConnection> connections;
    };

    struct DlsInstrument {
        u32 bank = 0;
        u32 program = 0;
        std::vector<DlsRegion> regions;
        std::vector<i32> articulators;
        std::vector<DlsConnection> connections;
    };

    struct DlsWave {
        SampleHeader sample;
        u32 poolOffset = 0;
        bool hasWsmp = false;
        bool noTruncation = false;
    };

    std::vector<i16>           sampleData_;
    std::vector<DlsWave>       waves_;
    std::vector<DlsInstrument> instruments_;
    std::vector<u32>           poolTableOffsets_;
    std::string                errorMsg_;
    bool                       halveSampleRates_ = false;
    size_t                     maxSampleDataBytes_ = 512ull * 1024ull * 1024ull;
    u32                        maxPoolTableEntries_ = 1u << 20;

    bool ParseRiff(BinaryReader& r);
    bool ParseCollection(BinaryReader& r, u32 chunkSize);
    bool ParsePoolTable(BinaryReader& r, u32 chunkSize);
    bool ParseWavePool(BinaryReader& r, u32 chunkSize);
    bool ParseInstruments(BinaryReader& r, u32 chunkSize);
    bool ParseWaveList(BinaryReader& r, u32 chunkSize, DlsWave& outWave);
    bool ParseInstrumentList(BinaryReader& r, u32 chunkSize, DlsInstrument& outInstrument);
    bool ParseRegionList(BinaryReader& r, u32 chunkSize, DlsRegion& outRegion);
    bool ParseArticulatorList(BinaryReader& r, std::vector<i32>& outGenerators,
                             std::vector<DlsConnection>& outConnections);

    void ApplyRegionToZone(const DlsRegion& region, const DlsWave& wave, ResolvedZone& outZone) const;
};

} // namespace XArkMidi


