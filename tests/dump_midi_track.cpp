#include <cstdio>
#include <vector>
#include "../src/midi/MidiFile.h"

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
    std::vector<u8> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);
    return buf;
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input.mid> <track>\n", argv[0]);
        return 1;
    }

    auto midiData = ReadFile(argv[1]);
    if (midiData.empty()) {
        std::fprintf(stderr, "Failed to read MIDI.\n");
        return 1;
    }

    MidiFile midi;
    if (!midi.LoadFromMemory(midiData.data(), midiData.size())) {
        std::fprintf(stderr, "MIDI parse failed: %s\n", midi.ErrorMessage().c_str());
        return 1;
    }

    const int targetTrack = std::atoi(argv[2]);
    if (targetTrack < 0 || targetTrack >= midi.TrackCount()) {
        std::fprintf(stderr, "Track out of range.\n");
        return 1;
    }

    for (const auto& ev : midi.Track(targetTrack).Events()) {
        switch (ev.type) {
        case MidiEventType::ProgramChange:
            std::printf("track=%d tick=%u ch=%u program=%u\n",
                targetTrack, ev.absoluteTick, ev.channel, ev.data1);
            break;
        case MidiEventType::ControlChange:
            if (ev.data1 == 0 || ev.data1 == 32 || ev.data1 == 7 || ev.data1 == 10 || ev.data1 == 11) {
                std::printf("track=%d tick=%u ch=%u cc=%u val=%u\n",
                    targetTrack, ev.absoluteTick, ev.channel, ev.data1, ev.data2);
            }
            break;
        case MidiEventType::NoteOn:
            if (ev.data2 > 0) {
                std::printf("track=%d tick=%u ch=%u noteon key=%u vel=%u\n",
                    targetTrack, ev.absoluteTick, ev.channel, ev.data1, ev.data2);
            }
            break;
        case MidiEventType::NoteOff:
            std::printf("track=%d tick=%u ch=%u noteoff key=%u\n",
                targetTrack, ev.absoluteTick, ev.channel, ev.data1);
            break;
        default:
            break;
        }
    }
    return 0;
}

