#include "MidiFile.h"
#include "../midi2/Midi2File.h"
#include <stdexcept>
#include <cstring>

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
    return true;
}

bool MidiFile::LoadFromMemory(const u8* data, size_t size) {
    tracks_.clear();
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
    return true;
}

bool MidiFile::LoadFromFile(const std::wstring& path) {
    std::vector<u8> data;
    if (!ReadFileBytes(path, data, errorMsg_))
        return false;
    return LoadFromMemory(data.data(), data.size());
}

} // namespace XArkMidi

