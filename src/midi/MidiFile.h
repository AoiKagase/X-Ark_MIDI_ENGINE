/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "MidiTrack.h"
#include "../common/FileUtil.h"
#include <vector>
#include <string>

namespace XArkMidi {

struct MidiHeader {
    u16 format;    // 0=単一トラック, 1=マルチトラック同期, 2=マルチトラック非同期
    u16 numTracks;
    u16 division;  // PPQ (ticks per quarter note) ※SMPTE非対応
};

class MidiFile {
public:
    bool LoadFromMemory(const u8* data, size_t size);
    bool LoadFromFile(const std::wstring& path);

    const MidiHeader&   Header()     const { return header_; }
    int                 TrackCount() const { return static_cast<int>(tracks_.size()); }
    const MidiTrack&    Track(int i) const { return tracks_[i]; }

    const std::string& ErrorMessage() const { return errorMsg_; }

private:
    MidiHeader              header_{};
    std::vector<MidiTrack>  tracks_;
    std::string             errorMsg_;

    // MIDI 2.0 Clip File (RIFF/MIDI) のロード。
    // LoadFromMemory() が自動的に呼び出す。
    bool LoadMidi2FromMemory(const u8* data, size_t size);
};

} // namespace XArkMidi

