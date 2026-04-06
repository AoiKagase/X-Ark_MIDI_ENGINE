#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
#include <map>
#include <tuple>
#include <array>
#include <algorithm>

#include "../ArkMidiEngine/src/common/Types.h"
#include "../ArkMidiEngine/src/midi/MidiFile.h"
#include "../ArkMidiEngine/src/sf2/Sf2File.h"

using namespace ArkMidi;

static std::vector<u8> ReadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<u8> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);
    return buf;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input.mid> <input.sf2>\n", argv[0]);
        return 1;
    }

    auto midiData = ReadFile(argv[1]);
    auto sf2Data = ReadFile(argv[2]);
    if (midiData.empty() || sf2Data.empty()) {
        std::fprintf(stderr, "Failed to read input files.\n");
        return 1;
    }

    MidiFile midi;
    if (!midi.LoadFromMemory(midiData.data(), midiData.size())) {
        std::fprintf(stderr, "MIDI parse failed: %s\n", midi.ErrorMessage().c_str());
        return 1;
    }

    Sf2File sf2;
    if (!sf2.LoadFromMemory(sf2Data.data(), sf2Data.size())) {
        std::fprintf(stderr, "SF2 parse failed: %s\n", sf2.ErrorMessage().c_str());
        return 1;
    }

    std::map<std::tuple<int, int, int>, u32> noteOnTicks;
    std::vector<std::pair<u32, int>> noteEdges;
    std::array<u8, MIDI_CHANNEL_COUNT> programs{};
    std::array<u8, MIDI_CHANNEL_COUNT> bankMsb{};
    std::array<u8, MIDI_CHANNEL_COUNT> bankLsb{};
    auto dumpZones = [&](int ti, const MidiEvent& ev) {
        std::vector<ResolvedZone> zones;
        u16 bank = (ev.channel == MIDI_DRUM_CHANNEL)
            ? static_cast<u16>(MIDI_DRUM_BANK)
            : static_cast<u16>(bankMsb[ev.channel]) * 128 + bankLsb[ev.channel];
        u8 program = programs[ev.channel];
        if (!sf2.FindZones(bank, program, ev.data1, ev.data2, zones, nullptr)) {
            std::printf("noteon track=%d tick=%u ch=%u bank=%u program=%u key=%u vel=%u zones=0\n",
                ti, ev.absoluteTick, ev.channel, bank, program, ev.data1, ev.data2);
            return;
        }

        std::printf("noteon track=%d tick=%u ch=%u bank=%u program=%u key=%u vel=%u zones=%zu\n",
            ti, ev.absoluteTick, ev.channel, bank, program, ev.data1, ev.data2, zones.size());
        for (size_t i = 0; i < zones.size() && i < 8; ++i) {
            const auto& z = zones[i];
            const auto* s = z.sample;
            std::printf(
                "  zone=%zu sampleRate=%u start=%u end=%u loopStart=%u loopEnd=%u sampleModes=%d pan=%d keynum=%d velocity=%d root=%d startOff=%d endOff=%d loopStartOff=%d loopEndOff=%d atk=%d dec=%d sus=%d rel=%d\n",
                i,
                s->dwSampleRate,
                s->dwStart,
                s->dwEnd,
                s->dwStartloop,
                s->dwEndloop,
                z.generators[GEN_SampleModes],
                z.generators[GEN_Pan],
                z.generators[GEN_Keynum],
                z.generators[GEN_Velocity],
                z.generators[GEN_OverridingRootKey],
                z.generators[GEN_StartAddrsOffset] + z.generators[GEN_StartAddrsCoarseOffset] * 32768,
                z.generators[GEN_EndAddrsOffset] + z.generators[GEN_EndAddrsCoarseOffset] * 32768,
                z.generators[GEN_StartloopAddrsOffset] + z.generators[GEN_StartloopAddrsCoarse] * 32768,
                z.generators[GEN_EndloopAddrsOffset] + z.generators[GEN_EndloopAddrsCoarse] * 32768,
                z.generators[GEN_AttackVolEnv],
                z.generators[GEN_DecayVolEnv],
                z.generators[GEN_SustainVolEnv],
                z.generators[GEN_ReleaseVolEnv]);
        }
    };

    for (int ti = 0; ti < midi.TrackCount(); ++ti) {
        for (const auto& ev : midi.Track(ti).Events()) {
            if (ev.type == MidiEventType::ProgramChange) {
                programs[ev.channel] = ev.data1;
                std::printf("track=%d tick=%u ch=%u program=%u\n", ti, ev.absoluteTick, ev.channel, ev.data1);
            }
            if (ev.type == MidiEventType::ControlChange && (
                ev.data1 == 0 || ev.data1 == 32 || ev.data1 == 7 || ev.data1 == 10 || ev.data1 == 11)) {
                if (ev.data1 == 0) bankMsb[ev.channel] = ev.data2;
                if (ev.data1 == 32) bankLsb[ev.channel] = ev.data2;
                if (ev.channel >= 6 && ev.channel <= 8) {
                    std::printf("track=%d tick=%u ch=%u cc=%u val=%u\n", ti, ev.absoluteTick, ev.channel, ev.data1, ev.data2);
                }
            }

            auto key = std::make_tuple(ti, ev.channel, ev.data1);
            if (ev.type == MidiEventType::NoteOn && ev.data2 > 0) {
                noteOnTicks[key] = ev.absoluteTick;
                noteEdges.push_back({ ev.absoluteTick, +1 });
                if (programs[ev.channel] == 118 || (ti == 0 && ev.absoluteTick >= 30720 && ev.absoluteTick < 40000)) {
                    dumpZones(ti, ev);
                }
            } else if (ev.type == MidiEventType::NoteOff) {
                auto it = noteOnTicks.find(key);
                if (it != noteOnTicks.end()) {
                    noteEdges.push_back({ ev.absoluteTick, -1 });
                    u32 dur = ev.absoluteTick - it->second;
                    if (dur > 480) {
                        std::printf("long-note track=%d ch=%u key=%u start=%u end=%u duration=%u ticks\n",
                            ti, ev.channel, ev.data1, it->second, ev.absoluteTick, dur);
                    }
                    noteOnTicks.erase(it);
                }
            }
        }
    }

    std::sort(noteEdges.begin(), noteEdges.end(),
        [](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            return a.second < b.second;
        });

    int activeNotes = 0;
    int peakNotes = 0;
    u32 peakTick = 0;
    for (const auto& edge : noteEdges) {
        activeNotes += edge.second;
        if (activeNotes > peakNotes) {
            peakNotes = activeNotes;
            peakTick = edge.first;
        }
    }
    std::printf("peak-midi-polyphony=%d at tick=%u\n", peakNotes, peakTick);

    return 0;
}
