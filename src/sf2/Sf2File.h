/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "Sf2Types.h"
#include "../common/BinaryReader.h"
#include "../common/FileUtil.h"
#include "../soundbank/SoundBank.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace XArkMidi {

class Sf2File : public SoundBank {
public:
    SoundBankKind Kind() const override { return SoundBankKind::Sf2; }
    bool LoadFromMemory(const u8* data, size_t size);
    bool LoadFromFile(const std::wstring& path);
    void SetResourceLimits(size_t maxSampleDataBytes, u32 maxPdtaEntries);

    // プリセット検索: bank/program/key/velocity に一致する ResolvedZone リストを返す
    bool FindZones(u16 bank, u8 program, u8 key, u16 velocity,
                   std::vector<ResolvedZone>& outZones,
                   const ModulatorContext* ctx = nullptr) const override;

    // サンプルデータへのアクセス（smpl チャンクの i16 配列）
    const i16*  SampleData()      const override { return sampleData_.data(); }
    size_t      SampleDataCount() const override { return sampleData_.size(); }

    const std::string& ErrorMessage() const override { return errorMsg_; }

    f32 GetLoudnessNormCompensation() const override { return normCompensation_; }

    size_t PresetCount() const { return presets_.size(); }
    size_t InstrumentCount() const { return instruments_.size(); }
    size_t SampleHeaderCount() const { return sampleHeaders_.size(); }
    const SFPresetHeader* Preset(size_t idx) const { return idx < presets_.size() ? &presets_[idx] : nullptr; }
    const SampleHeader* SampleHeaders(size_t idx) const { return idx < sampleHeaders_.size() ? &sampleHeaders_[idx] : nullptr; }

    bool GetPresetBagIndices(u16 bank, u8 program, int& outGlobalBagIdx, int& outLocalBagIdx) const;
    bool GetInstrumentBagIndices(int instrumentIdx, int localBagIdx, int& outGlobalBagIdx) const;
    void GetGeneratorLayer(int genStart, int genEnd, i32 outGens[GEN_COUNT]) const;
    void GetPresetGeneratorLayer(int bagIdx, i32 outGens[GEN_COUNT]) const;
    void GetInstrumentGeneratorLayer(int bagIdx, i32 outGens[GEN_COUNT]) const;
    int GetInstrumentIndex(int presetInstrumentGenValue) const;

    struct ZoneInfo {
        int bagIndex;
        int keyLo, keyHi;
        int velLo, velHi;
        int sampleId;
        i32 generators[GEN_COUNT];
    };
    bool GetInstrumentLocalZones(int instrumentIdx, std::vector<ZoneInfo>& outZones) const;

private:
    std::vector<i16>           sampleData_;
    std::vector<SFPresetHeader> presets_;
    std::vector<SFPresetBag>    presetBags_;
    std::vector<SFGenList>      presetGens_;
    std::vector<SFModList>      presetMods_;
    std::vector<SFInst>         instruments_;
    std::vector<SFInstBag>      instBags_;
    std::vector<SFGenList>      instGens_;
    std::vector<SFModList>      instMods_;
    std::vector<SFSample>       samples_;
    std::vector<SampleHeader>   sampleHeaders_;
    std::unordered_map<u32, std::vector<int>> presetIndexMap_;

    std::string errorMsg_;

    bool ParseRiff(BinaryReader& r);
    bool ParseSdta(BinaryReader& r, u32 chunkSize);
    bool ParsePdta(BinaryReader& r, u32 chunkSize);

    // pdta サブチャンク
    void ParsePhdr(BinaryReader& r, u32 size);
    void ParsePbag(BinaryReader& r, u32 size);
    void ParsePmod(BinaryReader& r, u32 size);
    void ParsePgen(BinaryReader& r, u32 size);
    void ParseInst(BinaryReader& r, u32 size);
    void ParseIbag(BinaryReader& r, u32 size);
    void ParseImod(BinaryReader& r, u32 size);
    void ParseIgen(BinaryReader& r, u32 size);
    void ParseShdr(BinaryReader& r, u32 size);

    // ジェネレーターマージ（インストグローバル→インストゾーン→プリセットゾーン）
    void ResolveZone(int globalPresetBagIdx, int globalInstBagIdx, int instBagIdx, int presetBagIdx,
                     const SampleHeader* sample, u8 key, u16 velocity,
                     const ModulatorContext* ctx,
                     ResolvedZone& outZone) const;

    bool ApplyModulators(const std::vector<SFModList>& mods, int modStart, int modEnd,
                         u8 key, u16 velocity, const ModulatorContext* ctx, ResolvedZone& zone) const;
    void BuildPresetIndex();
    void ComputeSampleLoudnessGains();
    bool ValidateSampleHeaders();

    f32 normCompensation_ = 1.0f;
    size_t maxSampleDataBytes_ = 512ull * 1024ull * 1024ull;
    u32 maxPdtaEntries_ = 1u << 20;
};

} // namespace XArkMidi

