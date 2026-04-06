#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../ArkMidiEngine/src/common/Types.h"
#include "../ArkMidiEngine/src/midi/MidiFile.h"

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
        std::fprintf(stderr, "Usage: %s <input.mid> <channel>\n", argv[0]);
        return 1;
    }

    auto midiData = ReadFile(argv[1]);
    if (midiData.empty()) {
        std::fprintf(stderr, "Failed to read MIDI.\n");
        return 1;
    }

    const int targetChannel = std::atoi(argv[2]);
    MidiFile midi;
    if (!midi.LoadFromMemory(midiData.data(), midiData.size())) {
        std::fprintf(stderr, "MIDI parse failed: %s\n", midi.ErrorMessage().c_str());
        return 1;
    }

    for (int ti = 0; ti < midi.TrackCount(); ++ti) {
        for (const auto& ev : midi.Track(ti).Events()) {
            if (ev.channel != targetChannel) continue;

            switch (ev.type) {
            case MidiEventType::ProgramChange:
                std::printf("track=%d tick=%u ch=%u program=%u\n",
                    ti, ev.absoluteTick, ev.channel, ev.data1);
                break;
            case MidiEventType::ControlChange:
                if (ev.data1 == 0 || ev.data1 == 7 || ev.data1 == 10 || ev.data1 == 11 || ev.data1 == 32) {
                    std::printf("track=%d tick=%u ch=%u cc=%u val=%u\n",
                        ti, ev.absoluteTick, ev.channel, ev.data1, ev.data2);
                }
                break;
            case MidiEventType::NoteOn:
                if (ev.data2 > 0) {
                    std::printf("track=%d tick=%u ch=%u noteon key=%u vel=%u\n",
                        ti, ev.absoluteTick, ev.channel, ev.data1, ev.data2);
                }
                break;
            case MidiEventType::NoteOff:
                std::printf("track=%d tick=%u ch=%u noteoff key=%u\n",
                    ti, ev.absoluteTick, ev.channel, ev.data1);
                break;
            default:
                break;
            }
        }
    }

    return 0;
}
