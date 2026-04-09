#include "Midi2File.h"
#include "UmpDownConverter.h"
#include <stdexcept>

namespace XArkMidi {

bool Midi2File::Parse(const u8* data, size_t size) {
    tracks_.clear();
    errorMsg_.clear();

    // デフォルト値
    header_ = {};
    header_.format    = 0;
    header_.numTracks = 0;
    header_.division  = 96; // デフォルト PPQ

    try {
        BinaryReader r(data, size);

        // RIFF ヘッダー: 'RIFF' <size(LE)> 'MIDI'
        const u32 riffId   = r.ReadU32BE(); // 'RIFF' = 0x52494646
        const u32 riffSize = r.ReadU32LE(); // RIFF チャンクサイズ (LE)
        const u32 formType = r.ReadU32BE(); // 'MIDI' = 0x4D494449

        (void)riffSize; // サイズ検証は省略（末尾まで読むだけ）

        if (riffId != 0x52494646u) {
            errorMsg_ = "Not a RIFF file";
            return false;
        }
        if (formType != 0x4D494449u) {
            errorMsg_ = "RIFF form type is not 'MIDI'";
            return false;
        }

        // チャンクを順に読む
        while (!r.IsEof()) {
            if (r.Remaining() < 8) break; // 不完全なチャンクは無視

            const u32 chunkId   = r.ReadU32BE(); // チャンク ID (4 文字)
            const u32 chunkSize = r.ReadU32LE(); // チャンクサイズ (LE)

            if (r.Remaining() < chunkSize) {
                errorMsg_ = "Truncated chunk data";
                return false;
            }

            if (chunkId == 0x4D494468u) {         // 'MIDh'
                auto chunk = r.ReadSlice(chunkSize);
                if (!ParseMIDh(chunk, chunkSize))
                    return false;
            }
            else if (chunkId == 0x4D494474u) {    // 'MIDt'
                auto chunk = r.ReadSlice(chunkSize);
                if (!ParseMIDt(chunk, chunkSize))
                    return false;
            }
            else {
                // 未知チャンクはスキップ
                r.Skip(chunkSize);
                // RIFF の 2-byte アライメント
                if (chunkSize & 1) r.Skip(1);
            }
        }

        header_.numTracks = static_cast<u16>(tracks_.size());
    }
    catch (const std::exception& e) {
        errorMsg_ = e.what();
        return false;
    }

    if (tracks_.empty()) {
        errorMsg_ = "No 'MIDt' track chunk found";
        return false;
    }

    return true;
}

// 'MIDh': ヘッダーチャンク
// 最低 2 バイト: ppq (LE u16)
bool Midi2File::ParseMIDh(BinaryReader& r, u32 chunkSize) {
    if (chunkSize < 2) {
        errorMsg_ = "MIDh chunk too small";
        return false;
    }
    header_.division = r.ReadU16LE(); // PPQ
    return true;
}

// 'MIDt': トラックチャンク
// イベント列: [delta_ticks: VLQ] [UMP ワード × N (BE)] ...
bool Midi2File::ParseMIDt(BinaryReader& r, u32 /*chunkSize*/) {
    MidiTrack track;
    UmpDownConverter converter;
    u32 currentTick = 0;

    try {
        while (!r.IsEof()) {
            // デルタタイム (VLQ、SMF1 と同じエンコーディング)
            const u32 delta = r.ReadVLQ();
            currentTick += delta;

            if (r.IsEof()) break; // デルタのみで終端（EndOfTrack 相当）

            // UMP 先頭ワードを読んでメッセージタイプを判定
            if (r.Remaining() < 4) break;
            const u32 word0 = r.ReadU32BE();
            const auto mt   = static_cast<UmpMessageType>((word0 >> 28) & 0xF);
            const int  wc   = UmpWordCount(mt);

            // 残りのワードを読む (word0 は取得済みなので wc-1 ワード)
            u32 words[4] = { word0, 0, 0, 0 };
            for (int i = 1; i < wc; ++i) {
                if (r.Remaining() < 4) break;
                words[i] = r.ReadU32BE();
            }

            // ダウンコンバート
            std::vector<MidiEvent> converted;
            converter.Convert(words, wc, currentTick, converted);
            for (auto& ev : converted)
                track.AddEvent(ev);
        }
    }
    catch (const std::exception& e) {
        errorMsg_ = std::string("MIDt parse error: ") + e.what();
        return false;
    }

    // EndOfTrack メタイベントを末尾に挿入
    MidiEvent eot{};
    eot.absoluteTick = currentTick;
    eot.type         = MidiEventType::MetaEndOfTrack;
    track.AddEvent(eot);

    tracks_.push_back(std::move(track));
    return true;
}

} // namespace XArkMidi
