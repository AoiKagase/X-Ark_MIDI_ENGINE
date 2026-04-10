/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <cstdio>
#include <vector>
#include <cmath>
#include <cstring>
#include <limits>

#include "../src/sf2/Sf2File.h"
#include "../src/sf2/Sf2Types.h"

using namespace XArkMidi;

static const char* GenName(int g) {
    switch (g) {
    case 0: return "startAddrsOffset";
    case 1: return "endAddrsOffset";
    case 2: return "startloopAddrsOffset";
    case 3: return "endloopAddrsOffset";
    case 4: return "startAddrsCoarseOffset";
    case 5: return "modLfoToPitch";
    case 6: return "vibLfoToPitch";
    case 7: return "modEnvToPitch";
    case 8: return "initialFilterFc";
    case 9: return "initialFilterQ";
    case 10: return "modLfoToFilterFc";
    case 11: return "modEnvToFilterFc";
    case 12: return "modLfoToVolume";
    case 13: return "unused1";
    case 15: return "chorusEffectsSend";
    case 16: return "reverbEffectsSend";
    case 17: return "pan";
    case 21: return "delayModLFO";
    case 22: return "freqModLFO";
    case 23: return "delayVibLFO";
    case 24: return "freqVibLFO";
    case 25: return "delayModEnv";
    case 26: return "attackModEnv";
    case 27: return "holdModEnv";
    case 28: return "decayModEnv";
    case 29: return "sustainModEnv";
    case 30: return "releaseModEnv";
    case 31: return "keynumToModEnvHold";
    case 32: return "keynumToModEnvDecay";
    case 33: return "delayVolEnv";
    case 34: return "attackVolEnv";
    case 35: return "holdVolEnv";
    case 36: return "decayVolEnv";
    case 37: return "sustainVolEnv";
    case 38: return "releaseVolEnv";
    case 39: return "keynumToVolEnvHold";
    case 40: return "keynumToVolEnvDecay";
    case 41: return "instrument";
    case 43: return "keyRange";
    case 44: return "velRange";
    case 45: return "startloopAddrsCoarse";
    case 46: return "keynum";
    case 47: return "velocity";
    case 48: return "initialAttenuation";
    case 50: return "endloopAddrsCoarse";
    case 51: return "coarseTune";
    case 52: return "fineTune";
    case 53: return "sampleID";
    case 54: return "sampleModes";
    case 56: return "scaleTuning";
    case 57: return "exclusiveClass";
    case 58: return "overridingRootKey";
    default: return "unknown";
    }
}

void PrintNonDefaultGens(const i32 gens[GEN_COUNT], const i32 defaults[GEN_COUNT], const char* header) {
    printf("  %s:\n", header);
    bool hasContent = false;
    for (int g = 0; g < GEN_COUNT; ++g) {
        if (g == GEN_COUNT - 1) continue;
        if (gens[g] != defaults[g]) {
            printf("    [%02d] %-25s = %d\n", g, GenName(g), gens[g]);
            hasContent = true;
        }
    }
    if (!hasContent) {
        printf("    (all default values)\n");
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.sf2> [bank program key velocity]\n", argv[0]);
        return 1;
    }

    const std::wstring path(argv[1], argv[1] + std::strlen(argv[1]));
    
    Sf2File sf2;
    if (!sf2.LoadFromFile(path)) {
        fprintf(stderr, "SF2 parse failed: %s\n", sf2.ErrorMessage().c_str());
        return 1;
    }

    const i16* pcm = sf2.SampleData();
    const size_t sampleCount = sf2.SampleDataCount();
    
    printf("=== SF2 Analysis: %s ===\n", argv[1]);
    printf("Samples: %zu, Presets: %zu, Instruments: %zu\n\n", 
        sampleCount, sf2.PresetCount(), sf2.InstrumentCount());

    if (argc >= 5) {
        u16 bank = static_cast<u16>(atoi(argv[2]));
        u8 program = static_cast<u8>(atoi(argv[3]));
        u8 key = static_cast<u8>(atoi(argv[4]));
        u8 velocity = 100;
        if (argc >= 6) velocity = static_cast<u8>(atoi(argv[5]));
        
        printf("=== Query: bank=%u program=%u key=%u velocity=%u ===\n\n", bank, program, key, velocity);

        const i32* defaults = GetSF2GeneratorDefaults();

        int globalPresetBag, localPresetBag;
        if (!sf2.GetPresetBagIndices(bank, program, globalPresetBag, localPresetBag)) {
            printf("Preset not found!\n");
            return 0;
        }

        printf("[Preset] bank=%u program=%u\n", bank, program);
        printf("  Global Bag: %d, Local Bag: %d\n\n", globalPresetBag, localPresetBag);

        i32 presetGlobalGens[GEN_COUNT] = {0};
        i32 presetLocalGens[GEN_COUNT] = {0};
        if (globalPresetBag >= 0) {
            sf2.GetPresetGeneratorLayer(globalPresetBag, presetGlobalGens);
            PrintNonDefaultGens(presetGlobalGens, defaults, "Preset Global Zone (raw)");
        }
        sf2.GetPresetGeneratorLayer(localPresetBag, presetLocalGens);
        PrintNonDefaultGens(presetLocalGens, defaults, "Preset Local Zone (raw)");

        int instrumentIdx = presetLocalGens[GEN_Instrument];
        if (instrumentIdx >= 0) {
            printf("\n[Instrument] index=%d\n", instrumentIdx);

            std::vector<Sf2File::ZoneInfo> instZones;
            sf2.GetInstrumentLocalZones(instrumentIdx, instZones);
            printf("  Total Instrument Zones: %zu\n", instZones.size());

            for (size_t iz = 0; iz < instZones.size(); ++iz) {
                const auto& izi = instZones[iz];
                char kr[32], vr[32];
                if (izi.keyLo == 0 && izi.keyHi == 127) strcpy(kr, "key=any"); else sprintf(kr, "key=%d-%d", izi.keyLo, izi.keyHi);
                if (izi.velLo == 0 && izi.velHi == 127) strcpy(vr, "vel=any"); else sprintf(vr, "vel=%d-%d", izi.velLo, izi.velHi);
                printf("    Zone %zu: bag=%d %s %s sampleID=%d attenu=%d\n",
                    iz, izi.bagIndex, kr, vr, izi.sampleId, izi.generators[GEN_InitialAttenuation]);
            }
        }

        std::vector<ResolvedZone> zones;
        if (!sf2.FindZones(bank, program, key, velocity, zones, nullptr)) {
            printf("\nNO ZONES FOUND!\n");
            return 0;
        }
        
        printf("\n--- Merged Results (%zu zones) ---\n", zones.size());
        for (size_t zi = 0; zi < zones.size(); ++zi) {
            const auto& z = zones[zi];
            const auto* h = z.sample;
            const i32 loopStartOff = z.generators[GEN_StartloopAddrsOffset] + z.generators[GEN_StartloopAddrsCoarse] * 32768;
            const i32 loopEndOff = z.generators[GEN_EndloopAddrsOffset] + z.generators[GEN_EndloopAddrsCoarse] * 32768;
            const i32 effectiveLoopStart = static_cast<i32>(h->loopStart) + loopStartOff;
            const i32 effectiveLoopEnd = static_cast<i32>(h->loopEnd) + loopEndOff;
            
            printf("\nZone %zu:\n", zi);
            printf("  Sample: '%.20s' start=%u end=%u rate=%u\n",
                h->sampleName, h->start, h->end, h->sampleRate);
            printf("  Header Loop: start=%u end=%u len=%u\n",
                h->loopStart, h->loopEnd, h->loopEnd > h->loopStart ? h->loopEnd - h->loopStart : 0);
            printf("  Effective Loop: start=%d end=%d len=%d (offsets %+d / %+d)\n",
                effectiveLoopStart, effectiveLoopEnd,
                effectiveLoopEnd > effectiveLoopStart ? effectiveLoopEnd - effectiveLoopStart : 0,
                loopStartOff, loopEndOff);
            
            i32 peak = 0;
            double sumSq = 0.0;
            u32 frames = 0;
            bool allZero = true;
            for (u32 pos = h->start; pos < h->end && pos < sampleCount; ++pos) {
                i16 v = pcm[pos];
                if (v != 0) allZero = false;
                i32 av = abs(v);
                if (av > peak) peak = av;
                sumSq += static_cast<double>(v) * static_cast<double>(v);
                ++frames;
            }
            double rms = frames > 0 ? sqrt(sumSq / frames) : 0.0;
            printf("  Audio: frames=%u peak=%d rms=%.1f\n", frames, peak, rms);
            if (allZero) printf("  WARNING: ALL SAMPLES ARE ZERO!\n");
            printf("  InitialAttenuation: %d cb\n", z.generators[48]);
            double linearGain = pow(10.0, -z.generators[48] / 200.0);
            printf("  Linear Gain: %.4f\n", linearGain);
            printf("  Effective Peak: %d * %.4f = %.1f\n", peak, linearGain, peak * linearGain);
            printf("  All gens:\n");
            bool anyGen = false;
            for (int g = 0; g < GEN_COUNT; ++g) {
                if (g == GEN_COUNT - 1) continue;
                if (z.generators[g] != 0) {
                    printf("    [%02d] %-25s = %d\n", g, GenName(g), z.generators[g]);
                    anyGen = true;
                }
            }
            if (!anyGen) printf("    (all zero)\n");
        }
        
        return 0;
    }

    printf("Specify bank program key velocity to query.\n");
    printf("Example: %s file.sf2 0 80 60 100\n", argv[0]);
    return 0;
}

