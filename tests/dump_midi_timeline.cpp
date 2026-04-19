/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <algorithm>
#include <cstdio>
#include <vector>

#include "../src/midi/MidiFile.h"

using namespace XArkMidi;

struct TimelineEvent {
    int track = 0;
    const MidiEvent* event = nullptr;
};

static const char* EventName(MidiEventType type) {
    switch (type) {
    case MidiEventType::NoteOn: return "noteon";
    case MidiEventType::NoteOff: return "noteoff";
    case MidiEventType::ControlChange: return "cc";
    case MidiEventType::ProgramChange: return "program";
    case MidiEventType::PitchBend: return "pitchbend";
    case MidiEventType::MetaTempo: return "tempo";
    default: return "other";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <input.mid>\n", argv[0]);
        return 1;
    }

    const std::wstring path(argv[1], argv[1] + std::strlen(argv[1]));

    MidiFile midi;
    if (!midi.LoadFromFile(path)) {
        std::fprintf(stderr, "MIDI parse failed: %s\n", midi.ErrorMessage().c_str());
        return 1;
    }

    std::printf("format=%u tracks=%u division=%u\n",
        midi.Header().format, midi.Header().numTracks, midi.Header().division);

    std::vector<TimelineEvent> timeline;
    for (int ti = 0; ti < midi.TrackCount(); ++ti) {
        const auto& track = midi.Track(ti);
        for (const auto& ev : track.Events()) {
            switch (ev.type) {
            case MidiEventType::ProgramChange:
            case MidiEventType::ControlChange:
            case MidiEventType::PitchBend:
            case MidiEventType::NoteOn:
            case MidiEventType::NoteOff:
            case MidiEventType::MetaTempo:
                timeline.push_back({ti, &ev});
                break;
            default:
                break;
            }
        }
    }

    std::sort(timeline.begin(), timeline.end(),
        [](const TimelineEvent& a, const TimelineEvent& b) {
            if (a.event->absoluteTick != b.event->absoluteTick) {
                return a.event->absoluteTick < b.event->absoluteTick;
            }
            return a.track < b.track;
        });

    for (const auto& entry : timeline) {
        const auto& ev = *entry.event;
        switch (ev.type) {
        case MidiEventType::MetaTempo:
            std::printf("tick=%-6u track=%d tempo=%u bpm=%.3f\n",
                ev.absoluteTick, entry.track, ev.tempoUs, 60000000.0 / static_cast<double>(ev.tempoUs));
            break;
        case MidiEventType::ProgramChange:
            std::printf("tick=%-6u track=%d ch=%u %-9s %u\n",
                ev.absoluteTick, entry.track, ev.channel, EventName(ev.type), ev.data1);
            break;
        case MidiEventType::ControlChange:
            if (ev.data1 == 0 || ev.data1 == 32 || ev.data1 == 6 || ev.data1 == 7 || ev.data1 == 10 ||
                ev.data1 == 11 || ev.data1 == 38 || ev.data1 == 100 || ev.data1 == 101) {
                std::printf("tick=%-6u track=%d ch=%u %-9s %u val=%u\n",
                    ev.absoluteTick, entry.track, ev.channel, EventName(ev.type), ev.data1, ev.data2);
            }
            break;
        case MidiEventType::PitchBend:
            std::printf("tick=%-6u track=%d ch=%u %-9s %d\n",
                ev.absoluteTick, entry.track, ev.channel, EventName(ev.type), ev.PitchBendValue());
            break;
        case MidiEventType::NoteOn:
            if (ev.data2 > 0) {
                std::printf("tick=%-6u track=%d ch=%u %-9s key=%u vel=%u\n",
                    ev.absoluteTick, entry.track, ev.channel, EventName(ev.type), ev.data1, ev.data2);
            }
            break;
        case MidiEventType::NoteOff:
            std::printf("tick=%-6u track=%d ch=%u %-9s key=%u\n",
                ev.absoluteTick, entry.track, ev.channel, EventName(ev.type), ev.data1);
            break;
        default:
            break;
        }
    }

    return 0;
}
