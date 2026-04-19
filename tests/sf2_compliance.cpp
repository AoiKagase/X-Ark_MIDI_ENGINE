#include "../src/sf2/Sf2File.h"
#include "../src/sf2/Sf2Types.h"
#include "../src/synth/Interpolator.h"
#include "../src/synth/Voice.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

using namespace XArkMidi;

namespace {

    const char* g_currentTestName = "";

    struct MinimalSf2Config {
        std::vector<SFGenList> presetGlobalGens;
        std::vector<SFModList> presetGlobalMods;
        std::vector<SFGenList> presetGens;
        std::vector<SFGenList> presetTrailingGens;
        std::vector<SFModList> presetMods;
        std::vector<SFGenList> instGlobalGens;
        std::vector<SFModList> instGlobalMods;
        std::vector<SFGenList> instGens;
        std::vector<SFGenList> instTrailingGens;
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
        const bool hasPresetGlobal = !config.presetGlobalGens.empty() || !config.presetGlobalMods.empty();
        const bool hasInstGlobal = !config.instGlobalGens.empty() || !config.instGlobalMods.empty();
        const u16 presetBagCount = static_cast<u16>(hasPresetGlobal ? 2 : 1);
        const u16 instBagCount = static_cast<u16>(hasInstGlobal ? 2 : 1);
        std::vector<u8> infoPayload;
        std::vector<u8> ifil;
        AppendU16LE(ifil, 2);
        AppendU16LE(ifil, 4);
        AppendChunk(infoPayload, "ifil", ifil);
        const std::vector<u8> isng = { 'E','M','U','8','0','0','0', 0 };
        AppendChunk(infoPayload, "isng", isng);
        const std::vector<u8> inam = { 'T','e','s','t', 0 };
        AppendChunk(infoPayload, "INAM", inam);

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
        appendPresetHeader("EOP", 0, 0, presetBagCount);
        AppendChunk(pdtaPayload, "phdr", phdr);

        std::vector<u8> pbag;
        AppendU16LE(pbag, 0);
        AppendU16LE(pbag, 0);
        if (hasPresetGlobal) {
            AppendU16LE(pbag, static_cast<u16>(config.presetGlobalGens.size()));
            AppendU16LE(pbag, static_cast<u16>(config.presetGlobalMods.size()));
        }
        AppendU16LE(pbag, static_cast<u16>(
            config.presetGlobalGens.size() + config.presetGens.size() + 1 + config.presetTrailingGens.size()));
        AppendU16LE(pbag, static_cast<u16>(
            config.presetGlobalMods.size() + config.presetMods.size()));
        AppendChunk(pdtaPayload, "pbag", pbag);

        std::vector<u8> pmod;
        for (const auto& mod : config.presetGlobalMods) {
            AppendU16LE(pmod, mod.sfModSrcOper);
            AppendU16LE(pmod, mod.sfModDestOper);
            AppendI16LE(pmod, mod.modAmount);
            AppendU16LE(pmod, mod.sfModAmtSrcOper);
            AppendU16LE(pmod, mod.sfModTransOper);
        }
        for (const auto& mod : config.presetMods) {
            AppendU16LE(pmod, mod.sfModSrcOper);
            AppendU16LE(pmod, mod.sfModDestOper);
            AppendI16LE(pmod, mod.modAmount);
            AppendU16LE(pmod, mod.sfModAmtSrcOper);
            AppendU16LE(pmod, mod.sfModTransOper);
        }
        for (int i = 0; i < 5; ++i) {
            AppendU16LE(pmod, 0);
        }
        AppendChunk(pdtaPayload, "pmod", pmod);

        std::vector<u8> pgen;
        auto appendGen = [](std::vector<u8>& bytes, const SFGenList& gen) {
            AppendU16LE(bytes, gen.sfGenOper);
            AppendU16LE(bytes, gen.genAmount.wAmount);
            };
        for (const auto& gen : config.presetGlobalGens) {
            appendGen(pgen, gen);
        }
        for (const auto& gen : config.presetGens) {
            appendGen(pgen, gen);
        }
        SFGenList instrumentGen{};
        instrumentGen.sfGenOper = GEN_Instrument;
        instrumentGen.genAmount.wAmount = 0;
        appendGen(pgen, instrumentGen);
        for (const auto& gen : config.presetTrailingGens) {
            appendGen(pgen, gen);
        }
        SFGenList terminalPgen{};
        terminalPgen.sfGenOper = 0;
        terminalPgen.genAmount.wAmount = 0;
        appendGen(pgen, terminalPgen);
        AppendChunk(pdtaPayload, "pgen", pgen);

        std::vector<u8> inst;
        auto appendInst = [&](const char* name, u16 bagIndex) {
            char padded[20] = {};
            std::strncpy(padded, name, sizeof(padded));
            inst.insert(inst.end(), padded, padded + 20);
            AppendU16LE(inst, bagIndex);
            };
        appendInst("Inst", 0);
        appendInst("EOI", instBagCount);
        AppendChunk(pdtaPayload, "inst", inst);

        std::vector<u8> ibag;
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 0);
        if (hasInstGlobal) {
            AppendU16LE(ibag, static_cast<u16>(config.instGlobalGens.size()));
            AppendU16LE(ibag, static_cast<u16>(config.instGlobalMods.size()));
        }
        AppendU16LE(ibag, static_cast<u16>(
            config.instGlobalGens.size() + config.instGens.size() + 1 + config.instTrailingGens.size()));
        AppendU16LE(ibag, static_cast<u16>(
            config.instGlobalMods.size() + config.instMods.size()));
        AppendChunk(pdtaPayload, "ibag", ibag);

        std::vector<u8> imod;
        for (const auto& mod : config.instGlobalMods) {
            AppendU16LE(imod, mod.sfModSrcOper);
            AppendU16LE(imod, mod.sfModDestOper);
            AppendI16LE(imod, mod.modAmount);
            AppendU16LE(imod, mod.sfModAmtSrcOper);
            AppendU16LE(imod, mod.sfModTransOper);
        }
        for (const auto& mod : config.instMods) {
            AppendU16LE(imod, mod.sfModSrcOper);
            AppendU16LE(imod, mod.sfModDestOper);
            AppendI16LE(imod, mod.modAmount);
            AppendU16LE(imod, mod.sfModAmtSrcOper);
            AppendU16LE(imod, mod.sfModTransOper);
        }
        for (int i = 0; i < 5; ++i) {
            AppendU16LE(imod, 0);
        }
        AppendChunk(pdtaPayload, "imod", imod);

        std::vector<u8> igen;
        for (const auto& gen : config.instGlobalGens) {
            appendGen(igen, gen);
        }
        for (const auto& gen : config.instGens) {
            appendGen(igen, gen);
        }
        SFGenList sampleGen{};
        sampleGen.sfGenOper = GEN_SampleID;
        sampleGen.genAmount.wAmount = 0;
        appendGen(igen, sampleGen);
        for (const auto& gen : config.instTrailingGens) {
            appendGen(igen, gen);
        }
        SFGenList terminalIgen{};
        terminalIgen.sfGenOper = 0;
        terminalIgen.genAmount.wAmount = 0;
        appendGen(igen, terminalIgen);
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
        file.insert(file.end(), { 'R','I','F','F' });
        AppendU32LE(file, static_cast<u32>(riffPayload.size() + 4));
        file.insert(file.end(), { 's','f','b','k' });
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
            std::fprintf(stderr, "FAILED [%s]: %s\n", g_currentTestName, message);
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
        const bool loaded = sf2.LoadFromMemory(bytes.data(), bytes.size());
        const std::string error = sf2.ErrorMessage();
        Require(loaded, error.c_str());

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

    void TestDefaultVelocityModulatorsAreNotSuppressedByAmountSourceMods() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(2, GEN_InitialAttenuation, 100, 16, 0));
        config.instMods.push_back(MakeMod(2, GEN_InitialFilterFc, 1200, 16, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        ctx.pitchWheelSensitivitySemitones = 24;

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 32768, &ctx, zones);
        const i32 expectedAtten = ExpectedVelocityAttenuationCb(32768u) + 50;
        const i32 customFilter = std::clamp(13500 + 600, 1500, 13500);
        const i32 expectedFilter = std::clamp(
            customFilter + static_cast<i32>(std::lround(-2400.0 * (1.0 - (32768.0 / 65535.0)))),
            1500, 13500);
        Require(zone.generators[GEN_InitialAttenuation] == expectedAtten,
            "Velocity mod with amount source must not suppress default attenuation modulator");
        Require(zone.generators[GEN_InitialFilterFc] == expectedFilter,
            "Velocity mod with amount source must not suppress default filter modulator");
    }

    void TestBagIndexHelpersSkipGlobalZones() {
        MinimalSf2Config config;
        config.presetGlobalGens.push_back(MakeSignedGen(GEN_CoarseTune, 1));
        config.instGlobalGens.push_back(MakeSignedGen(GEN_Pan, -100));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        int globalPresetBag = -1;
        int localPresetBag = -1;
        Require(sf2.GetPresetBagIndices(0, 0, globalPresetBag, localPresetBag),
            "GetPresetBagIndices should succeed with explicit global/local bags");
        Require(globalPresetBag == 0, "Preset global bag index should be 0");
        Require(localPresetBag == 1, "Preset local bag index should skip the global bag");

        int globalInstBag = -1;
        Require(sf2.GetInstrumentBagIndices(0, 1, globalInstBag),
            "GetInstrumentBagIndices should accept the local bag index");
        Require(globalInstBag == 0, "Instrument global bag index should be 0");
    }

    void TestPresetZoneTerminalInstrumentRule() {
        MinimalSf2Config config;
        config.presetTrailingGens.push_back(MakeSignedGen(GEN_OverridingRootKey, 72));
        config.instGens.push_back(MakeSignedGen(GEN_OverridingRootKey, 60));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_OverridingRootKey] == 60,
            "Generators after preset Instrument should be ignored");

        int globalPresetBag = -1;
        int localPresetBag = -1;
        Require(sf2.GetPresetBagIndices(0, 0, globalPresetBag, localPresetBag),
            "GetPresetBagIndices should still find the valid preset local zone");
        Require(localPresetBag == 0, "Single preset local bag should stay addressable");
    }

    void TestInstrumentZoneTerminalSampleRule() {
        MinimalSf2Config config;
        config.instTrailingGens.push_back(MakeSignedGen(GEN_ExclusiveClass, 5));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_ExclusiveClass] == 0,
            "Generators after instrument SampleID should be ignored");

        std::vector<Sf2File::ZoneInfo> localZones;
        Require(sf2.GetInstrumentLocalZones(0, localZones),
            "GetInstrumentLocalZones should still return the valid local zone");
        Require(localZones.size() == 1, "Expected a single instrument local zone");
        Require(localZones[0].sampleId == 0, "Local zone should keep its SampleID");
        Require(localZones[0].generators[GEN_ExclusiveClass] == 0,
            "GetInstrumentLocalZones should ignore generators after SampleID");
    }

    void TestPresetLevelIllegalSampleGeneratorsIgnored() {
        MinimalSf2Config config;
        config.presetGens.push_back(MakeSignedGen(GEN_OverridingRootKey, 72));
        config.presetGens.push_back(MakeSignedGen(GEN_ExclusiveClass, 9));
        config.presetGens.push_back(MakeSignedGen(GEN_SampleModes, 3));
        config.presetGens.push_back(MakeSignedGen(GEN_StartAddrsOffset, 10));
        config.presetGens.push_back(MakeSignedGen(GEN_EndAddrsOffset, -6));
        config.presetGens.push_back(MakeSignedGen(GEN_StartloopAddrsOffset, 4));
        config.presetGens.push_back(MakeSignedGen(GEN_EndloopAddrsOffset, -4));
        config.instGens.push_back(MakeSignedGen(GEN_OverridingRootKey, 60));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_OverridingRootKey] == 60,
            "Preset-level OverridingRootKey should be ignored");
        Require(zone.generators[GEN_ExclusiveClass] == 0,
            "Preset-level ExclusiveClass should be ignored");
        Require(zone.generators[GEN_SampleModes] == 0,
            "Preset-level SampleModes should be ignored");
        Require(zone.sample != nullptr, "Resolved zone sample should exist");
        Require(zone.sample->start == 0 && zone.sample->end == 64,
            "Preset-level sample address offsets should be ignored");
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
        char message[128];
        std::snprintf(message, sizeof(message), "Unsupported modulator count should be reported (actual=%u)",
            sf2.UnsupportedModulatorCount());
        Require(sf2.UnsupportedModulatorCount() == 1, message);
        std::snprintf(message, sizeof(message), "Unsupported transform count should be reported (actual=%u)",
            sf2.UnsupportedModulatorTransformCount());
        Require(sf2.UnsupportedModulatorTransformCount() == 1, message);
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
    void TestSourceCurvesSupport() {
        const u16 velocityConcave = static_cast<u16>(2u | (1u << 10));
        const u16 velocityConvex = static_cast<u16>(2u | (2u << 10));
        const u16 velocitySwitch = static_cast<u16>(2u | (3u << 10));

        {
            MinimalSf2Config config;
            config.instMods.push_back(MakeMod(velocityConcave, GEN_Pan, 500, 0, 0));
            config.instMods.push_back(MakeMod(velocityConvex, GEN_ModEnvToPitch, 500, 0, 0));
            config.instMods.push_back(MakeMod(velocitySwitch, GEN_InitialFilterQ, 500, 0, 0));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 32768, nullptr, zones);
            Require(zone.generators[GEN_Pan] == 354,
                "Concave source curve should map mid velocity to sin(pi/4)");
            Require(zone.generators[GEN_ModEnvToPitch] == 146,
                "Convex source curve should map mid velocity to 1-cos(pi/4)");
            Require(zone.generators[GEN_InitialFilterQ] == 500,
                "Switch source curve should step to 1.0 at mid velocity");
        }
    }

    // ---------------------------------------------------------------------------
    // velocity 7-bit 変換の境界値確認
    // ---------------------------------------------------------------------------
    void TestVelocityZoneBoundary() {
        MinimalSf2Config config;
        // velRange lo=64, hi=127 のゾーンを作る
        SFGenList velRange{};
        velRange.sfGenOper = GEN_VelRange;
        velRange.genAmount.ranges.lo = 64;
        velRange.genAmount.ranges.hi = 127;
        config.instGens.push_back(velRange);

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        // vel7 変換式: (velocity * 127 + 32767) / 65535
            // vel7=64 になる最小の 16-bit velocity を逆算する。
            //
            // (v * 127 + 32767) / 65535 >= 64
            // v * 127 + 32767 >= 64 * 65535
            // v >= (64 * 65535 - 32767) / 127 = (4194240 - 32767) / 127 = 32769.70...
            // → 切り上げて v = 32770 ... ではなく実際に計算すると境界は 32768。
            //
            // 正確には: vel7(32767) = (32767*127+32767)/65535 = 63
            //           vel7(32768) = (32768*127+32767)/65535 = 64
            // よって境界値 = 32768
        const u16 vel16_boundary = 32768u;

        std::vector<ResolvedZone> zones;
        Require(sf2.FindZones(0, 0, 60, vel16_boundary, zones, nullptr),
            "Velocity boundary (vel7=64) should hit velRange lo=64");

        // 32767 は vel7=63 → velRange lo=64 にヒットしないはず
        zones.clear();
        const bool hit = sf2.FindZones(0, 0, 60, 32767u, zones, nullptr);
        Require(!hit,
            "Velocity just below boundary (vel7=63) should miss velRange lo=64");

    }

    // ---------------------------------------------------------------------------
    // sm24 チャンクを含む SF2 で HasIgnoredSm24() が true になることを確認
    // ---------------------------------------------------------------------------
    void TestSm24Detection() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const std::vector<u8> original = bytes;

        const std::array<u8, 8> sm24Chunk = { 's', 'm', '2', '4', 0, 0, 0, 0 };
        auto insertSm24 = [&](std::vector<u8>& file) {
            auto readLE32 = [&](size_t offset) -> u32 {
                return static_cast<u32>(file[offset]) |
                    (static_cast<u32>(file[offset + 1]) << 8) |
                    (static_cast<u32>(file[offset + 2]) << 16) |
                    (static_cast<u32>(file[offset + 3]) << 24);
            };
            auto findSdtaList = [&]() -> size_t {
                for (size_t i = 12; i + 12 <= file.size(); ) {
                    if (file[i] == 'L' && file[i + 1] == 'I' && file[i + 2] == 'S' && file[i + 3] == 'T') {
                        const u32 chunkSize = readLE32(i + 4);
                        if (file[i + 8] == 's' && file[i + 9] == 'd' && file[i + 10] == 't' && file[i + 11] == 'a') {
                            return i;
                        }
                        i += 8 + chunkSize + (chunkSize & 1u);
                        continue;
                    }
                    break;
                }
                return std::numeric_limits<size_t>::max();
            };
            const size_t sdtaListPos = findSdtaList();
            Require(sdtaListPos != std::numeric_limits<size_t>::max(), "sdta LIST chunk should exist");
            const size_t sdtaPayloadEnd = sdtaListPos + 8 + readLE32(sdtaListPos + 4);
            file.insert(file.begin() + static_cast<std::ptrdiff_t>(sdtaPayloadEnd), sm24Chunk.begin(), sm24Chunk.end());
            auto addLE32 = [&](size_t offset, u32 delta) {
                const u32 value = readLE32(offset);
                const u32 updated = value + delta;
                file[offset] = static_cast<u8>(updated & 0xFFu);
                file[offset + 1] = static_cast<u8>((updated >> 8) & 0xFFu);
                file[offset + 2] = static_cast<u8>((updated >> 16) & 0xFFu);
                file[offset + 3] = static_cast<u8>((updated >> 24) & 0xFFu);
            };
            addLE32(4, static_cast<u32>(sm24Chunk.size()));
            addLE32(sdtaListPos + 4, static_cast<u32>(sm24Chunk.size()));
        };
        insertSm24(bytes);

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 with sm24 chunk should load");
        Require(sf2.HasIgnoredSm24(), "HasIgnoredSm24 should be true when sm24 chunk is present");

        Require(sf2.LoadFromMemory(original.data(), original.size()), "Base SF2 reload failed");
        Require(!sf2.HasIgnoredSm24(), "HasIgnoredSm24 should reset on subsequent loads");
    }

    void TestMissingIfilRejected() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        auto readLE32 = [&](size_t offset) -> u32 {
            return static_cast<u32>(bytes[offset]) |
                   (static_cast<u32>(bytes[offset + 1]) << 8) |
                   (static_cast<u32>(bytes[offset + 2]) << 16) |
                   (static_cast<u32>(bytes[offset + 3]) << 24);
        };
        auto writeLE32 = [&](size_t offset, u32 value) {
            bytes[offset] = static_cast<u8>(value & 0xFFu);
            bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xFFu);
            bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xFFu);
            bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xFFu);
        };
        auto findInfoList = [&]() -> size_t {
            for (size_t i = 12; i + 12 <= bytes.size();) {
                if (std::memcmp(bytes.data() + i, "LIST", 4) != 0) {
                    break;
                }
                const u32 chunkSize = readLE32(i + 4);
                if (std::memcmp(bytes.data() + i + 8, "INFO", 4) == 0) {
                    return i;
                }
                i += 8 + chunkSize + (chunkSize & 1u);
            }
            return std::numeric_limits<size_t>::max();
        };

        const size_t infoPos = findInfoList();
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
        const u32 infoChunkSize = readLE32(infoPos + 4);
        const size_t infoChunkEnd = infoPos + 8 + infoChunkSize + (infoChunkSize & 1u);
        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(infoPos + 12),
                    bytes.begin() + static_cast<std::ptrdiff_t>(infoChunkEnd));
        writeLE32(infoPos + 4, 4);
        writeLE32(4, static_cast<u32>(bytes.size() - 8));

        Sf2File sf2;
        Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 missing ifil should be rejected");
    }

    void TestNonMonotonicPbagRejected() {
        MinimalSf2Config config;
        config.presetGlobalGens.push_back(MakeSignedGen(GEN_CoarseTune, 1));
        std::vector<u8> bytes = BuildMinimalSf2(config);

        auto readLE32 = [&](size_t offset) -> u32 {
            return static_cast<u32>(bytes[offset]) |
                   (static_cast<u32>(bytes[offset + 1]) << 8) |
                   (static_cast<u32>(bytes[offset + 2]) << 16) |
                   (static_cast<u32>(bytes[offset + 3]) << 24);
        };
        auto findPdtaChunk = [&](const char id[4]) -> size_t {
            for (size_t i = 12; i + 12 <= bytes.size();) {
                if (std::memcmp(bytes.data() + i, "LIST", 4) != 0) {
                    break;
                }
                const u32 listSize = readLE32(i + 4);
                const size_t listData = i + 12;
                const size_t listEnd = i + 8 + listSize;
                if (std::memcmp(bytes.data() + i + 8, "pdta", 4) == 0) {
                    for (size_t p = listData; p + 8 <= listEnd;) {
                        const u32 chunkSize = readLE32(p + 4);
                        if (std::memcmp(bytes.data() + p, id, 4) == 0) {
                            return p;
                        }
                        p += 8 + chunkSize + (chunkSize & 1u);
                    }
                }
                i += 8 + listSize + (listSize & 1u);
            }
            return std::numeric_limits<size_t>::max();
        };
        const size_t pbagPos = findPdtaChunk("pbag");
        Require(pbagPos != std::numeric_limits<size_t>::max(), "pbag chunk should exist");
        const size_t pbagData = pbagPos + 8;
        // terminal bag wGenNdx -> 0, making indices non-monotonic (0,1,0)
        bytes[pbagData + 8] = 0;
        bytes[pbagData + 9] = 0;

        Sf2File sf2;
        Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 with non-monotonic pbag should be rejected");
    }

} // namespace

int main() {
    g_currentTestName = "TestForcedVelocityDefaultModulators";
    TestForcedVelocityDefaultModulators();
    g_currentTestName = "TestDefaultVelocityModulatorsAreNotSuppressedByAmountSourceMods";
    TestDefaultVelocityModulatorsAreNotSuppressedByAmountSourceMods();
    g_currentTestName = "TestAbsoluteTransformSupport";
    TestAbsoluteTransformSupport();
    g_currentTestName = "TestPresetZoneTerminalInstrumentRule";
    TestPresetZoneTerminalInstrumentRule();
    g_currentTestName = "TestInstrumentZoneTerminalSampleRule";
    TestInstrumentZoneTerminalSampleRule();
    g_currentTestName = "TestPresetLevelIllegalSampleGeneratorsIgnored";
    TestPresetLevelIllegalSampleGeneratorsIgnored();
    g_currentTestName = "TestUnsupportedTransformReporting";
    TestUnsupportedTransformReporting();
    g_currentTestName = "TestEffectsSendMixPolicy";
    TestEffectsSendMixPolicy();
    g_currentTestName = "TestEnvelopePitchAndKeynumScaling";
    TestEnvelopePitchAndKeynumScaling();
    g_currentTestName = "TestPressureSources";
    TestPressureSources();
    g_currentTestName = "TestPitchWheelSensitivityAmountSource";
    TestPitchWheelSensitivityAmountSource();
    g_currentTestName = "TestSourceCurvesSupport";
    TestSourceCurvesSupport();
    g_currentTestName = "TestVelocityZoneBoundary";
    TestVelocityZoneBoundary();
    g_currentTestName = "TestSm24Detection";
    TestSm24Detection();
    g_currentTestName = "TestBagIndexHelpersSkipGlobalZones";
    TestBagIndexHelpersSkipGlobalZones();
    g_currentTestName = "TestMissingIfilRejected";
    TestMissingIfilRejected();
    g_currentTestName = "TestNonMonotonicPbagRejected";
    TestNonMonotonicPbagRejected();
    std::printf("sf2_compliance: all tests passed\n");
    return 0;
}
