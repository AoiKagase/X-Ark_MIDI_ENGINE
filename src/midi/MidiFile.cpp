/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "MidiFile.h"
#include "../midi2/Midi2File.h"
#include <stdexcept>
#include <cstring>
#include <algorithm>
#include <cctype>
#include <limits>

namespace XArkMidi {

// RIFF/MIDI ファイルかどうかを先頭バイトで判定する
static bool IsMidi2Format(const u8* data, size_t size) {
    // 'RIFF' (4) + size (4) + 'MIDI' (4) = 最低 12 バイト
    if (size < 12) return false;
    return data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F'
        && data[8] == 'M' && data[9] == 'I' && data[10] == 'D' && data[11] == 'I';
}

bool MidiFile::LoadMidi2FromMemory(const u8* data, size_t size) {
    Midi2File midi2;
    if (!midi2.Parse(data, size)) {
        errorMsg_ = midi2.ErrorMessage();
        return false;
    }
    header_ = midi2.Header();
    tracks_.clear();
    for (int i = 0; i < midi2.TrackCount(); ++i)
        tracks_.push_back(midi2.Track(i));
    DetectLoopMarkers();
    return true;
}

bool MidiFile::LoadFromMemory(const u8* data, size_t size) {
    tracks_.clear();
    loopMarkers_ = {};
    errorMsg_.clear();

    // MIDI 2.0 Clip File (RIFF/MIDI) を自動検出
    if (IsMidi2Format(data, size))
        return LoadMidi2FromMemory(data, size);

    try {
        BinaryReader r(data, size);

        // MThd ヘッダーチャンク
        u32 chunkId   = r.ReadU32BE();
        u32 chunkSize = r.ReadU32BE();

        // 'MThd' = 0x4D546864
        if (chunkId != 0x4D546864u) {
            errorMsg_ = "Not a MIDI file: missing MThd header";
            return false;
        }
        if (chunkSize < 6) {
            errorMsg_ = "Invalid MThd chunk size";
            return false;
        }

        auto headerSlice = r.ReadSlice(chunkSize);
        header_.format    = headerSlice.ReadU16BE();
        header_.numTracks = headerSlice.ReadU16BE();
        header_.division  = headerSlice.ReadU16BE();

        if (header_.format > 2) {
            errorMsg_ = "Unsupported MIDI format";
            return false;
        }
        if (header_.division & 0x8000) {
            errorMsg_ = "SMPTE time code is not supported";
            return false;
        }

        // 各 MTrk チャンクを読み込む
        while (!r.IsEof()) {
            if (r.Remaining() < 8) break; // 不完全なチャンクは無視

            u32 trkId   = r.ReadU32BE();
            u32 trkSize = r.ReadU32BE();

            // 'MTrk' = 0x4D54726B
            if (trkId != 0x4D54726Bu) {
                // 未知チャンクはスキップ
                r.Skip(trkSize);
                continue;
            }

            auto trkSlice = r.ReadSlice(trkSize);
            MidiTrack track;
            if (!track.Parse(trkSlice)) {
                errorMsg_ = "Track parse error: " + track.ErrorMessage();
                return false;
            }
            tracks_.push_back(std::move(track));
        }
    }
    catch (const std::exception& e) {
        errorMsg_ = e.what();
        return false;
    }
    DetectLoopMarkers();
    return true;
}

bool MidiFile::LoadFromFile(const std::wstring& path) {
    std::vector<u8> data;
    if (!ReadFileBytes(path, data, errorMsg_))
        return false;
    return LoadFromMemory(data.data(), data.size());
}

namespace {
std::string LowerAscii(const std::vector<u8>& payload) {
    std::string text;
    text.reserve(payload.size());
    for (u8 ch : payload) {
        text.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return text;
}

bool IsLoopStartText(const std::string& text) {
    return text.find("loopstart") != std::string::npos ||
           text.find("loop start") != std::string::npos ||
           text.find("loop_start") != std::string::npos ||
           text.find("loop begin") != std::string::npos;
}

bool IsLoopEndText(const std::string& text) {
    return text.find("loopend") != std::string::npos ||
           text.find("loop end") != std::string::npos ||
           text.find("loop_end") != std::string::npos;
}
}

void MidiFile::DetectLoopMarkers() {
    loopMarkers_ = {};

    u32 songEndTick = 0;
    u32 textStartTick = 0;
    u32 textEndTick = 0;
    bool hasTextStart = false;
    bool hasTextEnd = false;
    u32 cc111Tick = std::numeric_limits<u32>::max();
    u32 cc116Tick = std::numeric_limits<u32>::max();
    u32 cc117Tick = std::numeric_limits<u32>::max();

    for (const auto& track : tracks_) {
        for (const auto& ev : track.Events()) {
            songEndTick = std::max(songEndTick, ev.absoluteTick);

            if (ev.type == MidiEventType::MetaOther &&
                (ev.metaType == 0x01 || ev.metaType == 0x05 || ev.metaType == 0x06 || ev.metaType == 0x07)) {
                const std::string text = LowerAscii(ev.payload);
                if (!hasTextStart && IsLoopStartText(text)) {
                    hasTextStart = true;
                    textStartTick = ev.absoluteTick;
                }
                if (!hasTextEnd && IsLoopEndText(text)) {
                    hasTextEnd = true;
                    textEndTick = ev.absoluteTick;
                }
            } else if (ev.type == MidiEventType::ControlChange) {
                if (ev.data1 == 111 && ev.absoluteTick < cc111Tick) {
                    cc111Tick = ev.absoluteTick;
                } else if (ev.data1 == 116 && ev.absoluteTick < cc116Tick) {
                    cc116Tick = ev.absoluteTick;
                } else if (ev.data1 == 117 && ev.absoluteTick < cc117Tick) {
                    cc117Tick = ev.absoluteTick;
                }
            }
        }
    }

    if (hasTextStart && hasTextEnd && textStartTick < textEndTick) {
        loopMarkers_ = { true, textStartTick, textEndTick, MidiLoopMarkerSource::TextMarker };
        return;
    }
    if (cc111Tick != std::numeric_limits<u32>::max() && cc111Tick < songEndTick) {
        loopMarkers_ = { true, cc111Tick, songEndTick, MidiLoopMarkerSource::Cc111 };
        return;
    }
    if (cc116Tick != std::numeric_limits<u32>::max() &&
        cc117Tick != std::numeric_limits<u32>::max() &&
        cc116Tick < cc117Tick) {
        loopMarkers_ = { true, cc116Tick, cc117Tick, MidiLoopMarkerSource::Cc116117 };
        return;
    }
}

} // namespace XArkMidi
