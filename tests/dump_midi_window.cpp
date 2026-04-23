/*
 * Print MIDI events that fall within a time window.
 *
 * Usage:
 *   dump_midi_window.exe <input.mid> <start_sec> <end_sec>
 */

#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../src/midi/MidiFile.h"
#include "../src/midi/MidiSequencer.h"

using namespace XArkMidi;

static std::vector<u8> ReadFile(const char* path) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "rb");
#else
    f = std::fopen(path, "rb");
#endif
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<u8> buf(static_cast<size_t>(size));
    fread(buf.data(), 1, static_cast<size_t>(size), f);
    fclose(f);
    return buf;
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::fprintf(stderr, "Usage: %s <input.mid> <start_sec> <end_sec>\n", argv[0]);
        return 1;
    }

    const char* midiPath = argv[1];
    const double startSec = std::atof(argv[2]);
    const double endSec = std::atof(argv[3]);
    if (!(startSec >= 0.0) || !(endSec >= startSec)) {
        std::fprintf(stderr, "Invalid time range.\n");
        return 1;
    }

    auto midiData = ReadFile(midiPath);
    if (midiData.empty()) {
        std::fprintf(stderr, "Failed to read MIDI.\n");
        return 1;
    }

    MidiFile midi;
    if (!midi.LoadFromMemory(midiData.data(), midiData.size())) {
        std::fprintf(stderr, "MIDI parse failed: %s\n", midi.ErrorMessage().c_str());
        return 1;
    }

    MidiSequencer seq;
    constexpr u32 kSampleRate = 44100;
    if (!seq.Init(&midi, kSampleRate)) {
        std::fprintf(stderr, "Sequencer init failed: %s\n", seq.ErrorMessage().c_str());
        return 1;
    }

    const double startSample = startSec * static_cast<double>(kSampleRate);
    const double endSample = endSec * static_cast<double>(kSampleRate);

    for (int t = 0; t < midi.TrackCount(); ++t) {
        for (const auto& ev : midi.Track(t).Events()) {
            const double eventSample = seq.TickToSample(ev.absoluteTick);
            if (eventSample < startSample || eventSample >= endSample) {
                continue;
            }

            const double eventSec = eventSample / static_cast<double>(kSampleRate);
            switch (ev.type) {
            case MidiEventType::ProgramChange:
                std::printf("t=%.3f track=%d tick=%u ch=%u pc=%u\n",
                            eventSec, t, ev.absoluteTick, ev.channel + 1u, ev.data1);
                break;
            case MidiEventType::ControlChange:
                if (ev.data1 == 0 || ev.data1 == 1 || ev.data1 == 7 || ev.data1 == 10 ||
                    ev.data1 == 11 || ev.data1 == 32 || ev.data1 == 91 || ev.data1 == 93) {
                    std::printf("t=%.3f track=%d tick=%u ch=%u cc=%u val=%u\n",
                                eventSec, t, ev.absoluteTick, ev.channel + 1u, ev.data1, ev.data2);
                }
                break;
            case MidiEventType::NoteOn:
                if (ev.data2 > 0) {
                    std::printf("t=%.3f track=%d tick=%u ch=%u noteon key=%u vel=%u\n",
                                eventSec, t, ev.absoluteTick, ev.channel + 1u, ev.data1, ev.data2);
                }
                break;
            case MidiEventType::NoteOff:
                std::printf("t=%.3f track=%d tick=%u ch=%u noteoff key=%u\n",
                            eventSec, t, ev.absoluteTick, ev.channel + 1u, ev.data1);
                break;
            default:
                break;
            }
        }
    }

    return 0;
}
