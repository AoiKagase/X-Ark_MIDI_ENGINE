#include "../src/sf2/Sf2File.h"
#include "../src/sf2/Sf2Types.h"
#include "../src/synth/Interpolator.h"
#include "../src/synth/Voice.h"
#include "../src/synth/VoicePool.h"
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

    std::vector<u8> BuildStereoLinkedSf2() {
        std::vector<u8> infoPayload;
        std::vector<u8> ifil;
        AppendU16LE(ifil, 2);
        AppendU16LE(ifil, 4);
        AppendChunk(infoPayload, "ifil", ifil);
        const std::vector<u8> isng = { 'E','M','U','8','0','0','0', 0 };
        AppendChunk(infoPayload, "isng", isng);
        const std::vector<u8> inam = { 'S','t','e','r','e','o', 0 };
        AppendChunk(infoPayload, "INAM", inam);

        std::vector<u8> sdtaPayload;
        std::vector<u8> smpl;
        for (int i = 0; i < 64; ++i) {
            AppendI16LE(smpl, static_cast<i16>(1000 + i * 120));
        }
        for (int i = 0; i < 46; ++i) {
            AppendI16LE(smpl, 0);
        }
        for (int i = 0; i < 64; ++i) {
            AppendI16LE(smpl, static_cast<i16>(-1000 - i * 120));
        }
        for (int i = 0; i < 46; ++i) {
            AppendI16LE(smpl, 0);
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
        appendPresetHeader("Stereo", 0, 0, 0);
        appendPresetHeader("EOP", 0, 0, 1);
        AppendChunk(pdtaPayload, "phdr", phdr);

        std::vector<u8> pbag;
        AppendU16LE(pbag, 0);
        AppendU16LE(pbag, 0);
        AppendU16LE(pbag, 1);
        AppendU16LE(pbag, 0);
        AppendChunk(pdtaPayload, "pbag", pbag);

        std::vector<u8> pmod;
        for (int i = 0; i < 5; ++i) {
            AppendU16LE(pmod, 0);
        }
        AppendChunk(pdtaPayload, "pmod", pmod);

        std::vector<u8> pgen;
        AppendU16LE(pgen, GEN_Instrument);
        AppendU16LE(pgen, 0);
        AppendU16LE(pgen, 0);
        AppendU16LE(pgen, 0);
        AppendChunk(pdtaPayload, "pgen", pgen);

        std::vector<u8> inst;
        auto appendInst = [&](const char* name, u16 bagIndex) {
            char padded[20] = {};
            std::strncpy(padded, name, sizeof(padded));
            inst.insert(inst.end(), padded, padded + 20);
            AppendU16LE(inst, bagIndex);
        };
        appendInst("StereoInst", 0);
        appendInst("EOI", 2);
        AppendChunk(pdtaPayload, "inst", inst);

        std::vector<u8> ibag;
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 1);
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 2);
        AppendU16LE(ibag, 0);
        AppendChunk(pdtaPayload, "ibag", ibag);

        std::vector<u8> imod;
        for (int i = 0; i < 5; ++i) {
            AppendU16LE(imod, 0);
        }
        AppendChunk(pdtaPayload, "imod", imod);

        std::vector<u8> igen;
        AppendU16LE(igen, GEN_SampleID);
        AppendU16LE(igen, 0);
        AppendU16LE(igen, GEN_SampleID);
        AppendU16LE(igen, 1);
        AppendU16LE(igen, 0);
        AppendU16LE(igen, 0);
        AppendChunk(pdtaPayload, "igen", igen);

        std::vector<u8> shdr;
        auto appendSampleHeader = [&](const char* name, u32 start, u32 end, u32 loopStart, u32 loopEnd,
                                      u16 sampleLink, u16 sampleType) {
            char padded[20] = {};
            std::strncpy(padded, name, sizeof(padded));
            shdr.insert(shdr.end(), padded, padded + 20);
            AppendU32LE(shdr, start);
            AppendU32LE(shdr, end);
            AppendU32LE(shdr, loopStart);
            AppendU32LE(shdr, loopEnd);
            AppendU32LE(shdr, 44100);
            shdr.push_back(60);
            shdr.push_back(0);
            AppendU16LE(shdr, sampleLink);
            AppendU16LE(shdr, sampleType);
        };
        appendSampleHeader("Left", 0, 64, 8, 56, 1, 4);
        appendSampleHeader("Right", 110, 174, 118, 166, 0, 2);
        appendSampleHeader("EOS", 0, 0, 0, 0, 0, 1);
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
        for (int i = 0; i < 46; ++i) {
            AppendI16LE(smpl, 0);
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

    u32 ReadLE32(const std::vector<u8>& bytes, size_t offset) {
        return static_cast<u32>(bytes[offset]) |
               (static_cast<u32>(bytes[offset + 1]) << 8) |
               (static_cast<u32>(bytes[offset + 2]) << 16) |
               (static_cast<u32>(bytes[offset + 3]) << 24);
    }

    void WriteLE32(std::vector<u8>& bytes, size_t offset, u32 value) {
        bytes[offset] = static_cast<u8>(value & 0xFFu);
        bytes[offset + 1] = static_cast<u8>((value >> 8) & 0xFFu);
        bytes[offset + 2] = static_cast<u8>((value >> 16) & 0xFFu);
        bytes[offset + 3] = static_cast<u8>((value >> 24) & 0xFFu);
    }

    size_t FindListChunk(const std::vector<u8>& bytes, const char type[4]) {
        for (size_t i = 12; i + 12 <= bytes.size();) {
            if (std::memcmp(bytes.data() + i, "LIST", 4) != 0) {
                break;
            }
            const u32 chunkSize = ReadLE32(bytes, i + 4);
            if (std::memcmp(bytes.data() + i + 8, type, 4) == 0) {
                return i;
            }
            i += 8 + chunkSize + (chunkSize & 1u);
        }
        return std::numeric_limits<size_t>::max();
    }

    size_t FindPdtaChunk(const std::vector<u8>& bytes, const char id[4]) {
        const size_t pdtaPos = FindListChunk(bytes, "pdta");
        if (pdtaPos == std::numeric_limits<size_t>::max()) {
            return pdtaPos;
        }
        const size_t listData = pdtaPos + 12;
        const size_t listEnd = pdtaPos + 8 + ReadLE32(bytes, pdtaPos + 4);
        for (size_t p = listData; p + 8 <= listEnd;) {
            const u32 chunkSize = ReadLE32(bytes, p + 4);
            if (std::memcmp(bytes.data() + p, id, 4) == 0) {
                return p;
            }
            p += 8 + chunkSize + (chunkSize & 1u);
        }
        return std::numeric_limits<size_t>::max();
    }

    void AddChunkSize(std::vector<u8>& bytes, size_t offset, u32 delta) {
        WriteLE32(bytes, offset, ReadLE32(bytes, offset) + delta);
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

    void SetDefaultMidiControllers(ModulatorContext& ctx) {
        ctx.ccValues[7] = 127;
        ctx.ccValues[10] = 64;
        ctx.ccValues[11] = 127;
        ctx.ccValues[91] = 40;
        ctx.ccValues[93] = 0;
        ctx.pitchWheelSensitivitySemitones = 2;
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
        SetDefaultMidiControllers(ctx);
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

    void TestDuplicateModulatorsUseLastDefinition() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(2, GEN_Pan, 100, 0, 0));
        config.instMods.push_back(MakeMod(2, GEN_Pan, 300, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_Pan] == 300,
            "Duplicate modulators should ignore the earlier definition");
    }

    void TestLinkedModulatorsFeedTargetSource() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(0, static_cast<u16>(0x8000u | 2u), 100, 0, 0));
        config.instMods.push_back(MakeMod(2, static_cast<u16>(0x8000u | 2u), 200, 0, 0));
        config.instMods.push_back(MakeMod(127, GEN_Pan, 1, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_Pan] == 300,
            "Linked modulators should sum into the target modulator source");
    }

    void TestAbsoluteTransformSupport() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(static_cast<u16>(14 | 0x0200), GEN_InitialAttenuation, 100, 0, 2));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
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

    void TestUnsupportedAmountSourceIgnored() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(2, GEN_Pan, 500, 1, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        char message[128];
        std::snprintf(message, sizeof(message), "Unsupported amount-source modulator count should be reported (actual=%u)",
            sf2.UnsupportedModulatorCount());
        Require(sf2.UnsupportedModulatorCount() == 1, message);
        Require(sf2.UnsupportedModulatorTransformCount() == 0,
            "Unsupported amount-source modulator must not increment transform count");

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_Pan] == 0,
            "Unsupported amount-source modulator should be ignored instead of applying full amount");
    }

    void TestEffectsSendMixPolicy() {
        Voice sf2;
        sf2.soundBankKind = SoundBankKind::Sf2;
        sf2.presetReverbSend = 0.25f;
        sf2.presetChorusSend = 0.4f;
        sf2.UpdateChannelMix(0.75f, 0xFFFFFFFFu, FloatToU32(0.5f), FloatToU32(0.25f));
        Require(std::fabs(sf2.channelGainL - 0.75f) < 1.0e-4f, "SF2 channel pan should not be post-applied on the left lane");
        Require(std::fabs(sf2.channelGainR - 0.75f) < 1.0e-4f, "SF2 channel pan should not be post-applied on the right lane");
        Require(std::fabs(sf2.reverbSend - 0.25f) < 1.0e-4f, "SF2 channel reverb send should come from modulators by default");
        Require(std::fabs(sf2.chorusSend - 0.4f) < 1.0e-4f, "SF2 channel chorus send should come from modulators by default");

        Voice sf2Compat;
        sf2Compat.soundBankKind = SoundBankKind::Sf2;
        sf2Compat.compatOptions.multiplySf2MidiEffectsSends = true;
        sf2Compat.presetReverbSend = 0.25f;
        sf2Compat.presetChorusSend = 0.4f;
        sf2Compat.UpdateChannelMix(1.0f, 0x80000000u, FloatToU32(0.5f), FloatToU32(0.25f));
        Require(std::fabs(sf2Compat.reverbSend - 0.125f) < 1.0e-4f, "SF2 compatibility mode should multiply reverb sends");
        Require(std::fabs(sf2Compat.chorusSend - 0.1f) < 1.0e-4f, "SF2 compatibility mode should multiply chorus sends");

        Voice dls;
        dls.soundBankKind = SoundBankKind::Dls;
        dls.presetReverbSend = 0.25f;
        dls.presetChorusSend = 0.4f;
        dls.UpdateChannelMix(1.0f, 0xFFFFFFFFu, FloatToU32(0.5f), FloatToU32(0.25f));
        Require(dls.channelGainR > dls.channelGainL, "Non-SF2 channel pan should still affect the output mix");
        Require(std::fabs(dls.reverbSend - 0.75f) < 1.0e-4f, "Non-SF2 send policy should still sum sends");
        Require(std::fabs(dls.chorusSend - 0.65f) < 1.0e-4f, "Non-SF2 chorus policy should still sum sends");
    }

    void TestSf2PitchPrecedence() {
        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> neutralZones;
        const ResolvedZone& neutralZone = RequireSingleZone(sf2, 60, 65535, nullptr, neutralZones);

        ModulatorContext bentCtx{};
        SetDefaultMidiControllers(bentCtx);
        bentCtx.pitchBend = 8191;
        bentCtx.pitchWheelSensitivitySemitones = 12;
        std::vector<ResolvedZone> bentZones;
        const ResolvedZone& bentZone = RequireSingleZone(sf2, 60, 65535, &bentCtx, bentZones);

        Voice neutralVoice;
        neutralVoice.NoteOn(neutralZone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 44100, 0.0,
                            SoundBankKind::Sf2, SynthCompatOptions{});

        Voice bentVoice;
        bentVoice.NoteOn(bentZone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 44100, 0.0,
                         SoundBankKind::Sf2, SynthCompatOptions{});

        Voice doubledVoice;
        doubledVoice.NoteOn(bentZone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 44100, 12.0,
                            SoundBankKind::Sf2, SynthCompatOptions{});

        const f64 neutralStep = static_cast<f64>(neutralVoice.sampleStepFixed);
        const f64 bentStep = static_cast<f64>(bentVoice.sampleStepFixed);
        const f64 doubledStep = static_cast<f64>(doubledVoice.sampleStepFixed);
        Require(NearlyEqual(bentStep / neutralStep, 2.0, 1.0e-3),
            "SF2 pitch bend should already be resolved into InitialPitch before voice start");
        Require(NearlyEqual(doubledStep / bentStep, 2.0, 1.0e-3),
            "Applying channel pitch again on top of the resolved SF2 zone would double the bend");
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
        voice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 72, 65535, 1, 48000, 0.0,
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

    void TestEnvelopeReleaseRecalculation() {
        MinimalSf2Config config;
        config.instGens.push_back(MakeSignedGen(GEN_SampleModes, 3));
        config.instGens.push_back(MakeSignedGen(GEN_ModEnvToPitch, 600));
        config.instGens.push_back(MakeSignedGen(GEN_ReleaseVolEnv, -12000));
        config.instGens.push_back(MakeSignedGen(GEN_ReleaseModEnv, -12000));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);

        Voice voice;
        voice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 48000, 0.0,
            SoundBankKind::Sf2, SynthCompatOptions{});

        voice.envPhase = EnvPhase::Sustain;
        voice.envLevel = 0.25f;
        voice.modEnvPhase = EnvPhase::Sustain;
        voice.modEnvLevel = 0.5f;

        voice.NoteOff();

        const f32 expectedReleaseTime = 0.005f;
        const f32 expectedEnvReleaseRate = static_cast<f32>(std::pow(1.0e-5 / 0.25, 1.0 / (expectedReleaseTime * 48000.0)));
        const f32 expectedModReleaseRate = static_cast<f32>(std::pow(1.0e-5 / 0.5, 1.0 / (expectedReleaseTime * 48000.0)));

        Require(!voice.looping, "Loop-until-release voice should stop looping when note-off enters release");
        Require(voice.envPhase == EnvPhase::Release, "NoteOff should switch the volume envelope into release");
        Require(voice.modEnvPhase == EnvPhase::Release, "NoteOff should switch the modulation envelope into release");
        Require(NearlyEqual(voice.envReleaseRate, expectedEnvReleaseRate, 1.0e-6),
            "Volume release should be recomputed from the current envelope level with loop minimum release time");
        Require(NearlyEqual(voice.modEnvReleaseRate, expectedModReleaseRate, 1.0e-6),
            "Mod release should be recomputed from the current modulation level with loop minimum release time");
    }

    void TestFilterAndLfoInitialization() {
        MinimalSf2Config config;
        config.instGens.push_back(MakeSignedGen(GEN_InitialFilterFc, 9000));
        config.instGens.push_back(MakeSignedGen(GEN_InitialFilterQ, 300));
        config.instGens.push_back(MakeSignedGen(GEN_ModEnvToFilterFc, 1200));
        config.instGens.push_back(MakeSignedGen(GEN_ModLfoToFilterFc, 600));
        config.instGens.push_back(MakeSignedGen(GEN_DelayModLFO, -600));
        config.instGens.push_back(MakeSignedGen(GEN_FreqModLFO, 1200));
        config.instGens.push_back(MakeSignedGen(GEN_DelayVibLFO, -1200));
        config.instGens.push_back(MakeSignedGen(GEN_FreqVibLFO, 0));
        config.instGens.push_back(MakeSignedGen(GEN_VibLfoToPitch, 75));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);

        Voice voice;
        voice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 48000, 0.0,
            SoundBankKind::Sf2, SynthCompatOptions{});

        const u32 expectedModLfoDelayEnd = static_cast<u32>(TimecentsToSeconds(-600) * 48000.0);
        const u32 expectedVibLfoDelayEnd = static_cast<u32>(TimecentsToSeconds(-1200) * 48000.0);
        const f32 expectedModLfoPhaseStep = static_cast<f32>((8.176 * std::pow(2.0, 1200.0 / 1200.0)) / 48000.0);
        const f32 expectedVibLfoPhaseStep = static_cast<f32>(8.176 / 48000.0);

        Require(voice.filterEnabled, "InitialFilterFc and filter modulators should enable the filter path");
        Require(voice.useModEnv, "ModEnvToFilterFc should enable modulation envelope processing");
        Require(voice.filterBaseFcCents == 9000, "InitialFilterFc should initialize filter cutoff");
        Require(voice.filterCurrentFcCents == 9000, "Current filter cutoff should start from the base cutoff");
        Require(voice.filterQCb == 300, "InitialFilterQ should initialize filter resonance");
        Require(voice.filterModEnvToFcCents == 1200, "ModEnvToFilterFc should initialize filter modulation depth");
        Require(voice.modLfoDelayEnd == expectedModLfoDelayEnd, "DelayModLFO should initialize modulation LFO delay");
        Require(NearlyEqual(voice.modLfoPhaseStep, expectedModLfoPhaseStep, 1.0e-7), "FreqModLFO should initialize modulation LFO rate");
        Require(voice.modLfoToFilterFcCents == 600.0f, "ModLfoToFilterFc should initialize filter LFO depth");
        Require(voice.vibLfoDelayEnd == expectedVibLfoDelayEnd, "DelayVibLFO should initialize vibrato LFO delay");
        Require(NearlyEqual(voice.vibLfoPhaseStep, expectedVibLfoPhaseStep, 1.0e-7), "FreqVibLFO should initialize vibrato LFO rate");
        Require(voice.vibLfoToPitchCents == 75.0f, "VibLfoToPitch should initialize vibrato pitch depth");
    }

    void TestPressureSources() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(10, GEN_Pan, 500, 0, 0));
        config.instMods.push_back(MakeMod(13, GEN_InitialAttenuation, 200, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
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
        SetDefaultMidiControllers(ctx);
        ctx.pitchBend = 8191;
        ctx.pitchWheelSensitivitySemitones = 12;

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
        Require(zone.generators[GEN_ModEnvToPitch] == 600,
            "Pitch wheel sensitivity amount source should scale pitch bend modulation");
    }

    void TestRemainingDefaultModulators() {
        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        {
            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.channelPressure = 127;
            ctx.ccValues[1] = 127;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_VibLfoToPitch] == 100,
                "Channel pressure and CC1 defaults should sum into VibLfoToPitch");
        }

        {
            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.ccValues[7] = 0;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_InitialAttenuation] == 960,
                "CC7 default should drive initial attenuation");
        }

        {
            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.ccValues[10] = 127;
            ctx.ccValues[91] = 127;
            ctx.ccValues[93] = 127;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_Pan] == 500,
                "CC10 default should drive pan");
            Require(zone.generators[GEN_ReverbEffectsSend] == 200,
                "CC91 default should drive reverb send");
            Require(zone.generators[GEN_ChorusEffectsSend] == 200,
                "CC93 default should drive chorus send");
        }

        {
            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.ccValues[11] = 0;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_InitialAttenuation] == 960,
                "CC11 default should drive initial attenuation");
        }

        {
            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.pitchBend = 8191;
            ctx.pitchWheelSensitivitySemitones = 12;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            const i32 pitchCents = zone.generators[GEN_CoarseTune] * 100 + zone.generators[GEN_FineTune];
            Require(pitchCents == 1200,
                "Pitch wheel default should feed initial pitch from pitch wheel sensitivity");
        }
    }

    void TestDefaultModulatorSupersedeSemantics() {
        {
            MinimalSf2Config config;
            config.instMods.push_back(MakeMod(0x028A, GEN_Pan, 100, 0, 0));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.ccValues[10] = 80;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_Pan] == 26,
                "Instrument-level explicit default mod should supersede the implicit default");
        }

        {
            MinimalSf2Config config;
            config.presetMods.push_back(MakeMod(0x028A, GEN_Pan, 100, 0, 0));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.ccValues[10] = 80;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_Pan] == 286,
                "Preset-level explicit default mod should add to the implicit default");
        }
    }

    void TestStereoSampleLinks() {
        const std::vector<u8> bytes = BuildStereoLinkedSf2();
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());
        Require(sf2.SampleHeaderCount() >= 2, "Stereo SF2 should expose sample headers");
        Require(sf2.SampleDataCount() >= 128, "Stereo SF2 should expose sample PCM");
        Require(sf2.SampleData()[0] == 1000, "Left stereo sample PCM should be preserved");
        Require(sf2.SampleData()[64] == -1000, "Right stereo sample PCM should be preserved");
        Require(sf2.SampleHeaders(0)->sampleLink == 1, "Left sample should preserve wSampleLink");
        Require(sf2.SampleHeaders(1)->sampleLink == 0, "Right sample should preserve wSampleLink");

        std::vector<ResolvedZone> zones;
        Require(sf2.FindZones(0, 0, 61, 65535, zones, nullptr), "Stereo SF2 should resolve zones");
        Require(zones.size() == 2, "Stereo SF2 should resolve both linked zones");

        {
            Voice voice;
            SynthCompatOptions compatOptions{};
            std::array<f32, 256> testL{};
            std::array<f32, 256> testR{};
            std::array<f32, 256> testRevL{};
            std::array<f32, 256> testRevR{};
            std::array<f32, 256> testChoL{};
            std::array<f32, 256> testChoR{};
            voice.NoteOn(zones[0], sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 61, 65535, 1, 44100, 0.0,
                         SoundBankKind::Sf2, compatOptions);
            char stateMessage[256];
            std::snprintf(stateMessage, sizeof(stateMessage),
                          "Voice should activate for stereo sample (active=%d sampleEnd=%u step=%lld dryL=%f dryR=%f)",
                          voice.active ? 1 : 0, voice.sampleEnd, static_cast<long long>(voice.sampleStepFixed),
                          voice.dryGainL, voice.dryGainR);
            Require(voice.active, stateMessage);
            voice.UpdateChannelMix(1.0f, 0x81020408u, 0x50A14285u, 0u);
            voice.RenderBlock(testL.data(), testR.data(), testRevL.data(), testRevR.data(), testChoL.data(), testChoR.data(),
                              static_cast<u32>(testL.size()));
            f32 directSum = 0.0f;
            for (size_t i = 0; i < testL.size(); ++i) {
                directSum += std::fabs(testL[i]) + std::fabs(testR[i]);
            }
            char message[160];
            std::snprintf(message, sizeof(message),
                          "Direct stereo-zone voice should render audible output (sum=%f sampleEnd=%u step=%lld dryL=%f dryR=%f)",
                          directSum, voice.sampleEnd, static_cast<long long>(voice.sampleStepFixed),
                          voice.dryGainL, voice.dryGainR);
            Require(directSum > 0.1f, message);
        }

        VoicePool pool;
        SynthCompatOptions compatOptions{};
        pool.NoteOn(zones,
                    sf2.SampleData(),
                    sf2.SampleData24(),
                    sf2.SampleDataCount(),
                    0,
                    0,
                    0,
                    61,
                    65535,
                    44100,
                    0.0,
                    0,
                    1.0f,
                    0x81020408u,
                    0x50A14285u,
                    0u,
                    SoundBankKind::Sf2,
                    compatOptions);
        Require(pool.ActiveCount() == 1, "Stereo linked zones should aggregate into one root voice");

        std::array<f32, 256> outL{};
        std::array<f32, 256> outR{};
        std::array<f32, 256> reverbL{};
        std::array<f32, 256> reverbR{};
        std::array<f32, 256> chorusL{};
        std::array<f32, 256> chorusR{};
        pool.RenderBlock(outL.data(), outR.data(), reverbL.data(), reverbR.data(), chorusL.data(), chorusR.data(),
                         static_cast<u32>(outL.size()));

        f32 sumL = 0.0f;
        f32 sumR = 0.0f;
        for (size_t i = 0; i < outL.size(); ++i) {
            sumL += outL[i];
            sumR += outR[i];
        }
        char message[160];
        std::snprintf(message, sizeof(message),
                      "Stereo linked left lane should produce non-trivial output (sumL=%f sumR=%f)",
                      sumL, sumR);
        Require(std::fabs(sumL) > 0.1f, message);
        std::snprintf(message, sizeof(message),
                      "Stereo linked right lane should produce non-trivial output (sumL=%f sumR=%f)",
                      sumL, sumR);
        Require(std::fabs(sumR) > 0.1f, message);
        std::snprintf(message, sizeof(message),
                      "Stereo linked samples should render into separate left/right output lanes (sumL=%f sumR=%f)",
                      sumL, sumR);
        Require(sumL > 0.0f && sumR < 0.0f, message);
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

        std::vector<u8> sm24Chunk = { 's', 'm', '2', '4' };
        AppendU32LE(sm24Chunk, 64);
        sm24Chunk.resize(sm24Chunk.size() + 64, 0x7F);
        auto insertSm24 = [&](std::vector<u8>& file) {
            const size_t sdtaListPos = FindListChunk(file, "sdta");
            Require(sdtaListPos != std::numeric_limits<size_t>::max(), "sdta LIST chunk should exist");
            const size_t sdtaPayloadEnd = sdtaListPos + 8 + ReadLE32(file, sdtaListPos + 4);
            file.insert(file.begin() + static_cast<std::ptrdiff_t>(sdtaPayloadEnd), sm24Chunk.begin(), sm24Chunk.end());
            AddChunkSize(file, 4, static_cast<u32>(sm24Chunk.size()));
            AddChunkSize(file, sdtaListPos + 4, static_cast<u32>(sm24Chunk.size()));
        };
        insertSm24(bytes);

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 with sm24 chunk should load");
        Require(!sf2.HasIgnoredSm24(), "HasIgnoredSm24 should be false when valid sm24 data is used");
        Require(sf2.SampleData24() != nullptr, "Valid sm24 should produce a 24-bit sample pool");
        Require(sf2.SampleData24()[0] == ((static_cast<i32>(sf2.SampleData()[0]) << 8) | 0x7F),
            "sm24 low byte should be combined with smpl high word");

        Require(sf2.LoadFromMemory(original.data(), original.size()), "Base SF2 reload failed");
        Require(!sf2.HasIgnoredSm24(), "HasIgnoredSm24 should reset on subsequent loads");
        Require(sf2.SampleData24() == nullptr, "24-bit sample pool should reset on subsequent loads");
    }

    void TestSm24RequiresIfil204() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
        const size_t ifilPos = infoPos + 12;
        Require(std::memcmp(bytes.data() + ifilPos, "ifil", 4) == 0, "ifil chunk should be first in INFO");
        bytes[ifilPos + 8] = 2;
        bytes[ifilPos + 9] = 0;
        bytes[ifilPos + 10] = 1;
        bytes[ifilPos + 11] = 0;

        std::vector<u8> sm24Chunk = { 's', 'm', '2', '4' };
        AppendU32LE(sm24Chunk, 64);
        sm24Chunk.resize(sm24Chunk.size() + 64, 0);
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t sdtaPayloadEnd = sdtaPos + 8 + ReadLE32(bytes, sdtaPos + 4);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(sdtaPayloadEnd), sm24Chunk.begin(), sm24Chunk.end());
        AddChunkSize(bytes, 4, static_cast<u32>(sm24Chunk.size()));
        AddChunkSize(bytes, sdtaPos + 4, static_cast<u32>(sm24Chunk.size()));

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "sm24 before ifil 2.04 should be ignored");
        Require(sf2.HasIgnoredSm24(), "sm24 should still be reported as ignored");
        Require(sf2.SampleData24() == nullptr, "Ignored sm24 should not produce a 24-bit sample pool");
    }

    void TestSm24SizeIgnored() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        std::vector<u8> sm24Chunk = { 's', 'm', '2', '4' };
        AppendU32LE(sm24Chunk, 8);
        sm24Chunk.resize(sm24Chunk.size() + 8, 0);
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t sdtaPayloadEnd = sdtaPos + 8 + ReadLE32(bytes, sdtaPos + 4);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(sdtaPayloadEnd), sm24Chunk.begin(), sm24Chunk.end());
        AddChunkSize(bytes, 4, static_cast<u32>(sm24Chunk.size()));
        AddChunkSize(bytes, sdtaPos + 4, static_cast<u32>(sm24Chunk.size()));

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "sm24 size mismatch should be ignored");
        Require(sf2.HasIgnoredSm24(), "invalid sm24 should still be reported as ignored");
        Require(sf2.SampleData24() == nullptr, "Invalid sm24 should not produce a 24-bit sample pool");
    }

    void TestInvalidTerminalReferencesRejected() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            const size_t pgenPos = FindPdtaChunk(bytes, "pgen");
            Require(pgenPos != std::numeric_limits<size_t>::max(), "pgen chunk should exist");
            const size_t pgenData = pgenPos + 8;
            bytes[pgenData + 2] = 1;
            bytes[pgenData + 3] = 0;

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Preset Instrument terminal reference should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            const size_t igenPos = FindPdtaChunk(bytes, "igen");
            Require(igenPos != std::numeric_limits<size_t>::max(), "igen chunk should exist");
            const size_t igenData = igenPos + 8;
            bytes[igenData + 2] = 1;
            bytes[igenData + 3] = 0;

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Instrument SampleID terminal reference should be rejected");
        }
    }

    void TestRomSampleSkippedWithoutAttachedRomBank() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");

        std::vector<u8> romInfo;
        const std::vector<u8> iromData = { 'R','O','M',0 };
        AppendChunk(romInfo, "irom", iromData);
        std::vector<u8> iverData;
        AppendU16LE(iverData, 2);
        AppendU16LE(iverData, 0);
        AppendChunk(romInfo, "iver", iverData);

        const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd), romInfo.begin(), romInfo.end());
        AddChunkSize(bytes, 4, static_cast<u32>(romInfo.size()));
        AddChunkSize(bytes, infoPos + 4, static_cast<u32>(romInfo.size()));

        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        const size_t sampleTypeOffset = shdrData + 44;
        bytes[sampleTypeOffset] = 0x01;
        bytes[sampleTypeOffset + 1] = 0x80;

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "ROM samples should load");

        std::vector<ResolvedZone> zones;
        Require(!sf2.FindZones(0, 0, 60, 50000, zones, nullptr),
            "ROM-backed instrument zones should be skipped at playback");
    }

    void TestRomSampleUsesAttachedRomBank() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");

        std::vector<u8> romInfo;
        const std::vector<u8> iromData = { 'R','O','M',0 };
        AppendChunk(romInfo, "irom", iromData);
        std::vector<u8> iverData;
        AppendU16LE(iverData, 2);
        AppendU16LE(iverData, 0);
        AppendChunk(romInfo, "iver", iverData);

        const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd), romInfo.begin(), romInfo.end());
        AddChunkSize(bytes, 4, static_cast<u32>(romInfo.size()));
        AddChunkSize(bytes, infoPos + 4, static_cast<u32>(romInfo.size()));

        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        const size_t sampleTypeOffset = shdrData + 44;
        bytes[sampleTypeOffset] = 0x01;
        bytes[sampleTypeOffset + 1] = 0x80;

        std::vector<u8> romBytes = BuildMinimalSf2(config);
        const size_t romSdtaPos = FindListChunk(romBytes, "sdta");
        Require(romSdtaPos != std::numeric_limits<size_t>::max(), "ROM sdta list should exist");
        const size_t romSmplPos = romSdtaPos + 12;
        Require(std::memcmp(romBytes.data() + romSmplPos, "smpl", 4) == 0, "ROM smpl chunk should be first in sdta");
        const size_t romSmplData = romSmplPos + 8;
        romBytes[romSmplData] = 0xD2;
        romBytes[romSmplData + 1] = 0x04;

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "ROM samples should load");
        Require(sf2.LoadRomSampleSourceFromMemory(romBytes.data(), romBytes.size()),
            "ROM sample source should load");

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 50000, nullptr, zones);
        Require(zone.sampleDataOverride != nullptr, "ROM-backed zone should use external sample data");
        Require(zone.sampleDataOverrideCount == 110, "ROM-backed zone should expose external sample count");
        Require(zone.sampleDataOverride[0] == 1234, "ROM-backed zone should read from attached ROM sample data");
        Require(zone.sampleData24Override == nullptr, "16-bit ROM bank should not expose 24-bit override");
    }

    void TestRomMetadataWithoutRomSampleIgnored() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");

        std::vector<u8> romInfo;
        const std::vector<u8> iromData = { 'R','O','M',0 };
        AppendChunk(romInfo, "irom", iromData);
        std::vector<u8> iverData;
        AppendU16LE(iverData, 2);
        AppendU16LE(iverData, 0);
        AppendChunk(romInfo, "iver", iverData);

        const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd), romInfo.begin(), romInfo.end());
        AddChunkSize(bytes, 4, static_cast<u32>(romInfo.size()));
        AddChunkSize(bytes, infoPos + 4, static_cast<u32>(romInfo.size()));

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "ROM INFO metadata without ROM-backed sample headers should be ignored");
    }

    void TestTruncatedSmplChunkRejected() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t smplPos = sdtaPos + 12;
        Require(std::memcmp(bytes.data() + smplPos, "smpl", 4) == 0, "smpl chunk should be first in sdta");

        WriteLE32(bytes, smplPos + 4, ReadLE32(bytes, smplPos + 4) + 2);

        Sf2File sf2;
        Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "smpl chunk that overstates its size should be rejected");
    }

    void TestSampleGuardPaddingRequired() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t smplPos = sdtaPos + 12;
        Require(std::memcmp(bytes.data() + smplPos, "smpl", 4) == 0, "smpl chunk should be first in sdta");
        const size_t smplData = smplPos + 8;
        const size_t firstGuardSample = smplData + static_cast<size_t>(64 * sizeof(i16));
        bytes[firstGuardSample] = 1;
        bytes[firstGuardSample + 1] = 0;

        Sf2File sf2;
        Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "Samples without zero-filled trailing guard points should be rejected");
    }

    void TestSampleLoopGuardPointsRequired() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        WriteLE32(bytes, shdrData + 28, 7);

        Sf2File sf2;
        Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "Samples without eight valid points before loop start should be rejected");
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

    void RemoveInfoSubchunk(std::vector<u8>& bytes, const char id[4]) {
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
        const size_t listData = infoPos + 12;
        const size_t listEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
        for (size_t p = listData; p + 8 <= listEnd;) {
            const u32 chunkSize = ReadLE32(bytes, p + 4);
            const size_t paddedSize = 8 + chunkSize + (chunkSize & 1u);
            if (std::memcmp(bytes.data() + p, id, 4) == 0) {
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(p),
                            bytes.begin() + static_cast<std::ptrdiff_t>(p + paddedSize));
                AddChunkSize(bytes, 4, static_cast<u32>(-static_cast<i32>(paddedSize)));
                AddChunkSize(bytes, infoPos + 4, static_cast<u32>(-static_cast<i32>(paddedSize)));
                return;
            }
            p += paddedSize;
        }
        Require(false, "Requested INFO subchunk should exist");
    }

    void ReplaceInfoSubchunkData(std::vector<u8>& bytes, const char id[4], const std::vector<u8>& data) {
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
        const size_t listData = infoPos + 12;
        const size_t listEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
        for (size_t p = listData; p + 8 <= listEnd;) {
            const u32 chunkSize = ReadLE32(bytes, p + 4);
            const size_t paddedSize = 8 + chunkSize + (chunkSize & 1u);
            if (std::memcmp(bytes.data() + p, id, 4) == 0) {
                std::vector<u8> replacement;
                AppendChunk(replacement, id, data);
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(p),
                            bytes.begin() + static_cast<std::ptrdiff_t>(p + paddedSize));
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(p), replacement.begin(), replacement.end());
                const i32 delta = static_cast<i32>(replacement.size()) - static_cast<i32>(paddedSize);
                AddChunkSize(bytes, 4, static_cast<u32>(delta));
                AddChunkSize(bytes, infoPos + 4, static_cast<u32>(delta));
                return;
            }
            p += paddedSize;
        }
        Require(false, "Requested INFO subchunk should exist");
    }

    void RemoveSdtaSubchunk(std::vector<u8>& bytes, const char id[4]) {
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t listData = sdtaPos + 12;
        const size_t listEnd = sdtaPos + 8 + ReadLE32(bytes, sdtaPos + 4);
        for (size_t p = listData; p + 8 <= listEnd;) {
            const u32 chunkSize = ReadLE32(bytes, p + 4);
            const size_t paddedSize = 8 + chunkSize + (chunkSize & 1u);
            if (std::memcmp(bytes.data() + p, id, 4) == 0) {
                bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(p),
                            bytes.begin() + static_cast<std::ptrdiff_t>(p + paddedSize));
                AddChunkSize(bytes, 4, static_cast<u32>(-static_cast<i32>(paddedSize)));
                AddChunkSize(bytes, sdtaPos + 4, static_cast<u32>(-static_cast<i32>(paddedSize)));
                return;
            }
            p += paddedSize;
        }
        Require(false, "Requested sdta subchunk should exist");
    }

    void DuplicateListSubchunk(std::vector<u8>& bytes, const char listType[4], const char id[4]) {
        const size_t listPos = FindListChunk(bytes, listType);
        Require(listPos != std::numeric_limits<size_t>::max(), "LIST chunk should exist");
        const size_t listData = listPos + 12;
        const size_t listEnd = listPos + 8 + ReadLE32(bytes, listPos + 4);
        for (size_t p = listData; p + 8 <= listEnd;) {
            const u32 chunkSize = ReadLE32(bytes, p + 4);
            const size_t paddedSize = 8 + chunkSize + (chunkSize & 1u);
            if (std::memcmp(bytes.data() + p, id, 4) == 0) {
                const std::vector<u8> duplicate(bytes.begin() + static_cast<std::ptrdiff_t>(p),
                                                bytes.begin() + static_cast<std::ptrdiff_t>(p + paddedSize));
                bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(listEnd), duplicate.begin(), duplicate.end());
                AddChunkSize(bytes, 4, static_cast<u32>(duplicate.size()));
                AddChunkSize(bytes, listPos + 4, static_cast<u32>(duplicate.size()));
                return;
            }
            p += paddedSize;
        }
        Require(false, "Requested subchunk should exist for duplication");
    }

    void DuplicateTopLevelList(std::vector<u8>& bytes, const char listType[4]) {
        const size_t listPos = FindListChunk(bytes, listType);
        Require(listPos != std::numeric_limits<size_t>::max(), "Top-level LIST chunk should exist");
        const u32 listSize = ReadLE32(bytes, listPos + 4);
        const size_t paddedSize = 8 + listSize + (listSize & 1u);
        const std::vector<u8> duplicate(bytes.begin() + static_cast<std::ptrdiff_t>(listPos),
                                        bytes.begin() + static_cast<std::ptrdiff_t>(listPos + paddedSize));
        bytes.insert(bytes.end(), duplicate.begin(), duplicate.end());
        AddChunkSize(bytes, 4, static_cast<u32>(duplicate.size()));
    }

    void TestMissingMandatoryInfoChunksAccepted() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            RemoveInfoSubchunk(bytes, "isng");

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 missing isng should load");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            RemoveInfoSubchunk(bytes, "INAM");

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 missing INAM should load");
        }
    }

    void TestMalformedInfoStringsIgnored() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            RemoveInfoSubchunk(bytes, "isng");
            const size_t infoPos = FindListChunk(bytes, "INFO");
            Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
            std::vector<u8> badIsng;
            AppendChunk(badIsng, "isng", { 'B','A','D' });
            const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd),
                         badIsng.begin(), badIsng.end());
            AddChunkSize(bytes, 4, static_cast<u32>(badIsng.size()));
            AddChunkSize(bytes, infoPos + 4, static_cast<u32>(badIsng.size()));

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Unterminated isng should be ignored rather than rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            RemoveInfoSubchunk(bytes, "INAM");
            const size_t infoPos = FindListChunk(bytes, "INFO");
            Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
            std::vector<u8> badInam;
            AppendChunk(badInam, "INAM", { 'N','a','m','e', 0xFFu, 0 });
            const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd),
                         badInam.begin(), badInam.end());
            AddChunkSize(bytes, 4, static_cast<u32>(badInam.size()));
            AddChunkSize(bytes, infoPos + 4, static_cast<u32>(badInam.size()));

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Non-ASCII INAM should be ignored rather than rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            const size_t infoPos = FindListChunk(bytes, "INFO");
            Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
            std::vector<u8> invalidIver;
            AppendChunk(invalidIver, "iver", { 2, 0 });
            const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd),
                         invalidIver.begin(), invalidIver.end());
            AddChunkSize(bytes, 4, static_cast<u32>(invalidIver.size()));
            AddChunkSize(bytes, infoPos + 4, static_cast<u32>(invalidIver.size()));

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Invalid iver size without ROM samples should be ignored rather than rejected");
        }
    }

    void TestRomSamplesRequireValidRomMetadata() {
        auto makeRomSampleBank = [&]() -> std::vector<u8> {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            const size_t infoPos = FindListChunk(bytes, "INFO");
            Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");

            std::vector<u8> romInfo;
            const std::vector<u8> iromData = { 'R','O','M',0 };
            AppendChunk(romInfo, "irom", iromData);
            std::vector<u8> iverData;
            AppendU16LE(iverData, 2);
            AppendU16LE(iverData, 0);
            AppendChunk(romInfo, "iver", iverData);

            const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd), romInfo.begin(), romInfo.end());
            AddChunkSize(bytes, 4, static_cast<u32>(romInfo.size()));
            AddChunkSize(bytes, infoPos + 4, static_cast<u32>(romInfo.size()));

            const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
            Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
            const size_t sampleTypeOffset = shdrPos + 8 + 44;
            bytes[sampleTypeOffset] = 0x01;
            bytes[sampleTypeOffset + 1] = 0x80;
            return bytes;
        };

        MinimalSf2Config config;
        std::vector<u8> romSourceBytes = BuildMinimalSf2(config);

        {
            std::vector<u8> bytes = makeRomSampleBank();
            RemoveInfoSubchunk(bytes, "irom");
            const size_t infoPos = FindListChunk(bytes, "INFO");
            Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
            std::vector<u8> badIrom;
            AppendChunk(badIrom, "irom", { 'R','O','M' });
            const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd),
                         badIrom.begin(), badIrom.end());
            AddChunkSize(bytes, 4, static_cast<u32>(badIrom.size()));
            AddChunkSize(bytes, infoPos + 4, static_cast<u32>(badIrom.size()));

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "Malformed irom metadata should not reject load");
            Require(sf2.LoadRomSampleSourceFromMemory(romSourceBytes.data(), romSourceBytes.size()),
                "ROM source should still parse");

            std::vector<ResolvedZone> zones;
            Require(!sf2.FindZones(0, 0, 60, 50000, zones, nullptr),
                "ROM-backed zones should not resolve when irom is invalid");
        }

        {
            std::vector<u8> bytes = makeRomSampleBank();
            RemoveInfoSubchunk(bytes, "iver");
            const size_t infoPos = FindListChunk(bytes, "INFO");
            Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
            std::vector<u8> badIver;
            AppendChunk(badIver, "iver", { 2, 0 });
            const size_t infoPayloadEnd = infoPos + 8 + ReadLE32(bytes, infoPos + 4);
            bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(infoPayloadEnd),
                         badIver.begin(), badIver.end());
            AddChunkSize(bytes, 4, static_cast<u32>(badIver.size()));
            AddChunkSize(bytes, infoPos + 4, static_cast<u32>(badIver.size()));

            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "Malformed iver metadata should not reject load");
            Require(sf2.LoadRomSampleSourceFromMemory(romSourceBytes.data(), romSourceBytes.size()),
                "ROM source should still parse");

            std::vector<ResolvedZone> zones;
            Require(!sf2.FindZones(0, 0, 60, 50000, zones, nullptr),
                "ROM-backed zones should not resolve when iver is invalid");
        }
    }

    void InsertUnknownTopLevelChunk(std::vector<u8>& bytes) {
        std::vector<u8> chunk = { 'J','U','N','K' };
        AppendU32LE(chunk, 4);
        chunk.insert(chunk.end(), { 0, 0, 0, 0 });
        bytes.insert(bytes.end(), chunk.begin(), chunk.end());
        AddChunkSize(bytes, 4, static_cast<u32>(chunk.size()));
    }

    void InsertUnknownSdtaSubchunk(std::vector<u8>& bytes) {
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t sdtaPayloadEnd = sdtaPos + 8 + ReadLE32(bytes, sdtaPos + 4);
        std::vector<u8> chunk = { 'b','a','d','!' };
        AppendU32LE(chunk, 2);
        chunk.push_back(0);
        chunk.push_back(0);
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(sdtaPayloadEnd), chunk.begin(), chunk.end());
        AddChunkSize(bytes, 4, static_cast<u32>(chunk.size()));
        AddChunkSize(bytes, sdtaPos + 4, static_cast<u32>(chunk.size()));
    }

    void ReplacePdtaSubchunkId(std::vector<u8>& bytes, const char oldId[4], const char newId[4]) {
        const size_t pos = FindPdtaChunk(bytes, oldId);
        Require(pos != std::numeric_limits<size_t>::max(), "pdta subchunk should exist");
        std::memcpy(bytes.data() + pos, newId, 4);
    }

    void SwapTopLevelLists(std::vector<u8>& bytes, const char firstType[4], const char secondType[4]) {
        struct ChunkSlice {
            size_t pos = 0;
            size_t size = 0;
        };
        auto getChunkSlice = [&](const char listType[4]) -> ChunkSlice {
            const size_t pos = FindListChunk(bytes, listType);
            Require(pos != std::numeric_limits<size_t>::max(), "Top-level LIST chunk should exist");
            const u32 listSize = ReadLE32(bytes, pos + 4);
            return { pos, static_cast<size_t>(8 + listSize + (listSize & 1u)) };
        };

        const ChunkSlice a = getChunkSlice(firstType);
        const ChunkSlice b = getChunkSlice(secondType);
        Require(a.pos < b.pos, "Expected chunk order for swap helper");

        const std::vector<u8> bytesA(bytes.begin() + static_cast<std::ptrdiff_t>(a.pos),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(a.pos + a.size));
        const std::vector<u8> bytesB(bytes.begin() + static_cast<std::ptrdiff_t>(b.pos),
                                     bytes.begin() + static_cast<std::ptrdiff_t>(b.pos + b.size));

        bytes.erase(bytes.begin() + static_cast<std::ptrdiff_t>(a.pos),
                    bytes.begin() + static_cast<std::ptrdiff_t>(b.pos + b.size));
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(a.pos), bytesB.begin(), bytesB.end());
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(a.pos + bytesB.size()), bytesA.begin(), bytesA.end());
    }

    void TestUnknownChunksRejected() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            InsertUnknownTopLevelChunk(bytes);

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Unknown top-level chunk should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            InsertUnknownSdtaSubchunk(bytes);

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Unknown sdta subchunk should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            ReplacePdtaSubchunkId(bytes, "pmod", "bad!");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Unknown pdta subchunk should be rejected");
        }
    }

    void TestChunkOrderingRejected() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            SwapTopLevelLists(bytes, "INFO", "sdta");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "Top-level LIST chunks out of order should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            ReplacePdtaSubchunkId(bytes, "pbag", "inst");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()),
                "pdta subchunks out of order should be rejected");
        }
    }

    void TestIllegalOriginalPitchFallsBackTo60() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        const size_t originalPitchOffset = shdrData + 36;
        bytes[originalPitchOffset] = 255;

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 should load with illegal originalPitch");
        Require(sf2.SampleHeaders(0)->originalPitch == 60, "Illegal originalPitch should fall back to 60");
    }

    void TestMissingSmplRejected() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        RemoveSdtaSubchunk(bytes, "smpl");

        Sf2File sf2;
        Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 missing smpl should be rejected");
    }

    void TestDuplicateMandatoryChunksRejected() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            DuplicateListSubchunk(bytes, "INFO", "ifil");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 duplicate ifil should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            DuplicateListSubchunk(bytes, "sdta", "smpl");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 duplicate smpl should be rejected");
        }
    }

    void TestDuplicateTopLevelListsRejected() {
        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            DuplicateTopLevelList(bytes, "INFO");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 duplicate INFO LIST should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            DuplicateTopLevelList(bytes, "sdta");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 duplicate sdta LIST should be rejected");
        }

        {
            MinimalSf2Config config;
            std::vector<u8> bytes = BuildMinimalSf2(config);
            DuplicateTopLevelList(bytes, "pdta");

            Sf2File sf2;
            Require(!sf2.LoadFromMemory(bytes.data(), bytes.size()), "SF2 duplicate pdta LIST should be rejected");
        }
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
    g_currentTestName = "TestDuplicateModulatorsUseLastDefinition";
    TestDuplicateModulatorsUseLastDefinition();
    g_currentTestName = "TestLinkedModulatorsFeedTargetSource";
    TestLinkedModulatorsFeedTargetSource();
    g_currentTestName = "TestUnsupportedTransformReporting";
    TestUnsupportedTransformReporting();
    g_currentTestName = "TestUnsupportedAmountSourceIgnored";
    TestUnsupportedAmountSourceIgnored();
    g_currentTestName = "TestEffectsSendMixPolicy";
    TestEffectsSendMixPolicy();
    g_currentTestName = "TestSf2PitchPrecedence";
    TestSf2PitchPrecedence();
    g_currentTestName = "TestEnvelopePitchAndKeynumScaling";
    TestEnvelopePitchAndKeynumScaling();
    g_currentTestName = "TestEnvelopeReleaseRecalculation";
    TestEnvelopeReleaseRecalculation();
    g_currentTestName = "TestFilterAndLfoInitialization";
    TestFilterAndLfoInitialization();
    g_currentTestName = "TestPressureSources";
    TestPressureSources();
    g_currentTestName = "TestPitchWheelSensitivityAmountSource";
    TestPitchWheelSensitivityAmountSource();
    g_currentTestName = "TestRemainingDefaultModulators";
    TestRemainingDefaultModulators();
    g_currentTestName = "TestDefaultModulatorSupersedeSemantics";
    TestDefaultModulatorSupersedeSemantics();
    g_currentTestName = "TestStereoSampleLinks";
    TestStereoSampleLinks();
    g_currentTestName = "TestSourceCurvesSupport";
    TestSourceCurvesSupport();
    g_currentTestName = "TestVelocityZoneBoundary";
    TestVelocityZoneBoundary();
    g_currentTestName = "TestSm24Detection";
    TestSm24Detection();
    g_currentTestName = "TestSm24RequiresIfil204";
    TestSm24RequiresIfil204();
    g_currentTestName = "TestSm24SizeIgnored";
    TestSm24SizeIgnored();
    g_currentTestName = "TestBagIndexHelpersSkipGlobalZones";
    TestBagIndexHelpersSkipGlobalZones();
    g_currentTestName = "TestMissingIfilRejected";
    TestMissingIfilRejected();
    g_currentTestName = "TestMissingMandatoryInfoChunksAccepted";
    TestMissingMandatoryInfoChunksAccepted();
    g_currentTestName = "TestMalformedInfoStringsIgnored";
    TestMalformedInfoStringsIgnored();
    g_currentTestName = "TestDuplicateMandatoryChunksRejected";
    TestDuplicateMandatoryChunksRejected();
    g_currentTestName = "TestDuplicateTopLevelListsRejected";
    TestDuplicateTopLevelListsRejected();
    g_currentTestName = "TestUnknownChunksRejected";
    TestUnknownChunksRejected();
    g_currentTestName = "TestChunkOrderingRejected";
    TestChunkOrderingRejected();
    g_currentTestName = "TestInvalidTerminalReferencesRejected";
    TestInvalidTerminalReferencesRejected();
    g_currentTestName = "TestRomSampleSkippedWithoutAttachedRomBank";
    TestRomSampleSkippedWithoutAttachedRomBank();
    g_currentTestName = "TestRomSampleUsesAttachedRomBank";
    TestRomSampleUsesAttachedRomBank();
    g_currentTestName = "TestRomMetadataWithoutRomSampleIgnored";
    TestRomMetadataWithoutRomSampleIgnored();
    g_currentTestName = "TestRomSamplesRequireValidRomMetadata";
    TestRomSamplesRequireValidRomMetadata();
    g_currentTestName = "TestIllegalOriginalPitchFallsBackTo60";
    TestIllegalOriginalPitchFallsBackTo60();
    g_currentTestName = "TestTruncatedSmplChunkRejected";
    TestTruncatedSmplChunkRejected();
    g_currentTestName = "TestSampleGuardPaddingRequired";
    TestSampleGuardPaddingRequired();
    g_currentTestName = "TestSampleLoopGuardPointsRequired";
    TestSampleLoopGuardPointsRequired();
    g_currentTestName = "TestMissingSmplRejected";
    TestMissingSmplRejected();
    g_currentTestName = "TestNonMonotonicPbagRejected";
    TestNonMonotonicPbagRejected();
    std::printf("sf2_compliance: all tests passed\n");
    return 0;
}
