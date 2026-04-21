/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../common/Types.h"
#include <cstring>

namespace XArkMidi {

// ===== SF2 仕様準拠 POD 構造体 =====

#pragma pack(push, 1)

struct SFPresetHeader {
    char     achPresetName[20];
    u16      wPreset;       // MIDIプログラム番号
    u16      wBank;         // MIDIバンク番号
    u16      wPresetBagNdx; // PBAG インデックス
    u32      dwLibrary;
    u32      dwGenre;
    u32      dwMorphology;
};

struct SFPresetBag {
    u16 wGenNdx; // PGEN インデックス
    u16 wModNdx; // PMOD インデックス
};

struct SFInst {
    char achInstName[20];
    u16  wInstBagNdx; // IBAG インデックス
};

struct SFInstBag {
    u16 wInstGenNdx; // IGEN インデックス
    u16 wInstModNdx; // IMOD インデックス
};

struct SFSample {
    char     achSampleName[20];
    u32      dwStart;           // smpl 内のサンプル開始インデックス（int16_t 単位）
    u32      dwEnd;             // 終端（exclusive）
    u32      dwStartloop;       // ループ開始
    u32      dwEndloop;         // ループ終端（exclusive）
    u32      dwSampleRate;      // 元サンプルレート
    u8       byOriginalPitch;   // MIDI ルートノート
    i8       chPitchCorrection; // セント単位補正 [-99, 99]
    u16      wSampleLink;       // 関連サンプルインデックス（ステレオペア）
    u16      sfSampleType;      // 1=mono,2=right,4=left,8=linked,0x8001=ROM
};

struct SampleHeader {
    char     sampleName[20];
    u32      start = 0;
    u32      end = 0;
    u32      loopStart = 0;
    u32      loopEnd = 0;
    u32      sampleRate = 0;
    u8       originalPitch = 60;
    i8       pitchCorrection = 0;
    u16      sampleLink = 0;
    u16      sampleType = 1;
    f32      loudnessGain = 1.0f;
};

// GenAmountType: ジェネレーター値の共用体
union GenAmountType {
    struct { u8 lo, hi; } ranges; // KeyRange / VelRange
    i16 shAmount;
    u16 wAmount;
};

struct SFGenList {
    u16          sfGenOper;
    GenAmountType genAmount;
};

struct SFModList {
    u16 sfModSrcOper;
    u16 sfModDestOper;
    i16 modAmount;
    u16 sfModAmtSrcOper;
    u16 sfModTransOper;
};

#pragma pack(pop)

// ===== ジェネレーター番号（SF2 spec 8.1.3） =====
enum SFGenerator : u16 {
    GEN_StartAddrsOffset       =  0,
    GEN_EndAddrsOffset         =  1,
    GEN_StartloopAddrsOffset   =  2,
    GEN_EndloopAddrsOffset     =  3,
    GEN_StartAddrsCoarseOffset =  4,
    GEN_ModLfoToPitch          =  5,
    GEN_VibLfoToPitch          =  6,
    GEN_ModEnvToPitch          =  7,
    GEN_InitialFilterFc        =  8,
    GEN_InitialFilterQ         =  9,
    GEN_ModLfoToFilterFc       = 10,
    GEN_ModEnvToFilterFc       = 11,
    GEN_EndAddrsCoarseOffset   = 12,
    GEN_ModLfoToVolume         = 13,
    GEN_ChorusEffectsSend      = 15,
    GEN_ReverbEffectsSend      = 16,
    GEN_Pan                    = 17,
    GEN_DelayModLFO            = 21,
    GEN_FreqModLFO             = 22,
    GEN_DelayVibLFO            = 23,
    GEN_FreqVibLFO             = 24,
    GEN_DelayModEnv            = 25,
    GEN_AttackModEnv           = 26,
    GEN_HoldModEnv             = 27,
    GEN_DecayModEnv            = 28,
    GEN_SustainModEnv          = 29,
    GEN_ReleaseModEnv          = 30,
    GEN_KeynumToModEnvHold     = 31,
    GEN_KeynumToModEnvDecay    = 32,
    GEN_DelayVolEnv            = 33,
    GEN_AttackVolEnv           = 34,
    GEN_HoldVolEnv             = 35,
    GEN_DecayVolEnv            = 36,
    GEN_SustainVolEnv          = 37,
    GEN_ReleaseVolEnv          = 38,
    GEN_KeynumToVolEnvHold     = 39,
    GEN_KeynumToVolEnvDecay    = 40,
    GEN_Instrument             = 41,
    GEN_KeyRange               = 43,
    GEN_VelRange               = 44,
    GEN_StartloopAddrsCoarse   = 45,
    GEN_Keynum                 = 46,
    GEN_Velocity               = 47,
    GEN_InitialAttenuation     = 48,
    GEN_EndloopAddrsCoarse     = 50,
    GEN_CoarseTune             = 51,
    GEN_FineTune               = 52,
    GEN_SampleID               = 53,
    GEN_SampleModes            = 54,
    GEN_ScaleTuning            = 56,
    GEN_ExclusiveClass         = 57,
    GEN_OverridingRootKey      = 58,
    GEN_COUNT                  = 61,
};

// SF2 ジェネレーターデフォルト値テーブル（SF2 spec Table 8.1）
// i32 で保持（GenAmountType と同じビット幅）
inline const i32* GetSF2GeneratorDefaults() {
    static const i32 defaults[GEN_COUNT] = {
        0,       // 0  StartAddrsOffset
        0,       // 1  EndAddrsOffset
        0,       // 2  StartloopAddrsOffset
        0,       // 3  EndloopAddrsOffset
        0,       // 4  StartAddrsCoarseOffset
        0,       // 5  ModLfoToPitch
        0,       // 6  VibLfoToPitch
        0,       // 7  ModEnvToPitch
        13500,   // 8  InitialFilterFc (cents)
        0,       // 9  InitialFilterQ
        0,       // 10 ModLfoToFilterFc
        0,       // 11 ModEnvToFilterFc
        0,       // 12 EndAddrsCoarseOffset
        0,       // 13 ModLfoToVolume
        0,       // 14 (unused)
        0,       // 15 ChorusEffectsSend
        0,       // 16 ReverbEffectsSend
        0,       // 17 Pan
        0,       // 18 (unused)
        0,       // 19 (unused)
        0,       // 20 (unused)
        -12000,  // 21 DelayModLFO
        0,       // 22 FreqModLFO
        -12000,  // 23 DelayVibLFO
        0,       // 24 FreqVibLFO
        -12000,  // 25 DelayModEnv
        -12000,  // 26 AttackModEnv
        -12000,  // 27 HoldModEnv
        -12000,  // 28 DecayModEnv
        0,       // 29 SustainModEnv (centibels)
        -12000,  // 30 ReleaseModEnv
        0,       // 31 KeynumToModEnvHold
        0,       // 32 KeynumToModEnvDecay
        -12000,  // 33 DelayVolEnv
        -12000,  // 34 AttackVolEnv
        -12000,  // 35 HoldVolEnv
        -12000,  // 36 DecayVolEnv
        0,       // 37 SustainVolEnv (centibels)
        -12000,  // 38 ReleaseVolEnv
        0,       // 39 KeynumToVolEnvHold
        0,       // 40 KeynumToVolEnvDecay
        0,       // 41 Instrument
        0,       // 42 (unused)
        0x7F00,  // 43 KeyRange (lo=0, hi=127) as packed u16
        0x7F00,  // 44 VelRange (lo=0, hi=127) as packed u16
        0,       // 45 StartloopAddrsCoarse
        -1,      // 46 Keynum (-1 = not forced)
        -1,      // 47 Velocity (-1 = not forced)
        0,       // 48 InitialAttenuation
        0,       // 49 (unused)
        0,       // 50 EndloopAddrsCoarse
        0,       // 51 CoarseTune
        0,       // 52 FineTune
        0,       // 53 SampleID
        0,       // 54 SampleModes
        0,       // 55 (unused)
        100,     // 56 ScaleTuning (cents/semitone)
        0,       // 57 ExclusiveClass
        -1,      // 58 OverridingRootKey (-1 = use sample's root key)
        0,       // 59 (unused)
        0,       // 60 (unused)
    };
    return defaults;
}

// ゾーン解決済み構造体
// プリセット層 + インスト層のジェネレーターをマージした結果
struct ResolvedZone {
    const SampleHeader* sample; // サンプルヘッダーへのポインタ（非所有）
    i32 generators[GEN_COUNT];  // マージ済みジェネレーター値
    bool noTruncation = false;  // DLS wsmp NO_TRUNCATION
    const i16* sampleDataOverride = nullptr;
    const i32* sampleData24Override = nullptr;
    size_t sampleDataOverrideCount = 0;
    i32 presetBagIndex = -1;
    i32 instrumentBagIndex = -1;
    i32 sampleId = -1;
};

} // namespace XArkMidi

