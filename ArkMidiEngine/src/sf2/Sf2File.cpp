#include "Sf2File.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>
namespace ArkMidi {

// RIFF FourCC ヘルパー
static u32 MakeFourCC(const char* s) {
    return (static_cast<u32>(s[0])      )
         | (static_cast<u32>(s[1]) <<  8)
         | (static_cast<u32>(s[2]) << 16)
         | (static_cast<u32>(s[3]) << 24);
}

namespace {

constexpr u16 kModSrcVelocity = 2u;

double ComputeDefaultVelocityAttenuationCb(u8 velocity) {
    if (velocity >= 127) return 0.0;
    if (velocity <= 0)   return 960.0;
    // SF2 default modulator 1 (square law, FluidSynth/BASSMIDI互換):
    //   amplitude ∝ (vel/127)^2  →  attenuation = 400 * log10(127/vel) centibels
    //   vel=127 → 0cB, vel=64 → ~119cB, vel=1 → ~841cB
    return 400.0 * std::log10(127.0 / static_cast<double>(velocity));
}

bool IsVelocityToInitialAttenuationMod(const SFModList& mod) {
    if (mod.sfModDestOper != GEN_InitialAttenuation || mod.sfModTransOper != 0) {
        return false;
    }
    if ((mod.sfModSrcOper & 0x80u) != 0) { // CC source
        return false;
    }
    const u16 sourceIndex = mod.sfModSrcOper & 0x7Fu;
    return sourceIndex == kModSrcVelocity;
}

double DecodeModSourceValue(u16 oper, u8 key, u8 velocity, const ModulatorContext* ctx, bool& supported) {
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
        case 2: x = static_cast<double>(velocity) / 127.0; break;
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
        x = std::sin(x * (3.14159265358979323846 / 2.0));
        break;
    case 2: // convex
        x = 1.0 - std::cos(x * (3.14159265358979323846 / 2.0));
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

void AddPitchCents(i32* generators, i32 cents) {
    if (cents == 0) return;
    generators[GEN_FineTune] += cents;
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
        return std::clamp(value, 0, 32767);
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

void ApplyModulatorDelta(ResolvedZone& zone, u16 dest, i32 delta) {
    switch (dest) {
    case GEN_InitialFilterFc:
        zone.generators[GEN_InitialFilterFc] =
            ClampGeneratorValue(GEN_InitialFilterFc, zone.generators[GEN_InitialFilterFc] + delta);
        break;
    case GEN_ModEnvToFilterFc:
        zone.generators[GEN_ModEnvToFilterFc] =
            ClampGeneratorValue(GEN_ModEnvToFilterFc, zone.generators[GEN_ModEnvToFilterFc] + delta);
        break;
    case GEN_InitialAttenuation:
        zone.generators[GEN_InitialAttenuation] =
            ClampGeneratorValue(GEN_InitialAttenuation, zone.generators[GEN_InitialAttenuation] + delta);
        break;
    case GEN_Pan:
        zone.generators[GEN_Pan] =
            ClampGeneratorValue(GEN_Pan, zone.generators[GEN_Pan] + delta);
        break;
    case GEN_CoarseTune:
    case GEN_FineTune:
    case GEN_ModLfoToPitch:
    case GEN_VibLfoToPitch:
    case GEN_ModEnvToPitch:
        AddPitchCents(zone.generators, delta);
        zone.generators[GEN_CoarseTune] = ClampGeneratorValue(GEN_CoarseTune, zone.generators[GEN_CoarseTune]);
        zone.generators[GEN_FineTune] = ClampGeneratorValue(GEN_FineTune, zone.generators[GEN_FineTune]);
        break;
    case GEN_DelayVolEnv:
    case GEN_AttackVolEnv:
    case GEN_HoldVolEnv:
    case GEN_DecayVolEnv:
    case GEN_ReleaseVolEnv:
    case GEN_SustainVolEnv:
        zone.generators[dest] = ClampGeneratorValue(dest, zone.generators[dest] + delta);
        break;
    default:
        break;
    }
}

} // namespace

bool Sf2File::LoadFromMemory(const u8* data, size_t size) {
    sampleData_.clear();
    presets_.clear(); presetBags_.clear(); presetGens_.clear(); presetMods_.clear();
    instruments_.clear(); instBags_.clear(); instGens_.clear(); instMods_.clear();
    samples_.clear();
    sampleHeaders_.clear();
    presetIndexMap_.clear();
    errorMsg_.clear();

    try {
        BinaryReader r(data, size);
        if (!ParseRiff(r)) return false;
        BuildPresetIndex();
        ComputeSampleLoudnessGains();
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
    while (r.Tell() < end && !r.IsEof()) {
        u32 chunkId   = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();

        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto listData = r.ReadSlice(chunkSize - 4);

            if (listType == MakeFourCC("sdta")) {
                if (!ParseSdta(listData, chunkSize - 4)) return false;
            }
            else if (listType == MakeFourCC("pdta")) {
                if (!ParsePdta(listData, chunkSize - 4)) return false;
            }
            // INFO チャンクは無視
        }
        else {
            r.Skip(chunkSize);
            // 奇数サイズのパディング
            if (chunkSize & 1) r.Skip(1);
        }
    }
    return true;
}

bool Sf2File::ParseSdta(BinaryReader& r, u32 /*chunkSize*/) {
    while (!r.IsEof()) {
        if (r.Remaining() < 8) break;
        u32 subId   = r.ReadU32LE();
        u32 subSize = r.ReadU32LE();

        if (subId == MakeFourCC("smpl")) {
            // サイズ検証: subSize が実際の残りデータを超えていないか確認
            size_t available = r.Remaining();
            if (static_cast<size_t>(subSize) > available) {
                // 壊れたチャンクサイズ: 実際に読めるサイズに切り詰める
                subSize = static_cast<u32>(available & ~1u); // 偶数に揃える
            }
            size_t count = subSize / 2;
            sampleData_.resize(count);
            if (count > 0) {
                // バルクコピー（1サンプルずつ読むと大きなSF2でハングする）
                // x86/x64 はリトルエンディアンなので i16 は直接コピー可能
                std::memcpy(sampleData_.data(), r.CurrentPtr(), count * sizeof(i16));
                r.Skip(subSize);
            }
        }
        else {
            // sm24 等はスキップ（範囲チェック付き）
            size_t skipSize = std::min(static_cast<size_t>(subSize), r.Remaining());
            r.Skip(skipSize);
            if ((subSize & 1) && !r.IsEof()) r.Skip(1);
        }
    }
    return true;
}

bool Sf2File::ParsePdta(BinaryReader& r, u32 /*chunkSize*/) {
    while (!r.IsEof()) {
        if (r.Remaining() < 8) break;
        u32 subId   = r.ReadU32LE();
        u32 subSize = r.ReadU32LE();
        auto sub = r.ReadSlice(subSize);
        if (subSize & 1 && !r.IsEof()) r.Skip(1);

        if      (subId == MakeFourCC("phdr")) ParsePhdr(sub, subSize);
        else if (subId == MakeFourCC("pbag")) ParsePbag(sub, subSize);
        else if (subId == MakeFourCC("pmod")) ParsePmod(sub, subSize);
        else if (subId == MakeFourCC("pgen")) ParsePgen(sub, subSize);
        else if (subId == MakeFourCC("inst")) ParseInst(sub, subSize);
        else if (subId == MakeFourCC("ibag")) ParseIbag(sub, subSize);
        else if (subId == MakeFourCC("imod")) ParseImod(sub, subSize);
        else if (subId == MakeFourCC("igen")) ParseIgen(sub, subSize);
        else if (subId == MakeFourCC("shdr")) ParseShdr(sub, subSize);
    }
    return true;
}

void Sf2File::ParsePhdr(BinaryReader& r, u32 size) {
    u32 count = size / 38; // sizeof(SFPresetHeader) = 38
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
    u32 count = size / 4;
    presetBags_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        presetBags_[i].wGenNdx = r.ReadU16LE();
        presetBags_[i].wModNdx = r.ReadU16LE();
    }
}

void Sf2File::ParsePmod(BinaryReader& r, u32 size) {
    u32 count = size / 10;
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
    u32 count = size / 4;
    presetGens_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        presetGens_[i].sfGenOper          = r.ReadU16LE();
        presetGens_[i].genAmount.wAmount  = r.ReadU16LE();
    }
}

void Sf2File::ParseInst(BinaryReader& r, u32 size) {
    u32 count = size / 22;
    instruments_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        for (int c = 0; c < 20; ++c) instruments_[i].achInstName[c] = static_cast<char>(r.ReadU8());
        instruments_[i].wInstBagNdx = r.ReadU16LE();
    }
}

void Sf2File::ParseIbag(BinaryReader& r, u32 size) {
    u32 count = size / 4;
    instBags_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        instBags_[i].wInstGenNdx = r.ReadU16LE();
        instBags_[i].wInstModNdx = r.ReadU16LE();
    }
}

void Sf2File::ParseImod(BinaryReader& r, u32 size) {
    u32 count = size / 10;
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
    u32 count = size / 4;
    instGens_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        instGens_[i].sfGenOper         = r.ReadU16LE();
        instGens_[i].genAmount.wAmount = r.ReadU16LE();
    }
}

void Sf2File::ParseShdr(BinaryReader& r, u32 size) {
    u32 count = size / 46;
    samples_.resize(count);
    sampleHeaders_.resize(count);
    for (u32 i = 0; i < count; ++i) {
        auto& s = samples_[i];
        for (int c = 0; c < 20; ++c) s.achSampleName[c] = static_cast<char>(r.ReadU8());
        s.dwStart           = r.ReadU32LE();
        s.dwEnd             = r.ReadU32LE();
        s.dwStartloop       = r.ReadU32LE();
        s.dwEndloop         = r.ReadU32LE();
        s.dwSampleRate      = r.ReadU32LE();
        s.byOriginalPitch   = r.ReadU8();
        s.chPitchCorrection = static_cast<i8>(r.ReadU8());
        s.wSampleLink       = r.ReadU16LE();
        s.sfSampleType      = r.ReadU16LE();
        for (int c = 0; c < 20; ++c) sampleHeaders_[i].sampleName[c] = s.achSampleName[c];
        sampleHeaders_[i].start = s.dwStart;
        sampleHeaders_[i].end = s.dwEnd;
        sampleHeaders_[i].loopStart = s.dwStartloop;
        sampleHeaders_[i].loopEnd = s.dwEndloop;
        sampleHeaders_[i].sampleRate = s.dwSampleRate;
        sampleHeaders_[i].originalPitch = s.byOriginalPitch;
        sampleHeaders_[i].pitchCorrection = s.chPitchCorrection;
        sampleHeaders_[i].sampleType = s.sfSampleType;
    }
}

void Sf2File::ComputeSampleLoudnessGains() {
    // SF2仕様にはサンプル間ラウドネス正規化は存在しないため何もしない。
    // normCompensation_ = 1.0f, 各 loudnessGain = 1.0f のまま（デフォルト値）。
    normCompensation_ = 1.0f;
}

// ジェネレーターマージ（インストグローバル→インストゾーン→プリセットゾーン）
void Sf2File::ResolveZone(int globalPresetBagIdx, int globalInstBagIdx, int instBagIdx, int presetBagIdx,
                           const SampleHeader* sample, u8 key, u8 velocity,
                           const ModulatorContext* ctx,
                           ResolvedZone& outZone) const {
    outZone.sample = sample;
    const i32* defaults = GetSF2GeneratorDefaults();
    constexpr i32 kUnset = std::numeric_limits<i32>::min();

    // Step 1: デフォルト値で初期化
    for (int g = 0; g < GEN_COUNT; ++g)
        outZone.generators[g] = ClampGeneratorValue(static_cast<u16>(g), defaults[g]);

    auto clearLayer = [&](i32* layer) {
        for (int g = 0; g < GEN_COUNT; ++g) {
            layer[g] = kUnset;
        }
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
            if (oper >= GEN_COUNT) continue;
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

    bool hasVelocityToAttenuationMod = false;
    if (globalInstBagIdx >= 0 && globalInstBagIdx + 1 < static_cast<int>(instBags_.size())) {
        hasVelocityToAttenuationMod |= ApplyModulators(
            instMods_,
            instBags_[globalInstBagIdx].wInstModNdx,
            instBags_[globalInstBagIdx + 1].wInstModNdx,
            key, velocity, ctx, outZone);
    }
    if (instBagIdx >= 0 && instBagIdx + 1 < static_cast<int>(instBags_.size())) {
        hasVelocityToAttenuationMod |= ApplyModulators(
            instMods_,
            instBags_[instBagIdx].wInstModNdx,
            instBags_[instBagIdx + 1].wInstModNdx,
            key, velocity, ctx, outZone);
    }
    if (globalPresetBagIdx >= 0 && globalPresetBagIdx + 1 < static_cast<int>(presetBags_.size())) {
        hasVelocityToAttenuationMod |= ApplyModulators(
            presetMods_,
            presetBags_[globalPresetBagIdx].wModNdx,
            presetBags_[globalPresetBagIdx + 1].wModNdx,
            key, velocity, ctx, outZone);
    }
    if (presetBagIdx >= 0 && presetBagIdx + 1 < static_cast<int>(presetBags_.size())) {
        hasVelocityToAttenuationMod |= ApplyModulators(
            presetMods_,
            presetBags_[presetBagIdx].wModNdx,
            presetBags_[presetBagIdx + 1].wModNdx,
            key, velocity, ctx, outZone);
    }

    if (!hasVelocityToAttenuationMod) {
        const i32 delta = static_cast<i32>(std::lround(ComputeDefaultVelocityAttenuationCb(velocity)));
        outZone.generators[GEN_InitialAttenuation] =
            ClampGeneratorValue(GEN_InitialAttenuation, outZone.generators[GEN_InitialAttenuation] + delta);
    }
}

bool Sf2File::ApplyModulators(const std::vector<SFModList>& mods, int modStart, int modEnd,
                              u8 key, u8 velocity, const ModulatorContext* ctx, ResolvedZone& zone) const {
    const int modMax = static_cast<int>(mods.size());
    if (modStart < 0 || modStart > modMax) modStart = modMax;
    if (modEnd > modMax) modEnd = modMax;
    bool hasVelocityToAttenuationMod = false;

    for (int i = modStart; i < modEnd; ++i) {
        const auto& mod = mods[i];
        if (IsVelocityToInitialAttenuationMod(mod)) {
            hasVelocityToAttenuationMod = true;
        }
        if (mod.sfModDestOper >= GEN_COUNT) continue;
        if (mod.sfModTransOper != 0) continue;

        bool sourceSupported = false;
        const double source = DecodeModSourceValue(mod.sfModSrcOper, key, velocity, ctx, sourceSupported);
        if (!sourceSupported) continue;

        bool amountSupported = false;
        const double amountSource = DecodeModSourceValue(mod.sfModAmtSrcOper, key, velocity, ctx, amountSupported);
        const double amountScale = amountSupported ? amountSource : 1.0;
        i32 delta = static_cast<i32>(std::lround(static_cast<double>(mod.modAmount) * source * amountScale));
        if (delta == 0) continue;

        ApplyModulatorDelta(zone, mod.sfModDestOper, delta);
    }
    return hasVelocityToAttenuationMod;
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

bool Sf2File::FindZones(u16 bank, u8 program, u8 key, u8 velocity,
                         std::vector<ResolvedZone>& outZones,
                         const ModulatorContext* ctx) const {
    outZones.clear();

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
        if (pbagStart < pbagEnd && pbagStart + 1 < pbagMax) {
            int gs = presetBags_[pbagStart].wGenNdx;
            int ge = presetBags_[pbagStart + 1].wGenNdx;
            bool hasInstrument = false;
            for (int pg = gs; pg < ge && pg < static_cast<int>(presetGens_.size()); ++pg) {
                if (presetGens_[pg].sfGenOper == GEN_Instrument) { hasInstrument = true; break; }
            }
            if (!hasInstrument) globalPresetBag = pbagStart;
        }

        for (int pb = pbagStart; pb < pbagEnd; ++pb) {
            // プリセットゾーンのキーレンジ・ベロシティレンジを確認
            int pgenStart = presetBags_[pb].wGenNdx;
            int pgenEnd   = (pb + 1 < static_cast<int>(presetBags_.size()))
                            ? presetBags_[pb + 1].wGenNdx : static_cast<int>(presetGens_.size());

            // pgen 境界チェック
            int pgenMax = static_cast<int>(presetGens_.size());
            if (pgenStart < 0 || pgenStart > pgenMax) pgenStart = pgenMax;
            if (pgenEnd   > pgenMax) pgenEnd = pgenMax;

            u8 pkeyLo = 0, pkeyHi = 127, pvelLo = 0, pvelHi = 127;
            int instrumentIdx = -1;

            for (int pg = pgenStart; pg < pgenEnd; ++pg) {
                u16 oper = presetGens_[pg].sfGenOper;
                if (oper == GEN_KeyRange) {
                    pkeyLo = presetGens_[pg].genAmount.ranges.lo;
                    pkeyHi = presetGens_[pg].genAmount.ranges.hi;
                }
                else if (oper == GEN_VelRange) {
                    pvelLo = presetGens_[pg].genAmount.ranges.lo;
                    pvelHi = presetGens_[pg].genAmount.ranges.hi;
                }
                else if (oper == GEN_Instrument) {
                    instrumentIdx = presetGens_[pg].genAmount.wAmount;
                }
            }

            if (key < pkeyLo || key > pkeyHi) continue;
            if (velocity < pvelLo || velocity > pvelHi) continue;
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
            if (ibagStart < ibagEnd && ibagStart + 1 < ibagMax) {
                int gs = instBags_[ibagStart].wInstGenNdx;
                int ge = instBags_[ibagStart + 1].wInstGenNdx;
                bool hasSampleId = false;
                for (int ig = gs; ig < ge && ig < static_cast<int>(instGens_.size()); ++ig) {
                    if (instGens_[ig].sfGenOper == GEN_SampleID) { hasSampleId = true; break; }
                }
                if (!hasSampleId) globalInstBag = ibagStart;
            }

            for (int ib = ibagStart; ib < ibagEnd; ++ib) {
                int igenStart = instBags_[ib].wInstGenNdx;
                int igenEnd   = (ib + 1 < static_cast<int>(instBags_.size()))
                                ? instBags_[ib + 1].wInstGenNdx : static_cast<int>(instGens_.size());

                // igen 境界チェック
                int igenMax = static_cast<int>(instGens_.size());
                if (igenStart < 0 || igenStart > igenMax) igenStart = igenMax;
                if (igenEnd   > igenMax) igenEnd = igenMax;

                u8  ikeyLo = 0, ikeyHi = 127, ivelLo = 0, ivelHi = 127;
                int sampleIdx = -1;

                for (int ig = igenStart; ig < igenEnd; ++ig) {
                    u16 oper = instGens_[ig].sfGenOper;
                    if (oper == GEN_KeyRange) {
                        ikeyLo = instGens_[ig].genAmount.ranges.lo;
                        ikeyHi = instGens_[ig].genAmount.ranges.hi;
                    }
                    else if (oper == GEN_VelRange) {
                        ivelLo = instGens_[ig].genAmount.ranges.lo;
                        ivelHi = instGens_[ig].genAmount.ranges.hi;
                    }
                    else if (oper == GEN_SampleID) {
                        sampleIdx = instGens_[ig].genAmount.wAmount;
                    }
                }

                if (key < ikeyLo || key > ikeyHi) continue;
                if (velocity < ivelLo || velocity > ivelHi) continue;
                if (sampleIdx < 0 || sampleIdx >= static_cast<int>(samples_.size())) continue;

                const SFSample* smp = &samples_[sampleIdx];
                // ROM サンプルはスキップ
                if (smp->sfSampleType & 0x8000) continue;

                ResolvedZone zone;
                ResolveZone(globalPresetBag, globalInstBag, ib, pb, &sampleHeaders_[sampleIdx], key, velocity, ctx, zone);
                // SampleID を確定値で設定
                zone.generators[GEN_SampleID] = sampleIdx;
                outZones.push_back(zone);
            }
        }
    }
    return !outZones.empty();
}

} // namespace ArkMidi
