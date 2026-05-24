#include "../src/sf2/Sf2File.h"
#include "../src/sf2/Sf2ModulatorResolver.h"
#include "../src/sf2/Sf2Types.h"
#include "../src/synth/Channel.h"
#include "../src/synth/Interpolator.h"
#include "../src/synth/Synthesizer.h"
#include "../src/synth/Voice.h"
#include "../src/synth/VoicePool.h"
#include "../include/XArkMidiEngine.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace XArkMidi;

namespace {

    struct VoicePoolProbe {
        Voice voices_[MAX_VOICES];
        u32 nextNoteId_;
        std::deque<u32> noteQueue_[MIDI_CHANNEL_COUNT][128];
        std::array<u16, MAX_VOICES> activeIndices_;
        std::array<i16, MAX_VOICES> activeSlots_;
        u16 activeCount_;
    };

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

    void AppendU16BE(std::vector<u8>& out, u16 value) {
        out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
        out.push_back(static_cast<u8>(value & 0xFFu));
    }

    void AppendU32BE(std::vector<u8>& out, u32 value) {
        out.push_back(static_cast<u8>((value >> 24) & 0xFFu));
        out.push_back(static_cast<u8>((value >> 16) & 0xFFu));
        out.push_back(static_cast<u8>((value >> 8) & 0xFFu));
        out.push_back(static_cast<u8>(value & 0xFFu));
    }

    void AppendVarLen(std::vector<u8>& out, u32 value) {
        u8 bytes[5]{};
        int count = 1;
        bytes[4] = static_cast<u8>(value & 0x7Fu);
        while ((value >>= 7) != 0) {
            bytes[4 - count] = static_cast<u8>((value & 0x7Fu) | 0x80u);
            ++count;
        }
        out.insert(out.end(), bytes + 5 - count, bytes + 5);
    }

    void AppendChunk(std::vector<u8>& out, const char id[4], const std::vector<u8>& data) {
        out.insert(out.end(), id, id + 4);
        AppendU32LE(out, static_cast<u32>(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        if ((data.size() & 1u) != 0u) {
            out.push_back(0);
        }
    }

    std::vector<u8> BuildSingleNoteMidi() {
        std::vector<u8> track;
        AppendVarLen(track, 0);
        track.push_back(0x90);
        track.push_back(60);
        track.push_back(96);
        AppendVarLen(track, 48);
        track.push_back(0x80);
        track.push_back(60);
        track.push_back(0);
        AppendVarLen(track, 0);
        track.push_back(0xFF);
        track.push_back(0x2F);
        track.push_back(0);

        std::vector<u8> midi;
        midi.insert(midi.end(), { 'M','T','h','d' });
        AppendU32BE(midi, 6);
        AppendU16BE(midi, 0);
        AppendU16BE(midi, 1);
        AppendU16BE(midi, 96);
        midi.insert(midi.end(), { 'M','T','r','k' });
        AppendU32BE(midi, static_cast<u32>(track.size()));
        midi.insert(midi.end(), track.begin(), track.end());
        return midi;
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

    std::vector<u8> BuildStereoLinkedSf2WithLengths(u32 leftLength, u32 rightLength) {
        std::vector<u8> infoPayload;
        std::vector<u8> ifil;
        AppendU16LE(ifil, 2);
        AppendU16LE(ifil, 4);
        AppendChunk(infoPayload, "ifil", ifil);
        const std::vector<u8> isng = { 'E','M','U','8','0','0','0', 0 };
        AppendChunk(infoPayload, "isng", isng);
        const std::vector<u8> inam = { 'S','t','e','r','e','o','L','e','n', 0 };
        AppendChunk(infoPayload, "INAM", inam);

        std::vector<u8> sdtaPayload;
        std::vector<u8> smpl;
        for (u32 i = 0; i < leftLength; ++i) {
            AppendI16LE(smpl, static_cast<i16>(1000 + static_cast<i32>(i % 128u) * 32));
        }
        for (int i = 0; i < 46; ++i) {
            AppendI16LE(smpl, 0);
        }
        for (u32 i = 0; i < rightLength; ++i) {
            AppendI16LE(smpl, static_cast<i16>(-1000 - static_cast<i32>(i % 128u) * 32));
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
        appendPresetHeader("StereoLen", 0, 0, 0);
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
        auto appendSampleHeader = [&](const char* name, u32 start, u32 end, u16 sampleLink, u16 sampleType) {
            char padded[20] = {};
            std::strncpy(padded, name, sizeof(padded));
            shdr.insert(shdr.end(), padded, padded + 20);
            AppendU32LE(shdr, start);
            AppendU32LE(shdr, end);
            AppendU32LE(shdr, start);
            AppendU32LE(shdr, end);
            AppendU32LE(shdr, 44100);
            shdr.push_back(60);
            shdr.push_back(0);
            AppendU16LE(shdr, sampleLink);
            AppendU16LE(shdr, sampleType);
        };
        const u32 leftStart = 0;
        const u32 leftEnd = leftStart + leftLength;
        const u32 rightStart = leftEnd + 46;
        const u32 rightEnd = rightStart + rightLength;
        appendSampleHeader("LeftLong", leftStart, leftEnd, 1, 4);
        appendSampleHeader("RightShort", rightStart, rightEnd, 0, 2);
        appendSampleHeader("EOS", 0, 0, 0, 1);
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

    std::vector<u8> BuildLayeredSameSampleSf2(i16 panA, i16 panB, u16 exclusiveClassA = 0, u16 exclusiveClassB = 0) {
        std::vector<u8> infoPayload;
        std::vector<u8> ifil;
        AppendU16LE(ifil, 2);
        AppendU16LE(ifil, 4);
        AppendChunk(infoPayload, "ifil", ifil);
        const std::vector<u8> isng = { 'E','M','U','8','0','0','0', 0 };
        AppendChunk(infoPayload, "isng", isng);
        const std::vector<u8> inam = { 'L','a','y','e','r','e','d', 0 };
        AppendChunk(infoPayload, "INAM", inam);

        std::vector<u8> sdtaPayload;
        std::vector<u8> smpl;
        for (int i = 0; i < 64; ++i) {
            AppendI16LE(smpl, static_cast<i16>(std::lround(std::sin((static_cast<double>(i) / 64.0) * 6.283185307179586) * 12000.0)));
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
        appendPresetHeader("Layered", 0, 0, 0);
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
        appendInst("LayerInst", 0);
        appendInst("EOI", 2);
        AppendChunk(pdtaPayload, "inst", inst);

        std::vector<u8> ibag;
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 3);
        AppendU16LE(ibag, 0);
        AppendU16LE(ibag, 6);
        AppendU16LE(ibag, 0);
        AppendChunk(pdtaPayload, "ibag", ibag);

        std::vector<u8> imod;
        for (int i = 0; i < 5; ++i) {
            AppendU16LE(imod, 0);
        }
        AppendChunk(pdtaPayload, "imod", imod);

        std::vector<u8> igen;
        auto appendGen = [&](u16 oper, i16 value) {
            AppendU16LE(igen, oper);
            AppendI16LE(igen, value);
        };
        appendGen(GEN_Pan, panA);
        appendGen(GEN_ExclusiveClass, static_cast<i16>(exclusiveClassA));
        AppendU16LE(igen, GEN_SampleID);
        AppendU16LE(igen, 0);
        appendGen(GEN_Pan, panB);
        appendGen(GEN_ExclusiveClass, static_cast<i16>(exclusiveClassB));
        AppendU16LE(igen, GEN_SampleID);
        AppendU16LE(igen, 0);
        AppendU16LE(igen, 0);
        AppendU16LE(igen, 0);
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

    i32 ExpectedVelocityFilterCutoff(i32 baseFc, u16 velocity) {
        const i32 delta = static_cast<i32>(std::lround(
            -2400.0 * (1.0 - static_cast<double>(velocity) / 65535.0)));
        return std::clamp(baseFc + delta, 1500, 13500);
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

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        ctx.applySf2ChannelDefaults = true;
        ctx.applySf2VelocityToInitialAttenuation = true;

        std::vector<ResolvedZone> zones;
        if (!sf2.FindZones(0, 0, 60, 50000, zones, &ctx)) {
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
        ctx.applySf2ChannelDefaults = true;
        ctx.applySf2VelocityToInitialAttenuation = true;
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

    void TestSf2ModulatorResolverDefaultTableAndDestinations() {
        const std::vector<SFModList> defaults = GetSf2ImplicitDefaultModulators();
        Require(defaults.size() == 10, "SF2 implicit default modulator table should contain all spec defaults");
        Require(defaults[0].sfModSrcOper == 0x0502u, "Default velocity attenuation source should be concave negative unipolar");
        Require(defaults[0].sfModDestOper == GEN_InitialAttenuation, "Default velocity attenuation destination should be initialAttenuation");
        Require(defaults[9].sfModSrcOper == 0x020Eu, "Default pitch-wheel source should be positive bipolar pitch wheel");
        Require(defaults[9].sfModAmtSrcOper == 0x0010u, "Default pitch-wheel amount source should be pitch wheel sensitivity");

        Require(IsSf2SpecValueGeneratorDestination(GEN_Pan), "Pan should be a spec Value Generator mod destination");
        Require(!IsSf2SpecValueGeneratorDestination(GEN_StartAddrsOffset), "Sample offset generators should not be mod destinations");
        Require(!IsSf2SpecValueGeneratorDestination(GEN_SampleModes), "sampleModes should not be a mod destination");
        Require(!IsSf2SpecValueGeneratorDestination(GEN_SampleID), "sampleID should not be a mod destination");
        Require(!IsSf2SpecValueGeneratorDestination(GEN_ExclusiveClass), "exclusiveClass should not be a mod destination");
        Require(!IsSf2SpecValueGeneratorDestination(59), "Generator 59 unused5 should not be a mod destination");
    }

    void TestSf2ModulatorResolverDestinationClasses() {
        Require(ClassifySf2ModulatorDestination(GEN_InitialAttenuation) == Sf2ModulatorDestinationClass::Mix,
            "InitialAttenuation should be classified as mix refresh");
        Require(ClassifySf2ModulatorDestination(GEN_InitialFilterFc) == Sf2ModulatorDestinationClass::Filter,
            "InitialFilterFc should be classified as filter refresh");
        Require(ClassifySf2ModulatorDestination(GEN_CoarseTune) == Sf2ModulatorDestinationClass::Pitch,
            "CoarseTune should be classified as pitch refresh");
        Require(ClassifySf2ModulatorDestination(GEN_AttackVolEnv) == Sf2ModulatorDestinationClass::Envelope,
            "Volume envelope attack should be classified as envelope refresh");
        Require(ClassifySf2ModulatorDestination(GEN_FreqModLFO) == Sf2ModulatorDestinationClass::Lfo,
            "Mod LFO frequency should be classified as LFO refresh");
        Require(ClassifySf2ModulatorDestination(GEN_StartAddrsOffset) == Sf2ModulatorDestinationClass::Ignored,
            "Sample offset generators should remain ignored for modulator refresh");
        Require(ClassifySf2ModulatorDestination(GEN_SampleModes) == Sf2ModulatorDestinationClass::Ignored,
            "sampleModes should remain ignored for modulator refresh");
        Require(ClassifySf2ModulatorDestination(59) == Sf2ModulatorDestinationClass::Ignored,
            "unused5 should remain ignored for modulator refresh");
    }

    void TestSf2ModulatorResolverHierarchySemantics() {
        const SFModList instReplace = MakeMod(0x0502u, GEN_InitialAttenuation, 100, 0, 0);
        const SFModList presetAdd = MakeMod(0x0502u, GEN_InitialAttenuation, 25, 0, 0);
        std::vector<Sf2ModulatorZone> zones;
        zones.push_back({ Sf2ModulatorLevel::InstrumentLocal, &instReplace, 1 });
        zones.push_back({ Sf2ModulatorLevel::PresetLocal, &presetAdd, 1 });

        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators(zones, true);

        int identicalCount = 0;
        bool sawInstrumentReplacement = false;
        bool sawPresetAdd = false;
        const Sf2ModulatorIdentity target = MakeSf2ModulatorIdentity(instReplace);
        for (const auto& mod : resolved) {
            if (!(MakeSf2ModulatorIdentity(mod.mod) == target)) {
                continue;
            }
            ++identicalCount;
            sawInstrumentReplacement |=
                mod.level == Sf2ModulatorLevel::InstrumentLocal && mod.mod.modAmount == 100 &&
                mod.participatesInDefaultSuppression;
            sawPresetAdd |=
                mod.level == Sf2ModulatorLevel::PresetLocal && mod.mod.modAmount == 25 &&
                !mod.participatesInDefaultSuppression;
        }

        Require(identicalCount == 2, "Instrument identical mod should replace default while preset identical mod adds");
        Require(sawInstrumentReplacement, "Instrument local mod should replace the implicit default modulator");
        Require(sawPresetAdd, "Preset local identical mod should add instead of suppressing instrument/default");
    }

    void TestSf2ModulatorResolverInvalidModsDoNotSuppressDefaults() {
        const SFModList invalidAmountSource = MakeMod(0x0502u, GEN_InitialAttenuation, 100, 0x0080u, 0);
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, &invalidAmountSource, 1 };

        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, true);
        int defaultCount = 0;
        for (const auto& mod : resolved) {
            if (mod.mod.sfModSrcOper == 0x0502u &&
                mod.mod.sfModDestOper == GEN_InitialAttenuation &&
                mod.mod.modAmount == 960) {
                ++defaultCount;
            }
            Require(mod.mod.modAmount != 100, "Invalid amount source mod should not enter the effective set");
        }
        Require(defaultCount == 1, "Invalid identical instrument mod should not suppress the implicit default");
    }

    void TestSf2ModulatorResolverSourceAndTransformRules() {
        Require(!IsSf2SpecModulatorSourceDefinition(0x0080u, false), "CC0 should be an illegal modulator source");
        Require(!IsSf2SpecModulatorSourceDefinition(0x00A1u, false), "CC33 LSB source should be reserved");
        Require(!IsSf2SpecModulatorSourceDefinition(0x00F8u, false), "CC120..127 should be illegal modulator sources");
        Require(!IsSf2SpecModulatorSourceDefinition(static_cast<u16>(2u | (4u << 10)), false),
            "Unknown source curve types should be invalid");

        const SFModList mod = MakeMod(0x028Au, GEN_Pan, -100, 0x0081u, 2);
        const Sf2ResolvedModulator resolved{ mod, Sf2ModulatorLevel::InstrumentLocal,
            Sf2ModulatorValidity::Valid, true, Sf2ModulatorDependency::None };
        ModulatorContext ctx{};
        ctx.ccValues[1] = 64;
        ctx.ccValues[10] = 0;

        const std::vector<Sf2ModulatorEvaluation> evaluated = EvaluateSf2Modulators({ resolved }, 60, 65535, &ctx);
        Require(evaluated.size() == 1, "Valid resolver modulator should evaluate");
        Require(evaluated[0].amount == 50, "Transform should apply after amount * source * amountSource");
        Require((static_cast<u16>(evaluated[0].dependencies) &
                 static_cast<u16>(Sf2ModulatorDependency::ChannelController)) != 0,
            "Amount source and primary source should contribute controller dependencies");
    }

    void TestSf2ModulatorResolverLinkCyclesAreIgnored() {
        const SFModList cycleMods[] = {
            MakeMod(0x0081u, 0x8001u, 100, 0, 0),
            MakeMod(0x0082u, 0x8000u, 100, 0, 0),
        };
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, cycleMods, 2 };
        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, false);
        Require(resolved.empty(), "Modulators in a link cycle should be ignored");
    }

    void TestSf2ModulatorResolverLinkedInputsEvaluate() {
        const SFModList mods[] = {
            MakeMod(0, 0x8001u, 100, 0, 0),
            MakeMod(127, GEN_Pan, 1, 0, 0),
        };
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, mods, 2 };
        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, false);
        const std::vector<Sf2ModulatorEvaluation> evaluated = EvaluateSf2Modulators(resolved, 60, 65535, nullptr);
        Require(evaluated.size() == 1, "Linked modulator input should evaluate through the target source");
        Require(evaluated[0].destination == GEN_Pan && evaluated[0].amount == 100,
            "Linked input output should feed the target source");
    }

    void TestSf2ModulatorResolverLinkedChains() {
        // Test chain: Mod 0 -> Mod 1 -> Mod 2 -> GEN_Pan
        // Mod 0: Const 100 -> Dest 0x8001
        // Mod 1: Link (Mod 0) * 2 -> Dest 0x8002
        // Mod 2: Link (Mod 1) * 3 -> GEN_Pan
        const SFModList mods[] = {
            MakeMod(0, 0x8001u, 100, 0, 0),
            MakeMod(127, 0x8002u, 2, 0, 0),
            MakeMod(127, GEN_Pan, 3, 0, 0),
        };
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, mods, 3 };
        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, false);

        ModulatorContext ctx{};
        ctx.useSf2SpecModulatorResolver = true;
        const std::vector<Sf2ModulatorEvaluation> evaluated = EvaluateSf2Modulators(resolved, 60, 65535, &ctx);

        // Mod 0: 100
        // Mod 1: 100 * 2 = 200
        // Mod 2: 200 * 3 = 600
        Require(evaluated.size() == 1, "Chain evaluation should result in one final output");
        Require(evaluated[0].destination == GEN_Pan, "Final destination should be GEN_Pan");
        Require(evaluated[0].amount == 600, "Chain evaluation should correctly multiply amounts (100 * 2 * 3 = 600)");
    }

    void TestSf2ModulatorResolverChainWithInvalidNodeIsIgnored() {
        // Chain: Mod0 -> [Mod1 invalid] -> Mod2 -> GEN_Pan
        // Mod 0: Const 100 -> Dest 0x8001
        // Mod 1: Link(Mod 0) * 2 -> Dest 0x8002 (invalid amount source 128)
        // Mod 2: Link(Mod 1) * 3 -> GEN_Pan
        const SFModList mods[] = {
            MakeMod(0, 0x8001u, 100, 0, 0),
            MakeMod(127, 0x8002u, 2, 128, 0),
            MakeMod(127, GEN_Pan, 3, 0, 0),
        };
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, mods, 3 };
        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, false);

        ModulatorContext ctx{};
        ctx.useSf2SpecModulatorResolver = true;
        const std::vector<Sf2ModulatorEvaluation> evaluated = EvaluateSf2Modulators(resolved, 60, 65535, &ctx);

        Require(evaluated.empty(), "Chain with invalid intermediate node should produce no output");
    }

    void TestSf2SpecResolverOptInAppliesImplicitDefaults() {
        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        ctx.useSf2SpecModulatorResolver = true;

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 32768, &ctx, zones);
        const i32 expectedFilter = std::clamp(
            13500 + static_cast<i32>(std::lround(-2400.0 * (1.0 - (32768.0 / 65536.0)))),
            1500, 13500);
        Require(zone.generators[GEN_InitialFilterFc] == expectedFilter,
            "Spec resolver opt-in should apply implicit velocity->filter default without legacy flags");
    }

    void TestSf2SpecResolverOptInPresetAddsToInstrument() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(0, GEN_Pan, 100, 0, 0));
        config.presetMods.push_back(MakeMod(0, GEN_Pan, 25, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        ctx.useSf2SpecModulatorResolver = true;

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
        Require(zone.generators[GEN_Pan] == 125,
            "Spec resolver opt-in should add preset modulators to instrument modulators");
    }

    void TestSf2SpecResolverPitchWheelDefaultUsesSensitivityCents() {
        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        ctx.useSf2SpecModulatorResolver = true;
        ctx.pitchBend = 8191;
        ctx.pitchWheelSensitivitySemitones = 2;
        ctx.pitchWheelSensitivityCents = 0;

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
        const i32 totalCents = zone.generators[GEN_CoarseTune] * 100 + zone.generators[GEN_FineTune];
        Require(totalCents == 200,
            "Spec resolver pitch wheel default should map +8191 at 2 semitones to about +200 cents");
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

    void TestSf2ModulatorResolverSameZoneDuplicateRule() {
        const SFModList mods[] = {
            MakeMod(0x0502u, GEN_InitialAttenuation, 100, 0, 2),
            MakeMod(0x0502u, GEN_InitialAttenuation, 300, 0, 2),
        };
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, mods, 2 };
        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, false);

        Require(resolved.size() == 1, "Duplicate modulator identity within a zone should result in one effective modulator");
        Require(resolved[0].mod.modAmount == 300, "The last definition of a duplicate modulator should be kept");
        Require(resolved[0].mod.sfModTransOper == 2, "The transform of the last definition should be preserved");
    }

    void TestSf2ModulatorResolverTransformSeparatesIdentity() {
        const SFModList mods[] = {
            MakeMod(0x0502u, GEN_InitialAttenuation, 100, 0, 0),
            MakeMod(0x0502u, GEN_InitialAttenuation, 300, 0, 2),
        };
        const Sf2ModulatorZone zone{ Sf2ModulatorLevel::InstrumentLocal, mods, 2 };
        const std::vector<Sf2ResolvedModulator> resolved = BuildSf2EffectiveModulators({ zone }, false);

        Require(resolved.size() == 2, "Different transforms should not collapse otherwise matching modulators");
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

    void TestInvalidLinkSourceIsReported() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(127, GEN_Pan, 500, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        Require(sf2.UnsupportedModulatorCount() == 1,
            "Link source used as a direct source should be reported as unsupported");
        Require(sf2.UnsupportedModulatorTransformCount() == 0,
            "Invalid link source should not increment transform count");

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_Pan] == 0,
            "Invalid link source should be ignored during zone resolution");
    }

    void TestEffectsSendMixPolicy() {
        Voice sf2;
        sf2.soundBankKind = SoundBankKind::Sf2;
        sf2.presetReverbSend = 0.25f;
        sf2.presetChorusSend = 0.4f;
        sf2.UpdateChannelMix(0.75f, 0xFFFFFFFFu, FloatToU32(0.5f), FloatToU32(0.25f));
        Require(std::fabs(sf2.channelGainL) < 1.0e-4f, "Default SF2 mode should still post-apply channel pan on the left lane");
        Require(std::fabs(sf2.channelGainR - 0.75f) < 1.0e-4f, "Default SF2 mode should keep channel volume in the mixer");
        Require(std::fabs(sf2.reverbSend - 0.25f) < 1.0e-4f, "SF2 channel reverb send should come from modulators by default");
        Require(std::fabs(sf2.chorusSend - 0.4f) < 1.0e-4f, "SF2 channel chorus send should come from modulators by default");
        sf2.SetSf2EffectSendScale(0.5f, 1.5f);
        Require(std::fabs(sf2.reverbSend - 0.125f) < 1.0e-4f, "SF2 reverb send scale should attenuate preset send");
        Require(std::fabs(sf2.chorusSend - 0.6f) < 1.0e-4f, "SF2 chorus send scale should boost preset send");

        Voice sf2Center;
        sf2Center.soundBankKind = SoundBankKind::Sf2;
        sf2Center.UpdateChannelMix(0.75f, 0x81020408u, 0u, 0u);
        Require(sf2Center.channelGainL < 0.75f && sf2Center.channelGainR < 0.75f,
            "Default SF2 center pan should currently attenuate both lanes below channel volume");
        Require(std::fabs(sf2Center.channelGainL - sf2Center.channelGainR) < 0.01f,
            "Default SF2 center pan should keep both lanes nearly symmetric");

        Voice sf2Compat;
        sf2Compat.soundBankKind = SoundBankKind::Sf2;
        sf2Compat.compatOptions.applySf2ChannelDefaults = true;
        sf2Compat.compatOptions.multiplySf2MidiEffectsSends = true;
        sf2Compat.presetReverbSend = 0.25f;
        sf2Compat.presetChorusSend = 0.4f;
        sf2Compat.UpdateChannelMix(1.0f, 0x80000000u, FloatToU32(0.5f), FloatToU32(0.25f));
        Require(std::fabs(sf2Compat.channelGainL - sf2Compat.channelGainR) < 0.01f,
            "SF2 channel defaults should keep normal channel pan in the mixer");
        Require(sf2Compat.channelGainL < 1.0f && sf2Compat.channelGainR < 1.0f,
            "SF2 channel defaults should keep normal channel volume in the mixer");
        Require(std::fabs(sf2Compat.reverbSend - 0.125f) < 1.0e-4f, "SF2 compatibility mode should multiply reverb sends");
        Require(std::fabs(sf2Compat.chorusSend - 0.1f) < 1.0e-4f, "SF2 compatibility mode should multiply chorus sends");
        sf2Compat.SetSf2EffectSendScale(2.0f, 0.5f);
        Require(std::fabs(sf2Compat.reverbSend - 0.25f) < 1.0e-4f, "SF2 reverb send scale should apply after compatibility multiplication");
        Require(std::fabs(sf2Compat.chorusSend - 0.05f) < 1.0e-4f, "SF2 chorus send scale should apply after compatibility multiplication");

        Voice dls;
        dls.soundBankKind = SoundBankKind::Dls;
        dls.presetReverbSend = 0.25f;
        dls.presetChorusSend = 0.4f;
        dls.UpdateChannelMix(1.0f, 0xFFFFFFFFu, FloatToU32(0.5f), FloatToU32(0.25f));
        Require(dls.channelGainR > dls.channelGainL, "Non-SF2 channel pan should still affect the output mix");
        Require(std::fabs(dls.reverbSend - 0.75f) < 1.0e-4f, "Non-SF2 send policy should still sum sends");
        Require(std::fabs(dls.chorusSend - 0.65f) < 1.0e-4f, "Non-SF2 chorus policy should still sum sends");
        dls.SetSf2EffectSendScale(0.0f, 0.0f);
        Require(std::fabs(dls.reverbSend - 0.75f) < 1.0e-4f, "SF2 send scale should not affect non-SF2 reverb sends");
        Require(std::fabs(dls.chorusSend - 0.65f) < 1.0e-4f, "SF2 send scale should not affect non-SF2 chorus sends");
    }

    void TestOutputLimiterAvoidsCrossSampleDucking() {
        OutputLimiter limiter;

        f32 loudL = 1.5f;
        f32 loudR = -1.5f;
        limiter.Process(loudL, loudR);
        Require(std::fabs(loudL) <= 1.0f && std::fabs(loudR) <= 1.0f,
            "Limiter should still constrain hot samples into the PCM range");

        f32 quietL = 0.25f;
        f32 quietR = -0.25f;
        limiter.Process(quietL, quietR);
        Require(std::fabs(quietL - 0.25f) < 1.0e-6f,
            "Limiter should not duck later quiet samples after a transient peak");
        Require(std::fabs(quietR + 0.25f) < 1.0e-6f,
            "Limiter should preserve the opposite lane when no limiting is needed");
    }

    void TestOutputLimiterUsesLinkedStereoGain() {
        OutputLimiter limiter;

        f32 sampleL = 0.49f;
        f32 sampleR = 1.40f;
        limiter.Process(sampleL, sampleR);

        Require(sampleR > 0.98f && sampleR < 1.0f,
            "Limiter should soft-limit the hotter lane without exceeding full scale");
        const f32 expectedGain = sampleR / 1.40f;
        Require(std::fabs(sampleL - 0.49f * expectedGain) < 1.0e-6f,
            "Limiter should apply the same gain to the left lane when the right lane clips");
        Require(std::fabs(sampleR - 1.40f * expectedGain) < 1.0e-6f,
            "Limiter should keep both lanes on the same linked gain curve");
    }

    void TestEnhancedOutputStageAddsQuietLoudness() {
        OutputStage stage;
        stage.SetMode(OutputStage::Mode::EnhancedLoud);

        f32 sampleL = 0.20f;
        f32 sampleR = -0.20f;
        stage.Process(sampleL, sampleR, 1.0f);

        Require(sampleL > 0.20f && sampleL < 0.30f,
            "Enhanced output stage should add modest loudness below the saturation knee");
        Require(sampleR < -0.20f && sampleR > -0.30f,
            "Enhanced output stage should apply the same modest loudness to the opposite lane");
    }

    void TestEnhancedOutputStageUsesLinkedPeakShaping() {
        OutputStage stage;
        stage.SetMode(OutputStage::Mode::EnhancedLoud);

        f32 sampleL = 0.40f;
        f32 sampleR = 1.40f;
        stage.Process(sampleL, sampleR, 1.0f);

        Require(std::fabs(sampleL) < std::fabs(sampleR),
            "Enhanced output stage should preserve the relative hot stereo lane");
        Require(std::fabs(sampleR) < 1.0f,
            "Enhanced output stage should constrain hot samples before PCM conversion");
        const f32 expectedInputRatio = 0.40f / 1.40f;
        const f32 actualRatio = sampleL / sampleR;
        Require(std::fabs(actualRatio - expectedInputRatio) < 0.02f,
            "Enhanced output stage should keep linked stereo gain close to the input ratio");
    }

    void TestEnhancedOutputStageAdaptsToDensePassages() {
        OutputStage stage;
        stage.SetMode(OutputStage::Mode::EnhancedLoud);

        f32 firstL = 0.85f;
        f32 firstR = -0.85f;
        stage.Process(firstL, firstR, 1.0f);

        f32 lastL = 0.85f;
        f32 lastR = -0.85f;
        for (int i = 0; i < 512; ++i) {
            lastL = 0.85f;
            lastR = -0.85f;
            stage.Process(lastL, lastR, 1.0f);
        }

        Require(std::fabs(lastL) <= std::fabs(firstL),
            "Enhanced output stage should ease sustained dense passages instead of driving them harder");
        Require(std::fabs(lastL) < 1.0f && std::fabs(lastR) < 1.0f,
            "Enhanced output stage should keep sustained dense passages below full scale");

        stage.Reset();
        f32 resetL = 0.20f;
        f32 resetR = -0.20f;
        stage.Process(resetL, resetR, 1.0f);
        Require(resetL > 0.20f && resetL < 0.30f,
            "Enhanced output stage reset should clear dense-passage gain state");
    }

    void TestOutputStagePresetsHaveDistinctDrive() {
        OutputStage natural;
        natural.SetMode(OutputStage::Mode::EnhancedNatural);
        f32 naturalL = 0.25f;
        f32 naturalR = -0.25f;
        natural.Process(naturalL, naturalR, 1.0f);

        OutputStage warm;
        warm.SetMode(OutputStage::Mode::EnhancedWarm);
        f32 warmL = 0.25f;
        f32 warmR = -0.25f;
        warm.Process(warmL, warmR, 1.0f);

        OutputStage loud;
        loud.SetMode(OutputStage::Mode::EnhancedLoud);
        f32 loudL = 0.25f;
        f32 loudR = -0.25f;
        loud.Process(loudL, loudR, 1.0f);

        Require(warmL > naturalL,
            "Warm output stage preset should drive quiet material harder than Natural");
        Require(loudL > warmL,
            "Loud output stage preset should drive quiet material harder than Warm");
        Require(loudL > naturalL,
            "Loud output stage preset should drive quiet material harder than Natural");
        Require(std::fabs(warmR) > std::fabs(naturalR) && std::fabs(loudR) > std::fabs(warmR),
            "Output stage preset drive should increase on both stereo lanes");
    }

    void TestOutputStageSmoothingScalesWithSampleRate() {
        const auto measureMeter = [](u32 sampleRate) {
            OutputStage stage;
            stage.SetMode(OutputStage::Mode::EnhancedLoud);
            stage.SetSampleRate(sampleRate);
            stage.Reset();
            const int totalFrames = static_cast<int>(sampleRate * 120u / 1000u);
            for (int i = 0; i < totalFrames; ++i) {
                f32 sampleL = 0.86f;
                f32 sampleR = -0.86f;
                stage.Process(sampleL, sampleR, 1.0f);
            }
            return stage.GetMeter();
        };

        const auto meter44100 = measureMeter(44100);
        const auto meter48000 = measureMeter(48000);

        Require(std::fabs(meter44100.densityGain - meter48000.densityGain) < 0.025f,
            "Output stage density smoothing should keep similar time response across sample rates");
        Require(std::fabs(meter44100.peakGain - meter48000.peakGain) < 0.025f,
            "Output stage peak smoothing should keep similar time response across sample rates");
    }

    void TestOutputStageSampleRateAndModeOrderIsStable() {
        const auto measure = [](bool sampleRateFirst) {
            OutputStage stage;
            if (sampleRateFirst) {
                stage.SetSampleRate(48000);
                stage.SetMode(OutputStage::Mode::EnhancedWarm);
            } else {
                stage.SetMode(OutputStage::Mode::EnhancedWarm);
                stage.SetSampleRate(48000);
            }
            stage.Reset();

            f32 lastL = 0.0f;
            f32 lastR = 0.0f;
            for (int i = 0; i < 7200; ++i) {
                lastL = 0.72f;
                lastR = -0.68f;
                stage.Process(lastL, lastR, 1.0f);
            }

            return std::pair<OutputStage::Meter, f32>{stage.GetMeter(), lastL + lastR};
        };

        const auto modeThenRate = measure(false);
        const auto rateThenMode = measure(true);

        Require(std::fabs(modeThenRate.first.densityGain - rateThenMode.first.densityGain) < 1.0e-6f,
            "Output stage density smoothing should not depend on SetMode/SetSampleRate order");
        Require(std::fabs(modeThenRate.first.peakGain - rateThenMode.first.peakGain) < 1.0e-6f,
            "Output stage peak smoothing should not depend on SetMode/SetSampleRate order");
        Require(std::fabs(modeThenRate.second - rateThenMode.second) < 1.0e-6f,
            "Output stage samples should not depend on SetMode/SetSampleRate order");
    }

    void TestOutputStageStandardMatchesLimiterPath() {
        OutputStage stage;
        stage.SetMode(OutputStage::Mode::Standard);
        f32 stageL = 0.49f;
        f32 stageR = 1.40f;
        stage.Process(stageL, stageR, 1.0f);

        OutputLimiter limiter;
        f32 limiterL = 0.49f;
        f32 limiterR = 1.40f;
        limiter.Process(limiterL, limiterR);

        Require(std::fabs(stageL - limiterL) < 1.0e-6f,
            "Standard output stage should preserve the legacy limiter path on the left lane");
        Require(std::fabs(stageR - limiterR) < 1.0e-6f,
            "Standard output stage should preserve the legacy limiter path on the right lane");
    }

    void TestOutputStageMeterTracksRenderBlock() {
        OutputStage stage;
        stage.SetMode(OutputStage::Mode::EnhancedWarm);
        stage.BeginMeterBlock();

        f32 quietL = 0.20f;
        f32 quietR = -0.20f;
        stage.Process(quietL, quietR, 1.0f);

        f32 hotL = 0.40f;
        f32 hotR = 1.20f;
        stage.Process(hotL, hotR, 1.0f);

        const OutputStage::Meter meter = stage.GetMeter();
        Require(meter.mode == OutputStage::Mode::EnhancedWarm,
            "Output stage meter should report the active preset");
        Require(meter.processedFrames == 2,
            "Output stage meter should count processed samples in the current block");
        Require(meter.inputPeak > 1.19f && meter.inputPeak < 1.21f,
            "Output stage meter should track pre-stage input peak");
        Require(meter.outputPeak > 0.0f && meter.outputPeak < 1.0f,
            "Output stage meter should track constrained output peak");
        Require(meter.densityGain > 0.0f && meter.densityGain <= 1.0f,
            "Output stage meter should expose density gain");
        Require(meter.peakGain > 0.0f && meter.peakGain <= 1.0f,
            "Output stage meter should expose linked peak gain");
    }

    void TestPostMixEffectsProducesAndResetsTail() {
        PostMixEffects effects;
        effects.Init(44100);

        double wetEnergy = 0.0;
        for (int i = 0; i < 6000; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            wetEnergy += std::fabs(out.wetL) + std::fabs(out.wetR);
        }

        Require(wetEnergy > 0.0,
            "Post-mix effects should produce a reverb tail from the master reverb send");
        Require(effects.HasAudibleTail(1.0e-4f),
            "Post-mix effects should report an audible tail after an impulse");

        effects.ResetState();
        Require(!effects.HasAudibleTail(1.0e-4f),
            "Post-mix effects reset should clear delay-line tails");
        const auto silentAfterReset = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        Require(silentAfterReset.wetL == 0.0f && silentAfterReset.wetR == 0.0f,
            "Post-mix effects reset should clear reverb damping state");
    }

    void TestPostMixEffectsProcessesChorusSend() {
        PostMixEffects effects;
        effects.Init(44100);

        double wetEnergy = 0.0;
        for (int i = 0; i < 2000; ++i) {
            const f32 chorusSend = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
            wetEnergy += std::fabs(out.wetL) + std::fabs(out.wetR);
        }

        Require(wetEnergy > 0.0,
            "Post-mix effects should produce chorus output from the chorus send bus");
        Require(effects.ApplyGsParameter(0x0F, 127),
            "Post-mix effects should accept GS chorus level parameter");
        Require(!effects.ApplyGsParameter(0x7F, 64),
            "Post-mix effects should reject unknown GS effect parameters");

        effects.ResetState();
        const auto silentAfterReset = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        Require(silentAfterReset.wetL == 0.0f && silentAfterReset.wetR == 0.0f,
            "Post-mix effects reset should clear chorus damping state");
    }

    void TestPostMixEffectsUserMixScales() {
        PostMixEffects effects;
        effects.Init(44100);
        effects.SetMixScales(0.0f, 0.0f, 0.0f, 0.0f);
        Require(effects.GetReverbReturnScale() == 0.0f,
            "Post-mix effects should expose the user reverb return scale");
        Require(effects.GetChorusReturnScale() == 0.0f,
            "Post-mix effects should expose the user chorus return scale");
        Require(effects.GetMasterReverbSendScale() == 0.0f,
            "Post-mix effects should expose the user master reverb send scale");
        Require(effects.GetChorusToReverbScale() == 0.0f,
            "Post-mix effects should expose the user chorus-to-reverb scale");

        double wetEnergy = 0.0;
        for (int i = 0; i < 6000; ++i) {
            const f32 impulse = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(impulse, impulse, impulse, impulse, impulse, impulse);
            wetEnergy += std::fabs(out.wetL) + std::fabs(out.wetR);
        }
        Require(wetEnergy == 0.0,
            "Zero user mix scales should mute internal effect returns");

        effects.SetMixScales(8.0f, -1.0f, 2.0f, 3.0f);
        Require(effects.GetReverbReturnScale() == 4.0f,
            "Post-mix effects should clamp high user reverb return scale");
        Require(effects.GetChorusReturnScale() == 0.0f,
            "Post-mix effects should clamp low user chorus return scale");
        Require(effects.GetMasterReverbSendScale() == 2.0f,
            "Post-mix effects should preserve valid master reverb send scale");
        Require(effects.GetChorusToReverbScale() == 3.0f,
            "Post-mix effects should preserve valid chorus-to-reverb scale");
    }

    void TestPostMixEffectsAudioResetPreservesGsState() {
        PostMixEffects fullResetEffects;
        fullResetEffects.Init(44100);
        Require(fullResetEffects.ApplyGsParameter(0x0F, 127),
            "Post-mix effects should accept GS chorus level before full reset");
        fullResetEffects.ResetState();

        PostMixEffects audioResetEffects;
        audioResetEffects.Init(44100);
        Require(audioResetEffects.ApplyGsParameter(0x0F, 127),
            "Post-mix effects should accept GS chorus level before audio reset");
        audioResetEffects.ResetAudioState();

        double fullResetEnergy = 0.0;
        double audioResetEnergy = 0.0;
        for (int i = 0; i < 2000; ++i) {
            const f32 chorusSend = (i == 0) ? 1.0f : 0.0f;
            const auto fullOut =
                fullResetEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
            const auto audioOut =
                audioResetEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
            fullResetEnergy += std::fabs(fullOut.wetL) + std::fabs(fullOut.wetR);
            audioResetEnergy += std::fabs(audioOut.wetL) + std::fabs(audioOut.wetR);
        }

        Require(audioResetEnergy > fullResetEnergy,
            "Post-mix audio reset should clear buffers while preserving GS effect scales");
    }

    void TestPostMixEffectsGsWetChangesAreSmoothed() {
        PostMixEffects smoothedEffects;
        smoothedEffects.Init(44100);
        Require(smoothedEffects.ApplyGsParameter(0x0F, 127),
            "Post-mix effects should accept GS chorus level before smoothing test");

        PostMixEffects immediateEffects;
        immediateEffects.Init(44100);
        Require(immediateEffects.ApplyGsParameter(0x0F, 127),
            "Post-mix effects should accept GS chorus level before immediate reset");
        immediateEffects.ResetAudioState();

        double smoothedEarlyEnergy = 0.0;
        double immediateEarlyEnergy = 0.0;
        double smoothedLateEnergy = 0.0;
        double immediateLateEnergy = 0.0;
        for (int i = 0; i < 6000; ++i) {
            const auto smoothedOut =
                smoothedEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            const auto immediateOut =
                immediateEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);

            const f32 smoothedFrame = std::fabs(smoothedOut.wetL) + std::fabs(smoothedOut.wetR);
            const f32 immediateFrame = std::fabs(immediateOut.wetL) + std::fabs(immediateOut.wetR);
            if (i > 120 && i < 1200) {
                smoothedEarlyEnergy += smoothedFrame;
                immediateEarlyEnergy += immediateFrame;
            }
            if (i > 4800) {
                smoothedLateEnergy += smoothedFrame;
                immediateLateEnergy += immediateFrame;
            }
        }

        Require(smoothedEarlyEnergy > 0.0 && immediateEarlyEnergy > 0.0 &&
                smoothedLateEnergy > 0.0 && immediateLateEnergy > 0.0,
            "Post-mix GS wet smoothing should preserve early chorus energy");
        Require(smoothedEarlyEnergy < immediateEarlyEnergy,
            "Post-mix GS wet smoothing should ease into higher chorus levels");
        Require((smoothedLateEnergy / immediateLateEnergy) >
                (smoothedEarlyEnergy / immediateEarlyEnergy),
            "Post-mix GS wet smoothing should continue moving toward the requested level");
    }

    void TestPostMixEffectsChorusToReverbChangesAreSmoothed() {
        PostMixEffects smoothedEffects;
        smoothedEffects.Init(44100);
        Require(smoothedEffects.ApplyGsParameter(0x0F, 0),
            "Post-mix effects should accept low GS chorus output level before route smoothing test");
        Require(smoothedEffects.ApplyGsParameter(0x14, 127),
            "Post-mix effects should accept GS chorus-to-reverb level before smoothing test");

        PostMixEffects immediateEffects;
        immediateEffects.Init(44100);
        Require(immediateEffects.ApplyGsParameter(0x0F, 0),
            "Post-mix effects should accept low GS chorus output level before immediate route reset");
        Require(immediateEffects.ApplyGsParameter(0x14, 127),
            "Post-mix effects should accept GS chorus-to-reverb level before immediate reset");
        immediateEffects.ResetAudioState();

        double smoothedEarlyLateEnergy = 0.0;
        double immediateEarlyLateEnergy = 0.0;
        double smoothedLaterEnergy = 0.0;
        double immediateLaterEnergy = 0.0;
        for (int i = 0; i < 9000; ++i) {
            const auto smoothedOut =
                smoothedEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            const auto immediateOut =
                immediateEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);

            const f32 smoothedFrame = std::fabs(smoothedOut.wetL) + std::fabs(smoothedOut.wetR);
            const f32 immediateFrame = std::fabs(immediateOut.wetL) + std::fabs(immediateOut.wetR);
            if (i > 2200 && i < 4200) {
                smoothedEarlyLateEnergy += smoothedFrame;
                immediateEarlyLateEnergy += immediateFrame;
            }
            if (i > 7000) {
                smoothedLaterEnergy += smoothedFrame;
                immediateLaterEnergy += immediateFrame;
            }
        }

        Require(smoothedEarlyLateEnergy > 0.0 && immediateEarlyLateEnergy > 0.0 &&
                smoothedLaterEnergy > 0.0 && immediateLaterEnergy > 0.0,
            "Post-mix chorus-to-reverb smoothing should preserve routed reverb energy");
        Require(smoothedEarlyLateEnergy < immediateEarlyLateEnergy,
            "Post-mix chorus-to-reverb smoothing should ease into stronger routed reverb");
        Require((smoothedLaterEnergy / immediateLaterEnergy) >
                (smoothedEarlyLateEnergy / immediateEarlyLateEnergy),
            "Post-mix chorus-to-reverb smoothing should continue moving toward the requested route");
    }

    void TestPostMixEffectsMasterReverbSendChangesAreSmoothed() {
        PostMixEffects smoothedEffects;
        smoothedEffects.Init(44100);
        Require(smoothedEffects.ApplyGsParameter(0x0A, 127),
            "Post-mix effects should accept high GS master reverb send before smoothing test");

        PostMixEffects immediateEffects;
        immediateEffects.Init(44100);
        Require(immediateEffects.ApplyGsParameter(0x0A, 127),
            "Post-mix effects should accept high GS master reverb send before immediate reset");
        immediateEffects.ResetAudioState();

        double smoothedEarlyEnergy = 0.0;
        double immediateEarlyEnergy = 0.0;
        double smoothedLateEnergy = 0.0;
        double immediateLateEnergy = 0.0;
        for (int i = 0; i < 9000; ++i) {
            const auto smoothedOut =
                smoothedEffects.ProcessSample(1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            const auto immediateOut =
                immediateEffects.ProcessSample(1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f);

            const f32 smoothedFrame = std::fabs(smoothedOut.wetL) + std::fabs(smoothedOut.wetR);
            const f32 immediateFrame = std::fabs(immediateOut.wetL) + std::fabs(immediateOut.wetR);
            if (i > 900 && i < 2600) {
                smoothedEarlyEnergy += smoothedFrame;
                immediateEarlyEnergy += immediateFrame;
            }
            if (i > 7000) {
                smoothedLateEnergy += smoothedFrame;
                immediateLateEnergy += immediateFrame;
            }
        }

        Require(smoothedEarlyEnergy > 0.0 && immediateEarlyEnergy > 0.0 &&
                smoothedLateEnergy > 0.0 && immediateLateEnergy > 0.0,
            "Post-mix master reverb send smoothing should preserve routed dry reverb energy");
        Require(smoothedEarlyEnergy < immediateEarlyEnergy,
            "Post-mix master reverb send smoothing should ease into stronger dry reverb sends");
        Require((smoothedLateEnergy / immediateLateEnergy) >
                (smoothedEarlyEnergy / immediateEarlyEnergy),
            "Post-mix master reverb send smoothing should continue moving toward the requested send");
    }

    void TestPostMixEffectsChorusModulationChangesAreSmoothed() {
        PostMixEffects smoothedEffects;
        smoothedEffects.Init(44100);
        Require(smoothedEffects.ApplyGsParameter(0x11, 127),
            "Post-mix effects should accept high GS chorus delay before smoothing test");
        Require(smoothedEffects.ApplyGsParameter(0x13, 127),
            "Post-mix effects should accept high GS chorus depth before smoothing test");
        Require(smoothedEffects.ApplyGsParameter(0x12, 127),
            "Post-mix effects should accept high GS chorus rate before smoothing test");

        PostMixEffects immediateEffects;
        immediateEffects.Init(44100);
        Require(immediateEffects.ApplyGsParameter(0x11, 127),
            "Post-mix effects should accept high GS chorus delay before immediate reset");
        Require(immediateEffects.ApplyGsParameter(0x13, 127),
            "Post-mix effects should accept high GS chorus depth before immediate reset");
        Require(immediateEffects.ApplyGsParameter(0x12, 127),
            "Post-mix effects should accept high GS chorus rate before immediate reset");
        immediateEffects.ResetAudioState();

        int smoothedFirst = -1;
        int immediateFirst = -1;
        double smoothedLaterEnergy = 0.0;
        for (int i = 0; i < 3600; ++i) {
            const f32 send = (i == 0) ? 1.0f : 0.0f;
            const auto smoothedOut =
                smoothedEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, send, send);
            const auto immediateOut =
                immediateEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, send, send);

            if (smoothedFirst < 0 &&
                (std::fabs(smoothedOut.wetL) + std::fabs(smoothedOut.wetR)) > 1.0e-7f) {
                smoothedFirst = i;
            }
            if (immediateFirst < 0 &&
                (std::fabs(immediateOut.wetL) + std::fabs(immediateOut.wetR)) > 1.0e-7f) {
                immediateFirst = i;
            }
            if (i > 2000) {
                smoothedLaterEnergy += std::fabs(smoothedOut.wetL) + std::fabs(smoothedOut.wetR);
            }
        }

        Require(smoothedFirst >= 0 && immediateFirst >= 0,
            "Post-mix chorus modulation smoothing should preserve chorus output");
        Require(smoothedFirst < immediateFirst,
            "Post-mix chorus modulation smoothing should ease into longer chorus delay");
        Require(smoothedLaterEnergy > 0.0,
            "Post-mix chorus modulation smoothing should keep producing later chorus energy");
    }

    void TestPostMixEffectsFeedbackChangesAreSmoothed() {
        PostMixEffects smoothedEffects;
        smoothedEffects.Init(44100);
        Require(smoothedEffects.ApplyGsParameter(0x09, 127),
            "Post-mix effects should accept high GS reverb feedback before smoothing test");
        Require(smoothedEffects.ApplyGsParameter(0x10, 127),
            "Post-mix effects should accept high GS chorus feedback before smoothing test");

        PostMixEffects immediateEffects;
        immediateEffects.Init(44100);
        Require(immediateEffects.ApplyGsParameter(0x09, 127),
            "Post-mix effects should accept high GS reverb feedback before immediate reset");
        Require(immediateEffects.ApplyGsParameter(0x10, 127),
            "Post-mix effects should accept high GS chorus feedback before immediate reset");
        immediateEffects.ResetAudioState();

        double smoothedMidEnergy = 0.0;
        double immediateMidEnergy = 0.0;
        double smoothedLateEnergy = 0.0;
        for (int i = 0; i < 14000; ++i) {
            const auto smoothedOut =
                smoothedEffects.ProcessSample(0.0f, 0.0f, 0.85f, 0.85f, 0.85f, 0.85f);
            const auto immediateOut =
                immediateEffects.ProcessSample(0.0f, 0.0f, 0.85f, 0.85f, 0.85f, 0.85f);

            const f32 smoothedFrame = std::fabs(smoothedOut.wetL) + std::fabs(smoothedOut.wetR);
            const f32 immediateFrame = std::fabs(immediateOut.wetL) + std::fabs(immediateOut.wetR);
            if (i > 3000 && i < 7200) {
                smoothedMidEnergy += smoothedFrame;
                immediateMidEnergy += immediateFrame;
            }
            if (i > 11200) {
                smoothedLateEnergy += smoothedFrame;
            }
        }

        Require(smoothedMidEnergy > 0.0 && immediateMidEnergy > 0.0,
            "Post-mix feedback smoothing should preserve reverb and chorus output");
        Require(smoothedMidEnergy < immediateMidEnergy,
            "Post-mix feedback smoothing should ease into stronger feedback");
        Require(smoothedLateEnergy > 0.0,
            "Post-mix feedback smoothing should keep producing later wet energy");
    }

    void TestPostMixEffectsGsSmoothingScalesWithSampleRate() {
        const auto measureEnergy = [](u32 sampleRate) {
            PostMixEffects effects;
            effects.Init(sampleRate);
            Require(effects.ApplyGsParameter(0x0F, 127),
                "Post-mix effects should accept high GS chorus level before sample-rate smoothing test");

            double energy = 0.0;
            const int totalFrames = static_cast<int>(sampleRate * 140u / 1000u);
            const int windowStart = static_cast<int>(sampleRate * 45u / 1000u);
            const int windowEnd = static_cast<int>(sampleRate * 110u / 1000u);
            for (int i = 0; i < totalFrames; ++i) {
                const auto out =
                    effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
                if (i >= windowStart && i < windowEnd) {
                    energy += std::fabs(out.wetL) + std::fabs(out.wetR);
                }
            }
            return energy / static_cast<double>(windowEnd - windowStart);
        };

        const double energy44100 = measureEnergy(44100);
        const double energy48000 = measureEnergy(48000);
        const double ratio = energy48000 / energy44100;

        Require(energy44100 > 0.0 && energy48000 > 0.0,
            "Post-mix GS smoothing should produce wet output at both sample rates");
        Require(ratio > 0.80 && ratio < 1.20,
            "Post-mix GS smoothing should keep similar time response across sample rates");
    }

    void TestPostMixEffectsInputDampingScalesWithSampleRate() {
        const auto measureChorusOnset = [](u32 sampleRate) {
            PostMixEffects effects;
            effects.Init(sampleRate);

            int firstWetFrame = -1;
            double energy = 0.0;
            int frames = 0;
            const int totalFrames = static_cast<int>(sampleRate * 70u / 1000u);
            const int windowFrames = static_cast<int>(sampleRate * 7u / 1000u);
            for (int i = 0; i < totalFrames; ++i) {
                const f32 send = (i == 0) ? 1.0f : 0.0f;
                const auto out = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, send, send);
                const double frameWet = std::fabs(out.wetL) + std::fabs(out.wetR);
                if (firstWetFrame < 0 && frameWet > 1.0e-7) {
                    firstWetFrame = i;
                }
                if (firstWetFrame >= 0 && i >= firstWetFrame && i < firstWetFrame + windowFrames) {
                    energy += frameWet;
                    ++frames;
                }
            }
            Require(firstWetFrame >= 0 && frames > 0,
                "Post-mix chorus input damping should produce a measurable onset");
            return energy / static_cast<double>(frames);
        };

        const auto measureReverbOnset = [](u32 sampleRate) {
            PostMixEffects effects;
            effects.Init(sampleRate);

            int firstWetFrame = -1;
            double energy = 0.0;
            int frames = 0;
            const int totalFrames = static_cast<int>(sampleRate * 90u / 1000u);
            const int windowFrames = static_cast<int>(sampleRate * 9u / 1000u);
            for (int i = 0; i < totalFrames; ++i) {
                const f32 dry = (i == 0) ? 1.0f : 0.0f;
                const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
                const double frameWet = std::fabs(out.wetL) + std::fabs(out.wetR);
                if (firstWetFrame < 0 && frameWet > 1.0e-7) {
                    firstWetFrame = i;
                }
                if (firstWetFrame >= 0 && i >= firstWetFrame && i < firstWetFrame + windowFrames) {
                    energy += frameWet;
                    ++frames;
                }
            }
            Require(firstWetFrame >= 0 && frames > 0,
                "Post-mix reverb input damping should produce a measurable onset");
            return energy / static_cast<double>(frames);
        };

        const double chorusRatio = measureChorusOnset(48000) / measureChorusOnset(44100);
        const double reverbRatio = measureReverbOnset(48000) / measureReverbOnset(44100);

        Require(chorusRatio > 0.80 && chorusRatio < 1.20,
            "Post-mix chorus input damping should keep similar time response across sample rates");
        Require(reverbRatio > 0.80 && reverbRatio < 1.20,
            "Post-mix reverb input damping should keep similar time response across sample rates");
    }

    void TestPostMixEffectsInternalDampingScalesWithSampleRate() {
        const auto measureWetEnergy = [](u32 sampleRate, u32 startMs, u32 endMs) {
            PostMixEffects effects;
            effects.Init(sampleRate);

            double energy = 0.0;
            const int totalFrames = static_cast<int>(sampleRate * 190u / 1000u);
            const int windowStart = static_cast<int>(sampleRate * startMs / 1000u);
            const int windowEnd = static_cast<int>(sampleRate * endMs / 1000u);
            for (int i = 0; i < totalFrames; ++i) {
                const f32 impulse = (i == 0) ? 1.0f : 0.0f;
                const auto out = effects.ProcessSample(impulse, impulse,
                    impulse * 0.6f, impulse * 0.6f, impulse, impulse);
                if (i >= windowStart && i < windowEnd) {
                    energy += std::fabs(out.wetL) + std::fabs(out.wetR);
                }
            }
            return energy / static_cast<double>(windowEnd - windowStart);
        };

        const double midRatio =
            measureWetEnergy(48000, 35, 105) / measureWetEnergy(44100, 35, 105);
        const double lateRatio =
            measureWetEnergy(48000, 110, 180) / measureWetEnergy(44100, 110, 180);

        Require(midRatio > 0.70 && midRatio < 1.30,
            "Post-mix internal damping should keep similar mid-tail energy across sample rates");
        Require(lateRatio > 0.70 && lateRatio < 1.30,
            "Post-mix internal damping should keep similar late-tail energy across sample rates");
    }

    void TestPostMixEffectsChorusSecondaryTapThickensReturn() {
        PostMixEffects effects;
        effects.Init(44100);

        int wetFrameCount = 0;
        int lastWetFrame = -1;
        for (int i = 0; i < 1200; ++i) {
            const f32 chorusSend = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
            if ((std::fabs(out.wetL) + std::fabs(out.wetR)) > 1.0e-7f) {
                ++wetFrameCount;
                lastWetFrame = i;
            }
        }

        Require(wetFrameCount > 4,
            "Post-mix chorus should spread an impulse across multiple wet frames");
        Require(lastWetFrame > 260,
            "Post-mix chorus secondary tap should add a later thickening reflection");
    }

    void TestPostMixEffectsChorusToneDampingSmoothsReturn() {
        PostMixEffects effects;
        effects.Init(44100);

        int observedFrames = 0;
        double maxStep = 0.0;
        f32 previous = 0.0f;
        bool havePrevious = false;
        for (int i = 0; i < 1200; ++i) {
            const f32 chorusSend = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
            const f32 wet = (out.wetL + out.wetR) * 0.5f;
            if (std::fabs(wet) > 1.0e-7f) {
                if (havePrevious) {
                    maxStep = std::max<double>(maxStep, std::fabs(wet - previous));
                }
                previous = wet;
                havePrevious = true;
                ++observedFrames;
            }
        }

        Require(observedFrames > 4,
            "Post-mix chorus tone damping should preserve chorus return energy");
        Require(maxStep < 0.40,
            "Post-mix chorus tone damping should avoid abrupt chorus-return jumps");

        effects.ResetState();
        const auto silentAfterReset = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        Require(silentAfterReset.wetL == 0.0f && silentAfterReset.wetR == 0.0f,
            "Post-mix effects reset should clear chorus tone damping state");
    }

    void TestPostMixEffectsChorusInputDampingSpreadsOnset() {
        PostMixEffects effects;
        effects.Init(44100);

        int firstWetFrame = -1;
        int onsetFrames = 0;
        double onsetEnergy = 0.0;
        double totalEnergy = 0.0;
        for (int i = 0; i < 1200; ++i) {
            const f32 chorusSend = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
            const f32 frameWet = std::fabs(out.wetL) + std::fabs(out.wetR);
            totalEnergy += frameWet;
            if (firstWetFrame < 0 && frameWet > 1.0e-7f) {
                firstWetFrame = i;
            }
            if (firstWetFrame >= 0 && i >= firstWetFrame && i < firstWetFrame + 32 && frameWet > 1.0e-7f) {
                ++onsetFrames;
                onsetEnergy += frameWet;
            }
        }

        Require(firstWetFrame >= 0,
            "Post-mix chorus input damping should preserve the first chorus onset");
        Require(onsetFrames > 2,
            "Post-mix chorus input damping should spread a sharp send across multiple wet frames");
        Require(onsetEnergy > 0.0 && totalEnergy > onsetEnergy,
            "Post-mix chorus input damping should leave energy for the later chorus return");
    }

    void TestPostMixEffectsChorusRateScalesWithSampleRate() {
        PostMixEffects at44100;
        at44100.Init(44100);
        PostMixEffects at48000;
        at48000.Init(48000);

        double energy44100 = 0.0;
        double energy48000 = 0.0;
        int first44100 = -1;
        int first48000 = -1;
        for (int i = 0; i < 2200; ++i) {
            const f32 send44100 = (i == 0) ? 1.0f : 0.0f;
            const auto out44100 = at44100.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, send44100, send44100);
            if (first44100 < 0 && (std::fabs(out44100.wetL) + std::fabs(out44100.wetR)) > 1.0e-7f) {
                first44100 = i;
            }
            energy44100 += std::fabs(out44100.wetL) + std::fabs(out44100.wetR);

            const f32 send48000 = (i == 0) ? 1.0f : 0.0f;
            const auto out48000 = at48000.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, send48000, send48000);
            if (first48000 < 0 && (std::fabs(out48000.wetL) + std::fabs(out48000.wetR)) > 1.0e-7f) {
                first48000 = i;
            }
            energy48000 += std::fabs(out48000.wetL) + std::fabs(out48000.wetR);
        }

        Require(energy44100 > 0.0 && energy48000 > 0.0,
            "Post-mix chorus should produce output at common sample rates");
        Require(first48000 > first44100,
            "Post-mix chorus delay timing should scale upward at 48 kHz");
        Require(first48000 - first44100 < 24,
            "Post-mix chorus sample-rate scaling should keep timing close in milliseconds");
    }

    void TestPostMixEffectsTailIncludesSmoothingState() {
        PostMixEffects effects;
        effects.Init(44100);
        Require(effects.ApplyGsParameter(0x0F, 127),
            "Post-mix effects should accept GS chorus level before tail smoothing test");
        Require(effects.HasAudibleTail(1.0e-7f),
            "Post-mix tail detection should include pending GS smoothing state");

        effects.ResetAudioState();
        Require(!effects.HasAudibleTail(1.0e-7f),
            "Post-mix audio reset should sync pending GS smoothing state");

        for (int i = 0; i < 1600; ++i) {
            const f32 chorusSend = (i == 0) ? 1.0f : 0.0f;
            effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, chorusSend, chorusSend);
        }

        Require(effects.HasAudibleTail(1.0e-7f),
            "Post-mix tail detection should include smoothing state as well as delay buffers");

        effects.ResetState();
        Require(!effects.HasAudibleTail(1.0e-7f),
            "Post-mix reset should clear smoothing state from tail detection");
    }

    void TestPostMixEffectsFlushesTinyAudioState() {
        PostMixEffects effects;
        effects.Init(44100);

        double tinyWetEnergy = 0.0;
        for (int i = 0; i < 256; ++i) {
            const auto out =
                effects.ProcessSample(1.0e-30f, -1.0e-30f, 1.0e-30f, -1.0e-30f, 1.0e-30f, -1.0e-30f);
            tinyWetEnergy += std::fabs(out.wetL) + std::fabs(out.wetR);
        }

        Require(tinyWetEnergy == 0.0,
            "Post-mix effects should flush inaudibly tiny wet output");
        Require(!effects.HasAudibleTail(1.0e-25f),
            "Post-mix effects should not retain denormal-scale audio state");
    }

    void TestPostMixEffectsFeedbackClampKeepsHotGsStable() {
        PostMixEffects effects;
        effects.Init(44100);
        Require(effects.ApplyGsParameter(0x09, 127),
            "Post-mix effects should accept maximum GS reverb feedback");
        Require(effects.ApplyGsParameter(0x10, 127),
            "Post-mix effects should accept maximum GS chorus feedback");

        double peak = 0.0;
        double lateEnergy = 0.0;
        for (int i = 0; i < 48000; ++i) {
            const f32 hotSend = (i == 0) ? 8.0f : 0.0f;
            const auto out =
                effects.ProcessSample(0.0f, 0.0f, hotSend, hotSend, hotSend, hotSend);
            const f32 framePeak = std::max(std::fabs(out.wetL), std::fabs(out.wetR));
            peak = std::max<double>(peak, framePeak);
            if (i > 24000) {
                lateEnergy += std::fabs(out.wetL) + std::fabs(out.wetR);
            }
        }

        Require(peak > 0.0,
            "Post-mix feedback clamp should preserve hot GS effect output");
        Require(peak < 8.0,
            "Post-mix feedback clamp should keep hot GS effect peaks bounded");
        Require(lateEnergy < 200.0,
            "Post-mix feedback clamp should avoid runaway late tails");
    }

    void TestPostMixEffectsInputShapeSoftensExtremeSends() {
        PostMixEffects normalEffects;
        normalEffects.Init(44100);
        PostMixEffects extremeEffects;
        extremeEffects.Init(44100);

        double normalPeak = 0.0;
        double extremePeak = 0.0;
        for (int i = 0; i < 12000; ++i) {
            const f32 normalSend = (i == 0) ? 1.0f : 0.0f;
            const auto normalOut =
                normalEffects.ProcessSample(0.0f, 0.0f, normalSend, normalSend, normalSend, normalSend);
            normalPeak = std::max<double>(normalPeak,
                std::max(std::fabs(normalOut.wetL), std::fabs(normalOut.wetR)));

            const f32 extremeSend = (i == 0) ? 32.0f : 0.0f;
            const auto extremeOut =
                extremeEffects.ProcessSample(0.0f, 0.0f, extremeSend, extremeSend, extremeSend, extremeSend);
            extremePeak = std::max<double>(extremePeak,
                std::max(std::fabs(extremeOut.wetL), std::fabs(extremeOut.wetR)));
        }

        Require(normalPeak > 0.0,
            "Post-mix effect input shaping should preserve normal effect sends");
        Require(extremePeak > normalPeak,
            "Post-mix effect input shaping should still respond to stronger sends");
        Require(extremePeak < normalPeak * 16.0,
            "Post-mix effect input shaping should soften extreme send levels before the tank");
    }

    void TestPostMixEffectsShapesChorusToReverbSend() {
        PostMixEffects normalEffects;
        normalEffects.Init(44100);
        Require(normalEffects.ApplyGsParameter(0x14, 127),
            "Post-mix effects should accept maximum GS chorus-to-reverb send");
        PostMixEffects extremeEffects;
        extremeEffects.Init(44100);
        Require(extremeEffects.ApplyGsParameter(0x14, 127),
            "Post-mix effects should accept maximum GS chorus-to-reverb send");

        double normalLateEnergy = 0.0;
        double extremeLateEnergy = 0.0;
        for (int i = 0; i < 12000; ++i) {
            const f32 normalChorusSend = (i == 0) ? 1.0f : 0.0f;
            const auto normalOut =
                normalEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, normalChorusSend, normalChorusSend);
            if (i > 2000) {
                normalLateEnergy += std::fabs(normalOut.wetL) + std::fabs(normalOut.wetR);
            }

            const f32 extremeChorusSend = (i == 0) ? 32.0f : 0.0f;
            const auto extremeOut =
                extremeEffects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, extremeChorusSend, extremeChorusSend);
            if (i > 2000) {
                extremeLateEnergy += std::fabs(extremeOut.wetL) + std::fabs(extremeOut.wetR);
            }
        }

        Require(normalLateEnergy > 0.0,
            "Post-mix chorus-to-reverb shaping should preserve normal routed reverb energy");
        Require(extremeLateEnergy > normalLateEnergy,
            "Post-mix chorus-to-reverb shaping should still respond to stronger chorus sends");
        Require(extremeLateEnergy < normalLateEnergy * 16.0,
            "Post-mix chorus-to-reverb shaping should soften extreme routed reverb energy");
    }

    void TestPostMixEffectsReverbDiffusionCreatesDenseTail() {
        PostMixEffects effects;
        effects.Init(44100);

        int wetFrameCount = 0;
        double wetEnergyL = 0.0;
        double wetEnergyR = 0.0;
        for (int i = 0; i < 12000; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            const f32 frameWet = std::fabs(out.wetL) + std::fabs(out.wetR);
            if (frameWet > 1.0e-7f) {
                ++wetFrameCount;
            }
            wetEnergyL += std::fabs(out.wetL);
            wetEnergyR += std::fabs(out.wetR);
        }

        Require(wetFrameCount > 24,
            "Post-mix reverb diffusion should create a dense enough tail after an impulse");
        Require(wetEnergyL > 0.0 && wetEnergyR > 0.0,
            "Post-mix reverb diffusion should produce stereo wet energy");
    }

    void TestPostMixEffectsEarlyReflectionsArriveQuickly() {
        PostMixEffects effects;
        effects.Init(44100);

        int firstWetFrame = -1;
        for (int i = 0; i < 2200; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            if (firstWetFrame < 0 && (std::fabs(out.wetL) + std::fabs(out.wetR)) > 1.0e-7f) {
                firstWetFrame = i;
            }
        }

        Require(firstWetFrame >= 0,
            "Post-mix reverb should produce early reflections after an impulse");
        Require(firstWetFrame < 1000,
            "Post-mix early reflections should arrive before the main late reverb taps");
    }

    void TestPostMixEffectsReverbPredelaySeparatesOnset() {
        PostMixEffects effects;
        effects.Init(44100);

        int firstWetFrame = -1;
        for (int i = 0; i < 2200; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            if (firstWetFrame < 0 && (std::fabs(out.wetL) + std::fabs(out.wetR)) > 1.0e-7f) {
                firstWetFrame = i;
            }
        }

        Require(firstWetFrame > 700,
            "Post-mix reverb predelay should leave the direct onset uncluttered");
        Require(firstWetFrame < 1000,
            "Post-mix reverb predelay should still keep early reflections close to the source");
    }

    void TestPostMixEffectsReverbInputDampingSpreadsOnset() {
        PostMixEffects effects;
        effects.Init(44100);

        int firstWetFrame = -1;
        int onsetFrames = 0;
        double onsetEnergy = 0.0;
        double totalEnergy = 0.0;
        for (int i = 0; i < 2400; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            const f32 frameWet = std::fabs(out.wetL) + std::fabs(out.wetR);
            totalEnergy += frameWet;
            if (firstWetFrame < 0 && frameWet > 1.0e-7f) {
                firstWetFrame = i;
            }
            if (firstWetFrame >= 0 && i >= firstWetFrame && i < firstWetFrame + 48 && frameWet > 1.0e-7f) {
                ++onsetFrames;
                onsetEnergy += frameWet;
            }
        }

        Require(firstWetFrame >= 0,
            "Post-mix reverb input damping should preserve the first reverb onset");
        Require(onsetFrames > 4,
            "Post-mix reverb input damping should spread a sharp onset across multiple wet frames");
        Require(onsetEnergy > 0.0 && totalEnergy > onsetEnergy,
            "Post-mix reverb input damping should leave energy for the later tail");
    }

    void TestPostMixEffectsWetReturnShapeKeepsPeaksBounded() {
        PostMixEffects naturalEffects;
        naturalEffects.Init(44100);
        PostMixEffects hotEffects;
        hotEffects.Init(44100);

        double naturalPeak = 0.0;
        double hotPeak = 0.0;
        for (int i = 0; i < 12000; ++i) {
            const f32 naturalDry = (i == 0) ? 1.0f : 0.0f;
            const auto naturalOut =
                naturalEffects.ProcessSample(naturalDry, naturalDry, 0.0f, 0.0f, 0.0f, 0.0f);
            naturalPeak = std::max<double>(naturalPeak,
                std::max(std::fabs(naturalOut.wetL), std::fabs(naturalOut.wetR)));

            const f32 hotSend = (i == 0) ? 16.0f : 0.0f;
            const auto hotOut =
                hotEffects.ProcessSample(0.0f, 0.0f, hotSend, hotSend, hotSend, hotSend);
            hotPeak = std::max<double>(hotPeak,
                std::max(std::fabs(hotOut.wetL), std::fabs(hotOut.wetR)));
        }

        Require(naturalPeak > 0.0,
            "Post-mix wet return shaping should preserve normal effect tails");
        Require(hotPeak > naturalPeak,
            "Post-mix wet return shaping should still respond to stronger effect sends");
        Require(hotPeak < 8.0,
            "Post-mix wet return shaping should keep excessive effect-send peaks bounded");
    }

    void TestPostMixEffectsWetReturnKeepsStereoWidth() {
        PostMixEffects effects;
        effects.Init(44100);

        double midEnergy = 0.0;
        double sideEnergy = 0.0;
        for (int i = 0; i < 12000; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            midEnergy += std::fabs(out.wetL + out.wetR);
            sideEnergy += std::fabs(out.wetL - out.wetR);
        }

        Require(midEnergy > 0.0,
            "Post-mix wet return width should preserve mono-compatible effect energy");
        Require(sideEnergy > 0.0,
            "Post-mix wet return width should preserve stereo spread from the effect tank");
    }

    void TestPostMixEffectsWetReturnWidthLimitsSideBias() {
        PostMixEffects effects;
        effects.Init(44100);

        double midEnergy = 0.0;
        double sideEnergy = 0.0;
        for (int i = 0; i < 12000; ++i) {
            const f32 reverbSendL = (i == 0) ? 16.0f : 0.0f;
            const auto out = effects.ProcessSample(0.0f, 0.0f, reverbSendL, 0.0f, 0.0f, 0.0f);
            midEnergy += std::fabs((out.wetL + out.wetR) * 0.5f);
            sideEnergy += std::fabs((out.wetL - out.wetR) * 0.5f);
        }

        Require(sideEnergy > 0.0,
            "Post-mix wet return side limiter should preserve stereo side energy");
        Require(sideEnergy < midEnergy * 1.35,
            "Post-mix wet return side limiter should avoid excessive side bias");
    }

    void TestPostMixEffectsWetReturnShapeUsesLinkedStereoGain() {
        const auto measureSideToMid = [](f32 sendScale) {
            PostMixEffects effects;
            effects.Init(44100);

            double midEnergy = 0.0;
            double sideEnergy = 0.0;
            for (int i = 0; i < 18000; ++i) {
                const f32 sendL = (i == 0) ? sendScale : 0.0f;
                const f32 sendR = (i == 0) ? sendScale * 0.25f : 0.0f;
                const auto out = effects.ProcessSample(0.0f, 0.0f, sendL, sendR, sendL, sendR);
                midEnergy += std::fabs((out.wetL + out.wetR) * 0.5f);
                sideEnergy += std::fabs((out.wetL - out.wetR) * 0.5f);
            }

            Require(midEnergy > 0.0 && sideEnergy > 0.0,
                "Post-mix linked wet-return shape should preserve asymmetric wet energy");
            return sideEnergy / midEnergy;
        };

        const double normalRatio = measureSideToMid(1.0f);
        const double hotRatio = measureSideToMid(16.0f);

        Require(hotRatio < normalRatio * 1.20,
            "Post-mix linked wet-return shape should avoid exaggerating stereo imbalance on hot returns");
    }

    void TestPostMixEffectsWetReturnDcBlockResetsCleanly() {
        PostMixEffects effects;
        effects.Init(44100);

        double wetEnergy = 0.0;
        for (int i = 0; i < 24000; ++i) {
            const auto out = effects.ProcessSample(0.0f, 0.0f, 0.85f, 0.85f, 0.85f, 0.85f);
            wetEnergy += std::fabs(out.wetL) + std::fabs(out.wetR);
        }

        Require(wetEnergy > 0.0,
            "Post-mix wet return DC block should preserve sustained wet-return energy");

        effects.ResetAudioState();
        const auto silentAfterReset = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        Require(silentAfterReset.wetL == 0.0f && silentAfterReset.wetR == 0.0f,
            "Post-mix audio reset should clear wet-return DC block output state");
        Require(!effects.HasAudibleTail(1.0e-7f),
            "Post-mix reset should clear wet-return DC block state from tail detection");
    }

    void TestPostMixEffectsReverbToneDampingSmoothsTail() {
        PostMixEffects effects;
        effects.Init(44100);

        int observedFrames = 0;
        double maxStep = 0.0;
        f32 previous = 0.0f;
        bool havePrevious = false;
        for (int i = 0; i < 12000; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            const f32 wet = (out.wetL + out.wetR) * 0.5f;
            if (std::fabs(wet) > 1.0e-7f) {
                if (havePrevious) {
                    maxStep = std::max<double>(maxStep, std::fabs(wet - previous));
                }
                previous = wet;
                havePrevious = true;
                ++observedFrames;
            }
        }

        Require(observedFrames > 8,
            "Post-mix reverb tone damping should preserve the reverb tail");
        Require(maxStep < 0.12,
            "Post-mix reverb tone damping should avoid abrupt wet-tail jumps");

        effects.ResetState();
        const auto silentAfterReset = effects.ProcessSample(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        Require(silentAfterReset.wetL == 0.0f && silentAfterReset.wetR == 0.0f,
            "Post-mix effects reset should clear reverb tone damping state");
    }

    void TestPostMixEffectsReverbLowTrimKeepsTailBalanced() {
        PostMixEffects effects;
        effects.Init(44100);

        double absEnergy = 0.0;
        double signedEnergy = 0.0;
        for (int i = 0; i < 24000; ++i) {
            const f32 dry = (i == 0) ? 1.0f : 0.0f;
            const auto out = effects.ProcessSample(dry, dry, 0.0f, 0.0f, 0.0f, 0.0f);
            const f32 wet = (out.wetL + out.wetR) * 0.5f;
            if (i > 2000) {
                absEnergy += std::fabs(wet);
                signedEnergy += wet;
            }
        }

        Require(absEnergy > 0.0,
            "Post-mix reverb low trim should preserve audible reverb tail energy");
        Require(std::fabs(signedEnergy) < absEnergy * 0.95,
            "Post-mix reverb low trim should avoid a strongly biased late tail");

        effects.ResetState();
        Require(!effects.HasAudibleTail(1.0e-7f),
            "Post-mix reset should clear reverb low-trim state from tail detection");
    }

    void TestSynthCompatCanDisableInternalEffects() {
        SynthCompatOptions options;
        Require(!options.disableInternalEffects,
            "Internal effects should remain enabled by default for compatibility");
        options.disableInternalEffects = true;
        Require(options.disableInternalEffects,
            "Synth compatibility options should expose internal effects disable switch");
    }

    void TestSynthesizerCanDisableInternalEffectsTail() {
        MinimalSf2Config config;
        config.instGens.push_back(MakeSignedGen(GEN_ReverbEffectsSend, 1000));
        config.instGens.push_back(MakeSignedGen(GEN_ChorusEffectsSend, 1000));
        const std::vector<u8> sf2Bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(sf2Bytes.data(), sf2Bytes.size()), sf2.ErrorMessage().c_str());

        const std::vector<u8> midiBytes = BuildSingleNoteMidi();
        MidiFile midi;
        Require(midi.LoadFromMemory(midiBytes.data(), midiBytes.size()), midi.ErrorMessage().c_str());

        const auto renderUntilFinished = [&](const SynthCompatOptions& options) {
            Synthesizer synth;
            Require(synth.Init(&midi, &sf2, 44100, 2, options), synth.ErrorMessage().c_str());
            std::array<i16, 1024> buffer{};
            u64 frames = 0;
            for (int i = 0; i < 400 && !synth.IsFinished(); ++i) {
                const u32 written = synth.Render(buffer.data(), 512);
                frames += written;
                if (written == 0) {
                    break;
                }
            }
            Require(synth.IsFinished(),
                "Synthesizer should finish the short internal-effects test MIDI within the guard window");
            return frames;
        };

        SynthCompatOptions enabledOptions;
        SynthCompatOptions disabledOptions;
        disabledOptions.disableInternalEffects = true;
        const u64 enabledFrames = renderUntilFinished(enabledOptions);
        const u64 disabledFrames = renderUntilFinished(disabledOptions);

        Require(disabledFrames < enabledFrames,
            "Disabling internal effects should bypass post-mix reverb/chorus tail rendering");
    }

    void TestPublicCompatibilityFlagsRemainStable() {
        Require(XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER == (1u << 0),
            "Public SF2 zero-length loop compatibility flag value should remain stable");
        Require(XAME_COMPAT_ENABLE_SF2_SAMPLE_PITCH_CORRECTION == (1u << 1),
            "Public SF2 sample pitch correction flag value should remain stable");
        Require(XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS == (1u << 2),
            "Public SF2/MIDI effects-send multiply flag value should remain stable");
        Require(XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS == (1u << 3),
            "Public SF2 channel default modulator flag value should remain stable");
        Require(XAME_COMPAT_ENABLE_ENHANCED_OUTPUT_STAGE == (1u << 4),
            "Public enhanced output-stage flag value should remain stable");
        Require(XAME_COMPAT_ENHANCED_OUTPUT_STAGE_NATURAL == (1u << 5),
            "Public natural output-stage preset flag value should remain stable");
        Require(XAME_COMPAT_ENHANCED_OUTPUT_STAGE_WARM == (1u << 6),
            "Public warm output-stage preset flag value should remain stable");
        Require(XAME_COMPAT_DISABLE_INTERNAL_EFFECTS == (1u << 7),
            "Public internal effects disable flag value should remain stable");
        Require(XAME_COMPAT_USE_SF2_SPEC_MODULATOR_RESOLVER == (1u << 8),
            "Public SF2 spec modulator resolver flag value should remain stable");
    }

    void TestNegativeSampleOffsetsArePreserved() {
        MinimalSf2Config config;
        config.instGens.push_back(MakeSignedGen(GEN_StartAddrsOffset, -4));
        config.instGens.push_back(MakeSignedGen(GEN_EndAddrsOffset, -2));
        config.instGens.push_back(MakeSignedGen(GEN_StartloopAddrsOffset, -3));
        config.instGens.push_back(MakeSignedGen(GEN_EndloopAddrsOffset, -1));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_StartAddrsOffset] == -4,
            "Negative start address offsets should survive zone resolution");
        Require(zone.generators[GEN_EndAddrsOffset] == -2,
            "Negative end address offsets should survive zone resolution");
        Require(zone.generators[GEN_StartloopAddrsOffset] == -3,
            "Negative loop-start offsets should survive zone resolution");
        Require(zone.generators[GEN_EndloopAddrsOffset] == -1,
            "Negative loop-end offsets should survive zone resolution");

        Voice voice;
        voice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 44100, 0.0,
                     SoundBankKind::Sf2, SynthCompatOptions{});
        Require(voice.active, "Voice with negative sample offsets should still activate");
        Require(voice.samplePosFixed == static_cast<i64>(sf2.SampleHeaders(0)->start) * (1ll << 32),
            "Negative start offset should survive resolution without underflowing below sample start");
        Require(voice.sampleEnd == sf2.SampleHeaders(0)->end - 2,
            "Negative end offset should shorten the playable sample end");
        Require(voice.loopStart == sf2.SampleHeaders(0)->loopStart - 3,
            "Negative loop-start offset should move the loop earlier");
        Require(voice.loopEnd == sf2.SampleHeaders(0)->loopEnd - 1,
            "Negative loop-end offset should move the loop end earlier");
    }

    void TestSampleGeneratorModulatorDestinationsIgnored() {
        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(0, GEN_StartAddrsOffset, 2, 0, 0));
        config.instMods.push_back(MakeMod(0, GEN_EndAddrsOffset, -3, 0, 0));
        config.instMods.push_back(MakeMod(0, GEN_StartloopAddrsOffset, 1, 0, 0));
        config.instMods.push_back(MakeMod(0, GEN_EndloopAddrsOffset, -2, 0, 0));
        config.instMods.push_back(MakeMod(0, GEN_SampleModes, 1, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());
        Require(sf2.UnsupportedModulatorCount() == 5,
            "Sample generator modulator destinations should be reported as unsupported");

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);
        Require(zone.generators[GEN_StartAddrsOffset] == 0,
            "Start address modulator destination should be ignored in spec-compliant mode");
        Require(zone.generators[GEN_EndAddrsOffset] == 0,
            "End address modulator destination should be ignored in spec-compliant mode");
        Require(zone.generators[GEN_StartloopAddrsOffset] == 0,
            "Loop-start modulator destination should be ignored in spec-compliant mode");
        Require(zone.generators[GEN_EndloopAddrsOffset] == 0,
            "Loop-end modulator destination should be ignored in spec-compliant mode");
        Require(zone.generators[GEN_SampleModes] == 0,
            "SampleModes modulator destination should be ignored in spec-compliant mode");

        Voice voice;
        voice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 44100, 0.0,
                     SoundBankKind::Sf2, SynthCompatOptions{});
        Require(voice.active, "Voice with ignored sample generator modulators should activate");
        Require(!voice.looping, "Ignored sampleModes modulator should not enable looping");
    }

    void TestSf2FifthLayerStaysIndependent() {
        std::array<i16, 96> sampleData{};
        for (size_t i = 0; i < sampleData.size(); ++i) {
            sampleData[i] = static_cast<i16>(1000 + static_cast<i16>(i * 32));
        }

        SampleHeader sample{};
        std::strncpy(sample.sampleName, "Layer", sizeof(sample.sampleName));
        sample.start = 0;
        sample.end = 64;
        sample.loopStart = 8;
        sample.loopEnd = 56;
        sample.sampleRate = 44100;
        sample.originalPitch = 60;
        sample.sampleType = 1;

        ResolvedZone zone0{};
        ResolvedZone zone1{};
        std::memcpy(zone0.generators, GetSF2GeneratorDefaults(), sizeof(zone0.generators));
        std::memcpy(zone1.generators, GetSF2GeneratorDefaults(), sizeof(zone1.generators));
        zone0.sample = &sample;
        zone1.sample = &sample;
        zone1.generators[GEN_CoarseTune] = 7;
        zone1.generators[GEN_KeyRange] = 0x6400;
        zone1.generators[GEN_InitialAttenuation] = 175;
        zone1.generators[GEN_StartloopAddrsOffset] = 3;

        std::vector<ResolvedZone> zones = { zone0, zone1 };
        VoicePool pool;
        pool.NoteOn(zones,
                    sampleData.data(),
                    nullptr,
                    sampleData.size(),
                    0,
                    0,
                    0,
                    60,
                    65535,
                    44100,
                    0.0,
                    1.0f,
                    0x81020408u,
                    0x50A14285u,
                    0u,
                    SoundBankKind::Sf2,
                    SynthCompatOptions{});

        auto& probe = reinterpret_cast<VoicePoolProbe&>(pool);
        Require(probe.activeCount_ == 2, "SF2 fifth layers should remain independent program layers");
        Voice& root = probe.voices_[probe.activeIndices_[0]];
        Voice& layerB = probe.voices_[probe.activeIndices_[1]];
        Require(!root.HasLinkedVoice(), "Independent SF2 fifth layers should not be linked voices");
        Require(root.sampleStepFixed != layerB.sampleStepFixed,
            "Layer B should preserve its +7 semitone playback step");
        Require(layerB.attenuation < root.attenuation * 0.2f,
            "Layer B should preserve its -17.5 dB attenuation");
        Require(layerB.loopStart == sample.loopStart + 3,
            "Layer B should preserve its loop start offset");
    }

    void TestSpecialSf2RouteClampSurvivesControllerRefresh() {
        std::array<i16, 96> sampleData{};
        for (size_t i = 0; i < sampleData.size(); ++i) {
            sampleData[i] = static_cast<i16>(1000 + static_cast<i16>(i * 32));
        }

        SampleHeader sample{};
        std::strncpy(sample.sampleName, "Layer", sizeof(sample.sampleName));
        sample.start = 0;
        sample.end = 64;
        sample.loopStart = 8;
        sample.loopEnd = 56;
        sample.sampleRate = 44100;
        sample.originalPitch = 60;
        sample.sampleType = 1;

        ResolvedZone zone{};
        std::memcpy(zone.generators, GetSF2GeneratorDefaults(), sizeof(zone.generators));
        zone.sample = &sample;
        zone.sampleId = 0;
        zone.presetBagIndex = 0;
        zone.instrumentBagIndex = 0;

        SpecialVoiceRoute route{};
        route.enabled = true;
        route.clampAboveRoot = true;
        route.clampRootKey = 60;

        Voice voice;
        voice.NoteOn(zone, sampleData.data(), nullptr, sampleData.size(), 0, 0, 0, 72, 65535, 1, 44100, 0.0,
                     SoundBankKind::Sf2, SynthCompatOptions{}, route);
        Require(voice.active, "Special-route voice should activate");
        const i64 initialStep = voice.sampleStepFixed;
        voice.RefreshResolvedZoneControllers(zone);
        Require(voice.sampleStepFixed == initialStep,
            "Special-route clamp should survive controller refresh recalculation");
    }

    void TestProgramLayerRefreshMatchesZoneIdentity() {
        const std::vector<u8> bytes = BuildLayeredSameSampleSf2(-500, 500);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        Require(sf2.FindZones(0, 0, 60, 65535, zones, nullptr), "Layered SF2 should resolve zones");
        Require(zones.size() == 2, "Layered SF2 should expose two local zones");
        Require(zones[0].sample == zones[1].sample, "Layered SF2 test should share one sample");
        Require(zones[0].instrumentBagIndex != zones[1].instrumentBagIndex,
            "Layered SF2 test should use distinct instrument zones");

        VoicePool pool;
        pool.NoteOn(zones,
                    sf2.SampleData(),
                    sf2.SampleData24(),
                    sf2.SampleDataCount(),
                    0,
                    0,
                    0,
                    60,
                    65535,
                    44100,
                    0.0,
                    1.0f,
                    0x81020408u,
                    0x50A14285u,
                    0u,
                    SoundBankKind::Sf2,
                    SynthCompatOptions{});
        Require(pool.ActiveCount() == 2, "Non-aggregated same-sample layers should create two root voices");

        auto& probe = reinterpret_cast<VoicePoolProbe&>(pool);
        Voice& first = probe.voices_[probe.activeIndices_[0]];
        Voice& second = probe.voices_[probe.activeIndices_[1]];
        const bool hasLeftRightBefore =
            (first.baseGainL > first.baseGainR && second.baseGainR > second.baseGainL) ||
            (first.baseGainR > first.baseGainL && second.baseGainL > second.baseGainR);
        Require(hasLeftRightBefore, "Layered same-sample voices should start with distinct pan lanes");

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        pool.RefreshSf2Controllers(0, sf2, ctx, 1.0f, 0x81020408u, 0x50A14285u, 0u);

        const bool hasLeftRightAfter =
            (first.baseGainL > first.baseGainR && second.baseGainR > second.baseGainL) ||
            (first.baseGainR > first.baseGainL && second.baseGainL > second.baseGainR);
        Require(hasLeftRightAfter,
            "Controller refresh should preserve layer-specific zone identity for shared samples");
    }

    void TestExclusiveClassRespectsProgramLayerZone() {
        const std::vector<u8> bytes = BuildLayeredSameSampleSf2(-250, 250, 1, 0);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        Require(sf2.FindZones(0, 0, 60, 65535, zones, nullptr), "Layered SF2 should resolve zones");
        Require(zones.size() == 2, "Layered SF2 should expose two local zones");

        VoicePool pool;
        pool.NoteOn(zones,
                    sf2.SampleData(),
                    sf2.SampleData24(),
                    sf2.SampleDataCount(),
                    0,
                    0,
                    0,
                    60,
                    65535,
                    44100,
                    0.0,
                    1.0f,
                    0x81020408u,
                    0x50A14285u,
                    0u,
                    SoundBankKind::Sf2,
                    SynthCompatOptions{});
        Require(pool.ActiveCount() == 2, "Layered exclusive-class SF2 should create two root voices");

        auto& probe = reinterpret_cast<VoicePoolProbe&>(pool);
        const u8 exc0 = probe.voices_[probe.activeIndices_[0]].exclusiveClass;
        const u8 exc1 = probe.voices_[probe.activeIndices_[1]].exclusiveClass;
        Require((exc0 == 1 && exc1 == 0) || (exc0 == 0 && exc1 == 1),
            "ExclusiveClass should remain per-zone after ProgramLayer expansion");
    }

    void TestRomOverrideUsesOverrideSampleLimit() {
        std::array<i16, 128> basePcm{};
        std::array<i16, 24> romPcm{};
        for (size_t i = 0; i < basePcm.size(); ++i) {
            basePcm[i] = static_cast<i16>(i);
        }
        for (size_t i = 0; i < romPcm.size(); ++i) {
            romPcm[i] = static_cast<i16>(i * 3);
        }

        SampleHeader sample{};
        std::strncpy(sample.sampleName, "ROM", sizeof(sample.sampleName));
        sample.start = 0;
        sample.end = 64;
        sample.loopStart = 8;
        sample.loopEnd = 16;
        sample.sampleRate = 44100;
        sample.originalPitch = 60;
        sample.sampleType = 0x8001u;

        ResolvedZone zone{};
        std::memcpy(zone.generators, GetSF2GeneratorDefaults(), sizeof(zone.generators));
        zone.sample = &sample;
        zone.sampleDataOverride = romPcm.data();
        zone.sampleDataOverrideCount = romPcm.size();
        zone.sampleId = 0;
        zone.presetBagIndex = 0;
        zone.instrumentBagIndex = 0;

        Voice voice;
        voice.NoteOn(zone, basePcm.data(), nullptr, basePcm.size(), 0, 0, 0, 60, 65535, 1, 44100, 0.0,
                     SoundBankKind::Sf2, SynthCompatOptions{});
        Require(voice.active, "ROM override voice should activate when override sample data is available");
        Require(voice.sampleEnd == romPcm.size(),
            "ROM override playback range should be clamped by override sample data length");
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
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc7ToInitialAttenuation = true;
            ctx.ccValues[7] = 0;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_InitialAttenuation] == 960,
                "CC7 default should drive initial attenuation");
        }

        {
            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc10ToPan = true;
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
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc11ToInitialAttenuation = true;
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

    void TestSf2SplitDefaultModulatorCompatibility() {
        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        constexpr u8 key = 60;
        constexpr u16 velocity = 32768;
        constexpr i32 baseFilterFc = 13500;

        ModulatorContext offCtx{};
        SetDefaultMidiControllers(offCtx);
        offCtx.ccValues[7] = 0;
        offCtx.ccValues[10] = 127;
        offCtx.ccValues[11] = 0;
        offCtx.ccValues[91] = 127;
        offCtx.ccValues[93] = 127;

        ModulatorContext onCtx = offCtx;
        onCtx.applySf2ChannelDefaults = true;

        std::vector<ResolvedZone> zones;
        const ResolvedZone offZone = RequireSingleZone(sf2, key, velocity, &offCtx, zones);
        Require(offZone.generators[GEN_InitialAttenuation] == 0,
            "SF2 defaults OFF should not apply velocity/CC attenuation deltas");
        Require(offZone.generators[GEN_InitialFilterFc] == baseFilterFc,
            "SF2 defaults OFF should not apply velocity filter cutoff delta");
        Require(offZone.generators[GEN_Pan] == 0,
            "SF2 defaults OFF should not apply CC10 pan delta");
        Require(offZone.generators[GEN_ReverbEffectsSend] == 0,
            "SF2 defaults OFF should not apply CC91 reverb send delta");
        Require(offZone.generators[GEN_ChorusEffectsSend] == 0,
            "SF2 defaults OFF should not apply CC93 chorus send delta");

        const ResolvedZone onZone = RequireSingleZone(sf2, key, velocity, &onCtx, zones);
        Require(onZone.generators[GEN_InitialAttenuation] == 0,
            "SF2 defaults ON should not apply velocity/CC attenuation split defaults unless enabled");
        Require(onZone.generators[GEN_InitialFilterFc] == ExpectedVelocityFilterCutoff(baseFilterFc, velocity),
            "SF2 defaults ON should apply only the velocity filter cutoff delta by default");
        Require(onZone.generators[GEN_Pan] == 0,
            "SF2 defaults ON should not apply CC10 pan unless its split flag is enabled");
        Require(onZone.generators[GEN_ReverbEffectsSend] == 200,
            "SF2 defaults ON should apply CC91 reverb send by default");
        Require(onZone.generators[GEN_ChorusEffectsSend] == 200,
            "SF2 defaults ON should apply CC93 chorus send by default");

        ModulatorContext splitCtx = onCtx;
        splitCtx.applySf2VelocityToInitialAttenuation = true;
        splitCtx.applySf2Cc7ToInitialAttenuation = true;
        splitCtx.applySf2Cc10ToPan = true;
        splitCtx.applySf2Cc11ToInitialAttenuation = true;
        const ResolvedZone splitZone = RequireSingleZone(sf2, key, velocity, &splitCtx, zones);
        Require(splitZone.generators[GEN_InitialAttenuation] ==
                std::clamp(ExpectedVelocityAttenuationCb(velocity) + 1920, 0, 1440),
            "Velocity, CC7, and CC11 attenuation defaults should apply only when split flags are enabled");
        Require(splitZone.generators[GEN_Pan] == 500,
            "CC10 pan default should apply only when its split flag is enabled");

        SynthCompatOptions offOptions{};
        SynthCompatOptions onOptions{};
        onOptions.applySf2ChannelDefaults = true;

        Voice offVoice;
        offVoice.NoteOn(offZone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(),
            0, 0, 0, key, velocity, 1, 44100, 0.0, SoundBankKind::Sf2, offOptions);
        offVoice.UpdateChannelMix(0.5f, FloatToU32(0.75f), FloatToU32(0.25f), FloatToU32(0.5f));

        Voice onVoice;
        onVoice.NoteOn(onZone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(),
            0, 0, 0, key, velocity, 1, 44100, 0.0, SoundBankKind::Sf2, onOptions);
        onVoice.UpdateChannelMix(0.5f, FloatToU32(0.75f), FloatToU32(0.25f), FloatToU32(0.5f));

        Require(NearlyEqual(offVoice.channelGainL, onVoice.channelGainL, 1.0e-6),
            "SF2 defaults OFF/ON should leave channelGainL controlled by UpdateChannelMix");
        Require(NearlyEqual(offVoice.channelGainR, onVoice.channelGainR, 1.0e-6),
            "SF2 defaults OFF/ON should leave channelGainR controlled by UpdateChannelMix");
        Require(NearlyEqual(offVoice.reverbSend, 0.0f, 1.0e-6),
            "SF2 defaults OFF should leave final reverb send at the preset value");
        Require(NearlyEqual(offVoice.chorusSend, 0.0f, 1.0e-6),
            "SF2 defaults OFF should leave final chorus send at the preset value");
        Require(NearlyEqual(onVoice.reverbSend, 0.2f, 1.0e-6),
            "SF2 defaults ON should expose the CC91 default as final reverb send");
        Require(NearlyEqual(onVoice.chorusSend, 0.2f, 1.0e-6),
            "SF2 defaults ON should expose the CC93 default as final chorus send");
    }

    void TestDefaultModulatorHierarchySemantics() {
        {
            MinimalSf2Config config;
            config.instMods.push_back(MakeMod(0x028A, GEN_Pan, 100, 0, 0));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc10ToPan = true;
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
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc10ToPan = true;
            ctx.ccValues[10] = 80;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_Pan] == 286,
                "Preset-level explicit default mod should add to the implicit default");
        }

        {
            MinimalSf2Config config;
            config.instMods.push_back(MakeMod(0x028A, GEN_Pan, 100, 0, 0));
            config.presetMods.push_back(MakeMod(0x028A, GEN_Pan, 100, 0, 0));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc10ToPan = true;
            ctx.ccValues[10] = 80;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_Pan] == 52,
                "Preset-level identical mod should add to the instrument-level mod amount");
        }

        {
            MinimalSf2Config config;
            config.instMods.push_back(MakeMod(0x028A, GEN_Pan, 100, 0, 7));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.applySf2ChannelDefaults = true;
            ctx.applySf2Cc10ToPan = true;
            ctx.ccValues[10] = 80;
            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
            Require(zone.generators[GEN_Pan] == 260,
                "Invalid or unevaluated instrument modulator should not suppress the implicit default");
        }
    }

    void TestStereoSampleLinks() {
        const std::vector<u8> bytes = BuildStereoLinkedSf2();
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());
        Require(sf2.SampleHeaderCount() >= 2, "Stereo SF2 should expose sample headers");
        Require(sf2.SampleDataCount() >= 128, "Stereo SF2 should expose sample PCM");
        Require(sf2.SampleData()[0] == 1000, "Left stereo sample PCM should be preserved");
        Require(sf2.SampleData()[110] == -1000, "Right stereo sample PCM should be preserved");
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

    void TestParallelRenderClearsFinishedLinkedVoice() {
        if (std::thread::hardware_concurrency() <= 1) {
            return;
        }

        const std::vector<u8> bytes = BuildStereoLinkedSf2WithLengths(4096, 32);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        Require(sf2.FindZones(0, 0, 48, 65535, zones, nullptr), "Stereo SF2 should resolve zones for parallel cleanup test");
        Require(zones.size() == 2, "Parallel cleanup test should resolve an explicit stereo pair");

        VoicePool pool;
        SynthCompatOptions compatOptions{};
        for (u8 key = 36; key < 60; ++key) {
            pool.NoteOn(zones,
                        sf2.SampleData(),
                        sf2.SampleData24(),
                        sf2.SampleDataCount(),
                        0,
                        0,
                        0,
                        key,
                        65535,
                        44100,
                        0.0,
                        1.0f,
                        0x81020408u,
                        0x50A14285u,
                        0u,
                        SoundBankKind::Sf2,
                        compatOptions);
        }
        Require(pool.ActiveCount() == 24, "Parallel cleanup test should create 24 root voices");

        std::array<f32, 256> outL{};
        std::array<f32, 256> outR{};
        std::array<f32, 256> reverbL{};
        std::array<f32, 256> reverbR{};
        std::array<f32, 256> chorusL{};
        std::array<f32, 256> chorusR{};
        pool.RenderBlock(outL.data(), outR.data(), reverbL.data(), reverbR.data(), chorusL.data(), chorusR.data(),
                         static_cast<u32>(outL.size()));

        auto& probe = reinterpret_cast<VoicePoolProbe&>(pool);
        Require(probe.activeCount_ > 0, "At least one root voice should remain after parallel render");
        for (u16 i = 0; i < probe.activeCount_; ++i) {
            const Voice& root = probe.voices_[probe.activeIndices_[i]];
            Require(root.active, "Parallel cleanup test should retain active root voices");
            Require(!root.HasLinkedVoice(),
                "Finished linked voice should be detached after parallel RenderBlock cleanup");
        }
    }

    void TestSourceCurvesSupport() {
        const u16 velocityConcave = static_cast<u16>(2u | (1u << 10));
        const u16 velocityConvex = static_cast<u16>(2u | (2u << 10));
        const u16 velocitySwitch = static_cast<u16>(2u | (3u << 10));

        {
            MinimalSf2Config config;
            config.instMods.push_back(MakeMod(velocityConcave, GEN_InitialFilterQ, 500, 0, 0));
            config.instMods.push_back(MakeMod(velocityConvex, GEN_ModLfoToPitch, 500, 0, 0));
            config.instMods.push_back(MakeMod(velocitySwitch, GEN_ChorusEffectsSend, 500, 0, 0));

            const std::vector<u8> bytes = BuildMinimalSf2(config);
            Sf2File sf2;
            Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

            ModulatorContext ctx{};
            SetDefaultMidiControllers(ctx);
            ctx.useSf2SpecModulatorResolver = true;

            std::vector<ResolvedZone> zones;
            const ResolvedZone& zone = RequireSingleZone(sf2, 60, 32768, &ctx, zones);
            Require(zone.generators[GEN_InitialFilterQ] == 63,
                "Concave source curve should map mid velocity to ~0.125 (96dB scale)");
            Require(zone.generators[GEN_ModLfoToPitch] == 437,
                "Convex source curve should map mid velocity to ~0.875 (96dB scale)");
            Require(zone.generators[GEN_ChorusEffectsSend] == 500,
                "Switch source curve should step to 1.0 at mid velocity");
        }
    }

    void TestSourceCurvesQuarterPoints() {
        const u16 velocityConcave = static_cast<u16>(2u | (1u << 10));
        const u16 velocityConvex = static_cast<u16>(2u | (2u << 10));

        MinimalSf2Config config;
        config.instMods.push_back(MakeMod(velocityConcave, GEN_InitialFilterQ, 500, 0, 0));
        config.instMods.push_back(MakeMod(velocityConvex, GEN_ModLfoToPitch, 500, 0, 0));

        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        ctx.useSf2SpecModulatorResolver = true;

        std::vector<ResolvedZone> zones;
        const u16 quarterVelocity = 16384;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, quarterVelocity, &ctx, zones);
        const double x = static_cast<double>(quarterVelocity) / 65535.0;
        
        auto concaveFunc = [](double v) {
            if (v <= 0.0) return 0.0;
            if (v >= 1.0) return 1.0;
            return std::min(1.0, -40.0 / 96.0 * std::log10(1.0 - v));
        };
        auto convexFunc = [](double v) {
            if (v <= 0.0) return 0.0;
            if (v >= 1.0) return 1.0;
            return std::max(0.0, 1.0 + 40.0 / 96.0 * std::log10(v));
        };

        const i32 expectedConcave = static_cast<i32>(std::lround(500.0 * concaveFunc(x)));
        const i32 expectedConvex = static_cast<i32>(std::lround(500.0 * convexFunc(x)));
        Require(zone.generators[GEN_InitialFilterQ] == expectedConcave,
            "Concave source curve should follow the SF2 log-based characteristic");
        Require(zone.generators[GEN_ModLfoToPitch] == expectedConvex,
            "Convex source curve should mirror the SF2 concave curve");
    }

    void TestSf2NrpnGeneratorOffsets() {
        ChannelState state{};
        state.HandleSf2NrpnControl(99, 120);
        state.HandleSf2NrpnControl(98, static_cast<u8>(GEN_InitialAttenuation));
        state.HandleSf2NrpnControl(38, 72);
        state.HandleSf2NrpnControl(6, 65);

        Require(state.sf2Nrpn.sf2Mode, "CC99=120 should enter SF2 NRPN mode");
        Require(state.sf2Nrpn.generatorIndex == GEN_InitialAttenuation,
            "CC98 should select the target SF2 generator");
        Require(state.sf2Nrpn.generatorOffsets[GEN_InitialAttenuation] == 200,
            "Data Entry should map to a centered SF2 generator offset");

        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        ModulatorContext ctx{};
        SetDefaultMidiControllers(ctx);
        ctx.nrpnOffsets = state.sf2Nrpn.generatorOffsets;
        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, &ctx, zones);
        Require(zone.generators[GEN_InitialAttenuation] == 200,
            "SF2 NRPN offsets should be added during zone resolution");

        state.HandleSf2NrpnControl(121, 0);
        Require(!state.sf2Nrpn.sf2Mode, "Reset All Controllers should leave SF2 NRPN mode");
        Require(state.sf2Nrpn.generatorOffsets[GEN_InitialAttenuation] == 0,
            "Reset All Controllers should clear SF2 NRPN offsets");
    }

    void TestSoftPedalAffectsNewNoteOnOnly() {
        MinimalSf2Config config;
        const std::vector<u8> bytes = BuildMinimalSf2(config);
        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), sf2.ErrorMessage().c_str());

        std::vector<ResolvedZone> zones;
        const ResolvedZone& zone = RequireSingleZone(sf2, 60, 65535, nullptr, zones);

        Voice normalVoice;
        normalVoice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 48000, 0.0,
                           SoundBankKind::Sf2, SynthCompatOptions{});

        Voice softVoice;
        softVoice.NoteOn(zone, sf2.SampleData(), sf2.SampleData24(), sf2.SampleDataCount(), 0, 0, 0, 60, 65535, 1, 48000, 0.0,
                         SoundBankKind::Sf2, SynthCompatOptions{}, {}, -1, 0, true);

        Require(softVoice.filterBaseFcCents == normalVoice.filterBaseFcCents - 200,
            "Soft pedal should lower the initial filter cutoff for new voices");
        Require(softVoice.attenuation < normalVoice.attenuation,
            "Soft pedal should increase attenuation for new voices");

        softVoice.RefreshResolvedZoneControllers(zone);
        Require(softVoice.filterBaseFcCents == normalVoice.filterBaseFcCents,
            "Controller refresh without a new NoteOn should not keep reapplying soft pedal");
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
        const u32 expectedSm24Size = 110;

        std::vector<u8> sm24Chunk = { 's', 'm', '2', '4' };
        AppendU32LE(sm24Chunk, expectedSm24Size);
        sm24Chunk.resize(sm24Chunk.size() + expectedSm24Size, 0x7F);
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
        const u32 expectedSm24Size = 110;
        const size_t infoPos = FindListChunk(bytes, "INFO");
        Require(infoPos != std::numeric_limits<size_t>::max(), "INFO list should exist");
        const size_t ifilPos = infoPos + 12;
        Require(std::memcmp(bytes.data() + ifilPos, "ifil", 4) == 0, "ifil chunk should be first in INFO");
        bytes[ifilPos + 8] = 2;
        bytes[ifilPos + 9] = 0;
        bytes[ifilPos + 10] = 1;
        bytes[ifilPos + 11] = 0;

        std::vector<u8> sm24Chunk = { 's', 'm', '2', '4' };
        AppendU32LE(sm24Chunk, expectedSm24Size);
        sm24Chunk.resize(sm24Chunk.size() + expectedSm24Size, 0);
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

    void TestSm24BeforeSmplAccepted() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        std::vector<u8> sm24Chunk = { 's', 'm', '2', '4' };
        AppendU32LE(sm24Chunk, 110);
        sm24Chunk.resize(sm24Chunk.size() + 110, 0x55);

        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t sdtaFirstSubchunk = sdtaPos + 12;
        bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(sdtaFirstSubchunk), sm24Chunk.begin(), sm24Chunk.end());
        AddChunkSize(bytes, 4, static_cast<u32>(sm24Chunk.size()));
        AddChunkSize(bytes, sdtaPos + 4, static_cast<u32>(sm24Chunk.size()));

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()), "sm24 chunk before smpl should still be accepted");
        Require(!sf2.HasIgnoredSm24(), "valid sm24 should be accepted regardless of sdta subchunk order");
        Require(sf2.SampleData24() != nullptr, "valid sm24 should still produce a 24-bit sample pool");
        Require(sf2.SampleData24()[0] == ((static_cast<i32>(sf2.SampleData()[0]) << 8) | 0x55),
            "sm24 data should still be combined when the chunk appears before smpl");
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

    void TestSampleGuardPaddingIsAccepted() {
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
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "Samples without zero-filled trailing guard points should still load");
    }

    void TestSampleLoopGuardPointsAreAccepted() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        WriteLE32(bytes, shdrData + 28, 7);

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "Samples without eight valid points before loop start should still load");
    }

    void TestShortSampleIsAccepted() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t sdtaPos = FindListChunk(bytes, "sdta");
        Require(sdtaPos != std::numeric_limits<size_t>::max(), "sdta list should exist");
        const size_t smplPos = sdtaPos + 12;
        Require(std::memcmp(bytes.data() + smplPos, "smpl", 4) == 0, "smpl chunk should be first in sdta");
        const size_t smplData = smplPos + 8;
        for (size_t i = 40; i < 86; ++i) {
            bytes[smplData + i * sizeof(i16)] = 0;
            bytes[smplData + i * sizeof(i16) + 1] = 0;
        }
        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        WriteLE32(bytes, shdrData + 24, 40);
        WriteLE32(bytes, shdrData + 32, 40);

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "Samples shorter than the portable minimum should still load");
    }

    void TestShortLoopIsAccepted() {
        MinimalSf2Config config;
        std::vector<u8> bytes = BuildMinimalSf2(config);
        const size_t shdrPos = FindPdtaChunk(bytes, "shdr");
        Require(shdrPos != std::numeric_limits<size_t>::max(), "shdr chunk should exist");
        const size_t shdrData = shdrPos + 8;
        WriteLE32(bytes, shdrData + 28, 16);
        WriteLE32(bytes, shdrData + 32, 40);

        Sf2File sf2;
        Require(sf2.LoadFromMemory(bytes.data(), bytes.size()),
            "Samples with short loops should still load");
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

int main(int argc, char** argv) {
    const char* filter = (argc > 1) ? argv[1] : nullptr;
    auto shouldRun = [&](const char* name) {
        return !filter || std::strcmp(filter, name) == 0;
    };

#define RUN_TEST(name) do { if (shouldRun(#name)) { g_currentTestName = #name; name(); } } while (0)
    RUN_TEST(TestForcedVelocityDefaultModulators);
    RUN_TEST(TestDefaultVelocityModulatorsAreNotSuppressedByAmountSourceMods);
    RUN_TEST(TestSf2ModulatorResolverDefaultTableAndDestinations);
    RUN_TEST(TestSf2ModulatorResolverDestinationClasses);
    RUN_TEST(TestSf2ModulatorResolverHierarchySemantics);
    RUN_TEST(TestSf2ModulatorResolverInvalidModsDoNotSuppressDefaults);
    RUN_TEST(TestSf2ModulatorResolverSourceAndTransformRules);
    RUN_TEST(TestSf2ModulatorResolverLinkCyclesAreIgnored);
    RUN_TEST(TestSf2ModulatorResolverLinkedInputsEvaluate);
    RUN_TEST(TestSf2ModulatorResolverLinkedChains);
    RUN_TEST(TestSf2ModulatorResolverChainWithInvalidNodeIsIgnored);
    RUN_TEST(TestSf2SpecResolverOptInAppliesImplicitDefaults);
    RUN_TEST(TestSf2SpecResolverOptInPresetAddsToInstrument);
    RUN_TEST(TestSf2SpecResolverPitchWheelDefaultUsesSensitivityCents);
    RUN_TEST(TestAbsoluteTransformSupport);
    RUN_TEST(TestPresetZoneTerminalInstrumentRule);
    RUN_TEST(TestInstrumentZoneTerminalSampleRule);
    RUN_TEST(TestPresetLevelIllegalSampleGeneratorsIgnored);
    RUN_TEST(TestDuplicateModulatorsUseLastDefinition);
    RUN_TEST(TestLinkedModulatorsFeedTargetSource);
    RUN_TEST(TestUnsupportedTransformReporting);
    RUN_TEST(TestUnsupportedAmountSourceIgnored);
    RUN_TEST(TestInvalidLinkSourceIsReported);
    RUN_TEST(TestEffectsSendMixPolicy);
    RUN_TEST(TestOutputLimiterAvoidsCrossSampleDucking);
    RUN_TEST(TestOutputLimiterUsesLinkedStereoGain);
    RUN_TEST(TestEnhancedOutputStageAddsQuietLoudness);
    RUN_TEST(TestEnhancedOutputStageUsesLinkedPeakShaping);
    RUN_TEST(TestEnhancedOutputStageAdaptsToDensePassages);
    RUN_TEST(TestOutputStagePresetsHaveDistinctDrive);
    RUN_TEST(TestOutputStageSmoothingScalesWithSampleRate);
    RUN_TEST(TestOutputStageSampleRateAndModeOrderIsStable);
    RUN_TEST(TestOutputStageStandardMatchesLimiterPath);
    RUN_TEST(TestOutputStageMeterTracksRenderBlock);
    RUN_TEST(TestPostMixEffectsProducesAndResetsTail);
    RUN_TEST(TestPostMixEffectsProcessesChorusSend);
    RUN_TEST(TestPostMixEffectsUserMixScales);
    RUN_TEST(TestPostMixEffectsAudioResetPreservesGsState);
    RUN_TEST(TestPostMixEffectsGsWetChangesAreSmoothed);
    RUN_TEST(TestPostMixEffectsChorusToReverbChangesAreSmoothed);
    RUN_TEST(TestPostMixEffectsMasterReverbSendChangesAreSmoothed);
    RUN_TEST(TestPostMixEffectsChorusModulationChangesAreSmoothed);
    RUN_TEST(TestPostMixEffectsFeedbackChangesAreSmoothed);
    RUN_TEST(TestPostMixEffectsGsSmoothingScalesWithSampleRate);
    RUN_TEST(TestPostMixEffectsInputDampingScalesWithSampleRate);
    RUN_TEST(TestPostMixEffectsInternalDampingScalesWithSampleRate);
    RUN_TEST(TestPostMixEffectsChorusSecondaryTapThickensReturn);
    RUN_TEST(TestPostMixEffectsChorusToneDampingSmoothsReturn);
    RUN_TEST(TestPostMixEffectsChorusInputDampingSpreadsOnset);
    RUN_TEST(TestPostMixEffectsChorusRateScalesWithSampleRate);
    RUN_TEST(TestPostMixEffectsTailIncludesSmoothingState);
    RUN_TEST(TestPostMixEffectsFlushesTinyAudioState);
    RUN_TEST(TestPostMixEffectsFeedbackClampKeepsHotGsStable);
    RUN_TEST(TestPostMixEffectsInputShapeSoftensExtremeSends);
    RUN_TEST(TestPostMixEffectsShapesChorusToReverbSend);
    RUN_TEST(TestPostMixEffectsReverbDiffusionCreatesDenseTail);
    RUN_TEST(TestPostMixEffectsEarlyReflectionsArriveQuickly);
    RUN_TEST(TestPostMixEffectsReverbPredelaySeparatesOnset);
    RUN_TEST(TestPostMixEffectsReverbInputDampingSpreadsOnset);
    RUN_TEST(TestPostMixEffectsWetReturnShapeKeepsPeaksBounded);
    RUN_TEST(TestPostMixEffectsWetReturnKeepsStereoWidth);
    RUN_TEST(TestPostMixEffectsWetReturnWidthLimitsSideBias);
    RUN_TEST(TestPostMixEffectsWetReturnShapeUsesLinkedStereoGain);
    RUN_TEST(TestPostMixEffectsWetReturnDcBlockResetsCleanly);
    RUN_TEST(TestPostMixEffectsReverbToneDampingSmoothsTail);
    RUN_TEST(TestPostMixEffectsReverbLowTrimKeepsTailBalanced);
    RUN_TEST(TestSynthCompatCanDisableInternalEffects);
    RUN_TEST(TestSynthesizerCanDisableInternalEffectsTail);
    RUN_TEST(TestPublicCompatibilityFlagsRemainStable);
    RUN_TEST(TestNegativeSampleOffsetsArePreserved);
    RUN_TEST(TestSampleGeneratorModulatorDestinationsIgnored);
    RUN_TEST(TestSf2FifthLayerStaysIndependent);
    RUN_TEST(TestSpecialSf2RouteClampSurvivesControllerRefresh);
    RUN_TEST(TestSf2PitchPrecedence);
    RUN_TEST(TestEnvelopePitchAndKeynumScaling);
    RUN_TEST(TestEnvelopeReleaseRecalculation);
    RUN_TEST(TestFilterAndLfoInitialization);
    RUN_TEST(TestPressureSources);
    RUN_TEST(TestPitchWheelSensitivityAmountSource);
    RUN_TEST(TestRemainingDefaultModulators);
    RUN_TEST(TestSf2SplitDefaultModulatorCompatibility);
    RUN_TEST(TestDefaultModulatorHierarchySemantics);
    RUN_TEST(TestStereoSampleLinks);
    RUN_TEST(TestParallelRenderClearsFinishedLinkedVoice);
    RUN_TEST(TestProgramLayerRefreshMatchesZoneIdentity);
    RUN_TEST(TestExclusiveClassRespectsProgramLayerZone);
    RUN_TEST(TestRomOverrideUsesOverrideSampleLimit);
    RUN_TEST(TestSourceCurvesSupport);
    RUN_TEST(TestSourceCurvesQuarterPoints);
    RUN_TEST(TestSf2NrpnGeneratorOffsets);
    RUN_TEST(TestSoftPedalAffectsNewNoteOnOnly);
    RUN_TEST(TestVelocityZoneBoundary);
    RUN_TEST(TestSm24Detection);
    RUN_TEST(TestSm24RequiresIfil204);
    RUN_TEST(TestSm24SizeIgnored);
    RUN_TEST(TestSm24BeforeSmplAccepted);
    RUN_TEST(TestBagIndexHelpersSkipGlobalZones);
    RUN_TEST(TestMissingIfilRejected);
    RUN_TEST(TestMissingMandatoryInfoChunksAccepted);
    RUN_TEST(TestMalformedInfoStringsIgnored);
    RUN_TEST(TestDuplicateMandatoryChunksRejected);
    RUN_TEST(TestDuplicateTopLevelListsRejected);
    RUN_TEST(TestUnknownChunksRejected);
    RUN_TEST(TestChunkOrderingRejected);
    RUN_TEST(TestInvalidTerminalReferencesRejected);
    RUN_TEST(TestRomSampleSkippedWithoutAttachedRomBank);
    RUN_TEST(TestRomSampleUsesAttachedRomBank);
    RUN_TEST(TestRomMetadataWithoutRomSampleIgnored);
    RUN_TEST(TestRomSamplesRequireValidRomMetadata);
    RUN_TEST(TestIllegalOriginalPitchFallsBackTo60);
    RUN_TEST(TestTruncatedSmplChunkRejected);
    RUN_TEST(TestSampleGuardPaddingIsAccepted);
    RUN_TEST(TestSampleLoopGuardPointsAreAccepted);
    RUN_TEST(TestShortSampleIsAccepted);
    RUN_TEST(TestShortLoopIsAccepted);
    RUN_TEST(TestMissingSmplRejected);
    RUN_TEST(TestSf2ModulatorResolverSameZoneDuplicateRule);
    RUN_TEST(TestSf2ModulatorResolverTransformSeparatesIdentity);
    RUN_TEST(TestNonMonotonicPbagRejected);
#undef RUN_TEST
    std::printf("sf2_compliance: all tests passed\n");
    return 0;
}
