#include "../src/sf2/Sf2File.h"
#include "../src/sf2/Sf2Types.h"
#include "../src/synth/Interpolator.h"
#include "../src/synth/Voice.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace XArkMidi;

namespace {

struct MinimalSf2Config {
    std::vector<SFGenList> presetGens;
    std::vector<SFModList> presetMods;
    std::vector<SFGenList> instGens;
    std::vector<SFModList> instMods;
};

void AppendU16LE(std::vector<u8>& out, u16 value) {
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
}

void AppendU32LE(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value & 0xFFu));
    out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
    out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
}

void AppendI16LE(std::vector<u8>& out, i16 value) {
    AppendU16LE(out, static_cast<u16>(value));
}

void AppendChunk(std::vector<u8>& out, const char id[4], const std::vector<u8>& data) {
    out.insert(out.end(), id, id + 4);
    AppendU32LE(out, static_cast<u32>(data.size()));
    out.insert(out.end(), data.begin(), data.end());
    if ((data.size() & 1u) != 0u) {
        out.push_back(0);
    }
}

void AppendListChunk(std::vector<u8>& out, const char type[4], const std::vector<u8>& payload) {
    std::vector<u8> data;
    data.insert(data.end(), type, type + 4);
    data.insert(data.end(), payload.begin(), payload.end());
    AppendChunk(out, "LIST", data);
}

std::vector<u8> BuildMinimalSf2(const MinimalSf2Config& config) {
    std::vector<u8> infoPayload;

    std::vector<u8> sdtaPayload;
    std::vector<u8> smpl;
    for (int i = 0; i < 64; ++i) {
        const double phase = static_cast<double>(i) / 64.0;
        const i16 sample = static_cast<i16>(std::lround(std::sin(phase * 6.283185307179586) * 12000.0));
        AppendI16LE(smpl, sample);
    }
    AppendChunk(sdtaPayload, "smpl", smpl);

    std::vector<u8> pdtaPayload;

    std::vector<u8> phdr;
    auto appendPresetHeader = [&](const char* name, u16 preset, u16 bank, u16 bagIndex) {
        char padded[20] = {};
        std::strncpy(padded, name, sizeof(padded));
        phdr.insert(phdr.end(), padded, padded + 20);
        AppendU16LE(phdr, preset);
        AppendU16LE(phdr, bank);
        AppendU16LE(phdr, bagIndex);
        AppendU32LE(phdr, 0);
        AppendU32LE(phdr, 0);
        AppendU32LE(phdr, 0);
    };
    appendPresetHeader("Preset", 0, 0, 0);
    appendPresetHeader("EOP", 0, 0, 1);
    AppendChunk(pdtaPayload, "phdr", phdr);

    std::vector<u8> pbag;
    AppendU16LE(pbag, 0);
    AppendU16LE(pbag, 0);
    AppendU16LE(pbag, static_cast<u16>(config.presetGens.size() + 1));
    AppendU16LE(pbag, static_cast<u16>(config.presetMods.size()));
    AppendChunk(pdtaPayload, "pbag", pbag);

    std::vector<u8> pmod;
    for (const auto& mod : config.presetMods) {
        AppendU16LE(pmod, mod.sfModSrcOper);
        AppendU16LE(pmod, mod.sfModDestOper);
        AppendI16LE(pmod, mod.modAmount);
        AppendU16LE(pmod, mod.sfModAmtSrcOper);
        AppendU16LE(pmod, mod.sfModTransOper);
    }
    AppendChunk(pdtaPayload, "pmod", pmod);

    std::vector<u8> pgen;
    auto appendGen = [](std::vector<u8>& bytes, const SFGenList& gen) {
        AppendU16LE(bytes, gen.sfGenOper);
        AppendU16LE(bytes, gen.genAmount.wAmount);
    };
    for (const auto& gen : config.presetGens) {
        appendGen(pgen, gen);
    }
    SFGenList instrumentGen{};
    instrumentGen.sfGenOper = GEN_Instrument;
    instrumentGen.genAmount.wAmount = 0;
    appendGen(pgen, instrumentGen);
    AppendChunk(pdtaPayload, "pgen", pgen);

    std::vector<u8> inst;
    auto appendInst = [&](const char* name, u16 bagIndex) {
        char padded[20] = {};
        std::strncpy(padded, name, sizeof(padded));
        inst.insert(inst.end(), padded, padded + 20);
        AppendU16LE(inst, bagIndex);
    };
    appendInst("Inst", 0);
    appendInst("EOI", 1);
    AppendChunk(pdtaPayload, "inst", inst);

    std::vector<u8> ibag;
    AppendU16LE(ibag, 0);
    AppendU16LE(ibag, 0);
    AppendU16LE(ibag, static_cast<u16>(config.instGens.size() + 1));
    AppendU16LE(ibag, static_cast<u16>(config.instMods.size()));
    AppendChunk(pdtaPayload, "ibag", ibag);

    std::vector<u8> imod;
    for (const auto& mod : config.instMods) {
        AppendU16LE(imod, mod.sfModSrcOper);
        AppendU16LE(imod, mod.sfModDestOper);
        AppendI16LE(imod, mod.modAmount);
        AppendU16LE(imod, mod.sfModAmtSrcOper);
        AppendU16LE(imod, mod.sfModTransOper);
    }
    AppendChunk(pdtaPayload, "imod", imod);

    std::vector<u8> igen;
    for (const auto& gen : config.instGens) {
        appendGen(igen, gen);
    }
    SFGenList sampleGen{};
    sampleGen.sfGenOper = GEN_SampleID;
    sampleGen.genAmount.wAmount = 0;
    appendGen(igen, sampleGen);
    AppendChunk(pdtaPayload, "igen", igen);

    std::vector<u8> shdr;
    auto appendSampleHeader = [&](const char* name, u32 start, u32 end, u32 loopStart, u32 loopEnd,
                                  u32 sampleRate, u8 originalPitch, i8 pitchCorrection, u16 sampleType) {
        char padded[20] = {};
        std::strncpy(padded, name, sizeof(padded));
        shdr.insert(shdr.end(), padded, padded + 20);
        AppendU32LE(shdr, start);
        AppendU32LE(shdr, end);
        AppendU32LE(shdr, loopStart);
        AppendU32LE(shdr, loopEnd);
        AppendU32LE(shdr, sampleRate);
        shdr.push_back(originalPitch);
        shdr.push_back(static_cast<u8>(pitchCorrection));
        AppendU16LE(shdr, 0);
        AppendU16LE(shdr, sampleType);
    };
    appendSampleHeader("Sample", 0, 64, 8, 56, 44100, 60, 0, 1);
    appendSampleHeader("EOS", 0, 0, 0, 0, 44100, 0, 0, 1);
    AppendChunk(pdtaPayload, "shdr", shdr);

    std::vector<u8> riffPayload;
    AppendListChunk(riffPayload, "INFO", infoPayload);
    AppendListChunk(riffPayload, "sdta", sdtaPayload);
    AppendListChunk(riffPayload, "pdta", pdtaPayload);

    std::vector<u8> file;
    file.insert(file.end(), {'R','I','F','F'});
    AppendU32LE(file, static_cast<u32>(riffPayload.size() + 4));
    file.insert(file.end(), {'s','f','b','k'});
    file.insert(file.end(), riffPayload.begin(), riffPayload.end());
    return file;
}

SFGenList MakeSignedGen(u16 oper, i16 value) {
    SFGenList gen{};
    gen.sfGenOper = oper;
    gen.genAmount.shAmount = value;
    return gen;
}

SFModList MakeMod(u16 src, u16 dest, i16 amount, u16 amtSrc, u16 transform) {
    SFModList mod{};
    mod.sfModSrcOper = src;
    mod.sfModDestOper = dest;
    mod.modAmount = amount;
    mod.sfModAmtSrcOper = amtSrc;
    mod.sfModTransOper = transform;
    return mod;
}

i32 ExpectedVelocityAttenuationCb(u16 velocity) {
    if (velocity == 0) return 960;
    if (velocity >= 65535) return 0;
    return static_cast<i32>(std::lround(400.0 * std::log10(65535.0 / velocity)));
}

u32 FloatToU32(f32 value) {
    const double scaled = std::clamp(static_cast<double>(value), 0.0, 1.0) * 4294967295.0;
    return static_cast<u32>(std::llround(scaled));
}

bool NearlyEqual(f64 lhs, f64 rhs, f64 epsilon = 1.0e-6) {
    return std::fabs(lhs - rhs) <= epsilon;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

const ResolvedZone& RequireSingleZone(const Sf2File& sf2, u8 key, u16 velocity,
                                      const ModulatorContext* ctx,
                                      std::vector<ResolvedZone>& zones) {
    zones.clear();
    Require(sf2.FindZones(0, 0, key, velocity, zones, ctx), "Expected SF2 zone resolution to succeed");
    Require(zones.size() == 1, "Expected exactly one resolved zone");
    return zones[0];
}

void TestForcedVelocityDefaultModulators() {
    MinimalSf2Config config;
    config.instGens.push_back(MakeSignedGen(GEN_Keynum, 72));
    config.instGens.push_back(MakeSignedGen(GEN_Velocity, 64));

    const std::vector<u8> bytes = BuildMinimalSf2(config);
    Sf2File sf2;
    Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

    std::vector<ResolvedZone> zones;
    if (!sf2.FindZones(0, 0, 60, 50000, zones, nullptr)) {
        int globalPresetBag = -1;
        int localPresetBag = -1;
        std::fprintf(stderr, "diagnostic: presets=%zu instruments=%zu samples=%zu\n",
            sf2.PresetCount(), sf2.InstrumentCount(), sf2.SampleHeaderCount());
        std::fprintf(stderr, "diagnostic: GetPresetBagIndices=%d global=%d local=%d\n",
            sf2.GetPresetBagIndices(0, 0, globalPresetBag, localPresetBag) ? 1 : 0,
            globalPresetBag, localPresetBag);
        std::exit(1);
    }
    Require(zones.size() == 1, "Expected exactly one resolved zone");

    const auto& zone = zones[0];
    Require(zone.generators[GEN_Keynum] == 72, "Forced key should be preserved");
    Require(zone.generators[GEN_Velocity] == 64, "Forced velocity should be preserved");

    const u16 forcedVelocity16 = static_cast<u16>((64 * 65535 + 63) / 127);
    const i32 expectedAtten = ExpectedVelocityAttenuationCb(forcedVelocity16);
    const i32 expectedFilter = std::clamp(13500 +
        static_cast<i32>(std::lround(-2400.0 * (1.0 - static_cast<double>(forcedVelocity16) / 65535.0))),
        1500, 13500);

    Require(zone.generators[GEN_InitialAttenuation] == expectedAtten,
            "Default velocity->attenuation should use forced velocity");
    Require(zone.generators[GEN_InitialFilterFc] == expectedFilter,
            "Default velocity->filter cutoff should use forced velocity");
}

void TestAbsoluteTransformSupport() {
    MinimalSf2Config config;
    config.instMods.push_back(MakeMod(static_cast<u16>(14 | 0x0200), GEN_InitialAttenuation, 100, 0, 2));

    const std::vector<u8> bytes = BuildMinimalSf2(config);
    Sf2File sf2;
    Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

    ModulatorContext ctx{};
    ctx.pitchBend = -8192;
    std::vector<ResolvedZone> zones;
    Require(sf2.FindZones(0, 0, 60, 65535, zones, &ctx), "FindZones with absolute transform should succeed");
    Require(zones.size() == 1, "Expected one zone for absolute transform test");
    Require(zones[0].generators[GEN_InitialAttenuation] == 100,
            "Absolute transform should turn negative pitch-bend source into positive attenuation");
}

void TestUnsupportedTransformReporting() {
    MinimalSf2Config config;
    config.instMods.push_back(MakeMod(2, GEN_Pan, 100, 0, 7));

    const std::vector<u8> bytes = BuildMinimalSf2(config);
    Sf2File sf2;
    Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());
    Require(sf2.UnsupportedModulatorCount() == 1, "Unsupported modulator count should be reported");
    Require(sf2.UnsupportedModulatorTransformCount() == 1, "Unsupported transform count should be reported");
}

void TestEffectsSendMixPolicy() {
    Voice additive;
    additive.compatOptions.multiplySf2MidiEffectsSends = false;
    additive.presetReverbSend = 0.25f;
    additive.presetChorusSend = 0.4f;
    additive.UpdateChannelMix(1.0f, 0x80000000u, FloatToU32(0.5f), FloatToU32(0.25f));
    Require(std::fabs(additive.reverbSend - 0.75f) < 1.0e-4f, "Additive send policy should sum sends");
    Require(std::fabs(additive.chorusSend - 0.65f) < 1.0e-4f, "Additive chorus policy should sum sends");

    Voice multiplied;
    multiplied.compatOptions.multiplySf2MidiEffectsSends = true;
    multiplied.presetReverbSend = 0.25f;
    multiplied.presetChorusSend = 0.4f;
    multiplied.UpdateChannelMix(1.0f, 0x80000000u, FloatToU32(0.5f), FloatToU32(0.25f));
    Require(std::fabs(multiplied.reverbSend - 0.125f) < 1.0e-4f, "Multiplicative send policy should multiply sends");
    Require(std::fabs(multiplied.chorusSend - 0.1f) < 1.0e-4f, "Multiplicative chorus policy should multiply sends");
}

void TestEnvelopePitchAndKeynumScaling() {
    MinimalSf2Config config;
    config.instGens.push_back(MakeSignedGen(GEN_ModEnvToPitch, 600));
    config.instGens.push_back(MakeSignedGen(GEN_HoldModEnv, -1200));
    config.instGens.push_back(MakeSignedGen(GEN_DecayModEnv, 0));
    config.instGens.push_back(MakeSignedGen(GEN_SustainModEnv, 500));
    config.instGens.push_back(MakeSignedGen(GEN_KeynumToModEnvHold, 100));
    config.instGens.push_back(MakeSignedGen(GEN_KeynumToModEnvDecay, -100));
    config.instGens.push_back(MakeSignedGen(GEN_HoldVolEnv, -1200));
    config.instGens.push_back(MakeSignedGen(GEN_DecayVolEnv, 0));
    config.instGens.push_back(MakeSignedGen(GEN_SustainVolEnv, 600));
    config.instGens.push_back(MakeSignedGen(GEN_KeynumToVolEnvHold, 100));
    config.instGens.push_back(MakeSignedGen(GEN_KeynumToVolEnvDecay, -100));

    const std::vector<u8> bytes = BuildMinimalSf2(config);
    Sf2File sf2;
    Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

    std::vector<ResolvedZone> zones;
    const ResolvedZone& zone = RequireSingleZone(sf2, 72, 65535, nullptr, zones);
    Require(zone.generators[GEN_ModEnvToPitch] == 600, "Resolved zone should preserve ModEnvToPitch");

    Voice voice;
    voice.NoteOn(zone, sf2.SampleData(), sf2.SampleDataCount(), 0, 0, 0, 72, 65535, 1, 48000, 0.0,
                 SoundBankKind::Sf2, SynthCompatOptions{});

    const f64 holdScale = std::pow(2.0, 100.0 * (60.0 - 72.0) / 1200.0);
    const f64 decayScale = std::pow(2.0, -100.0 * (60.0 - 72.0) / 1200.0);
    const u32 expectedModHoldEnd = static_cast<u32>(TimecentsToSeconds(-1200) * holdScale * 48000.0);
    const u32 expectedVolHoldEnd = static_cast<u32>(TimecentsToSeconds(-1200) * holdScale * 48000.0);
    const f32 expectedModDecayRate = static_cast<f32>(std::pow(0.5, 1.0 / (TimecentsToSeconds(0) * decayScale * 48000.0)));
    const f32 expectedVolSustainLevel = static_cast<f32>(CentibelsToGain(600));
    const f32 expectedVolDecayRate = static_cast<f32>(std::pow(
        std::max(static_cast<f64>(expectedVolSustainLevel), 1.0e-9), 1.0 / (TimecentsToSeconds(0) * decayScale * 48000.0)));

    Require(voice.useModEnv, "Mod env should be enabled when ModEnvToPitch is present");
    Require(voice.modEnvToPitchCents == 600.0f, "Voice should apply ModEnvToPitch from the resolved zone");
    Require(voice.modEnvHoldEnd == expectedModHoldEnd, "KeynumToModEnvHold should scale hold duration");
    Require(voice.envHoldEnd == expectedVolHoldEnd, "KeynumToVolEnvHold should scale hold duration");
    Require(NearlyEqual(voice.modEnvDecayRate, expectedModDecayRate, 1.0e-6), "KeynumToModEnvDecay should scale decay rate");
    Require(NearlyEqual(voice.envDecayRate, expectedVolDecayRate, 1.0e-6), "KeynumToVolEnvDecay should scale decay rate");
}

void TestPressureSources() {
    MinimalSf2Config config;
    config.instMods.push_back(MakeMod(10, GEN_Pan, 500, 0, 0));
    config.instMods.push_back(MakeMod(13, GEN_InitialAttenuation, 200, 0, 0));

    const std::vector<u8> bytes = BuildMinimalSf2(config);
    Sf2File sf2;
    Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

    ModulatorContext ctx{};
    ctx.polyPressure[60] = 127;
    ctx.channelPressure = 127;

    std::vector<ResolvedZone> zones;
    const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
    Require(zone.generators[GEN_Pan] == 500, "Poly pressure should be able to drive pan");
    Require(zone.generators[GEN_InitialAttenuation] == 200, "Channel pressure should be able to drive attenuation");
}

void TestPitchWheelSensitivityAmountSource() {
    MinimalSf2Config config;
    config.instMods.push_back(MakeMod(static_cast<u16>(14 | 0x0200), GEN_ModEnvToPitch, 1200, 16, 0));

    const std::vector<u8> bytes = BuildMinimalSf2(config);
    Sf2File sf2;
    Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

    ModulatorContext ctx{};
    ctx.pitchBend = 8191;
    ctx.pitchWheelSensitivitySemitones = 12;

    std::vector<ResolvedZone> zones;
    const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
    Require(zone.generators[GEN_ModEnvToPitch] == 600,
            "Pitch wheel sensitivity amount source should scale pitch bend modulation");
}

} // namespace

int main() {
    TestForcedVelocityDefaultModulators();
    TestAbsoluteTransformSupport();
    TestUnsupportedTransformReporting();
    TestEffectsSendMixPolicy();
    TestEnvelopePitchAndKeynumScaling();
    TestPressureSources();
    TestPitchWheelSensitivityAmountSource();
    std::printf("sf2_compliance: all tests passed\n");
    return 0;
}
