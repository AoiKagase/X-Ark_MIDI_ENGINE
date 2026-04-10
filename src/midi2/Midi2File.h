/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "../midi/MidiFile.h"
#include "../common/BinaryReader.h"
#include <string>

namespace XArkMidi {

// MIDI 2.0 Clip File (RIFF/MIDI 形式) のパーサー。
//
// ファイル構造 (MC-HB-25 MIDI Clip File 仕様準拠):
//   RIFF <size> 'MIDI'
//     'MIDh' <size>          -- ヘッダーチャンク
//       ppq : u16 (LE)       -- Pulses Per Quarter Note (デフォルト 96)
//     'MIDt' <size>          -- トラックチャンク (1つ以上)
//       [delta_ticks: VLQ] [ump_word(BE) × N]  -- イベント列
//
// パース結果は MidiFile と同じ MidiHeader / MidiTrack 形式で提供する。
// UMP メッセージは UmpDownConverter により MIDI 1.0 互換イベントに変換される。
class Midi2File {
public:
    // data: ファイル全体のバイト列, size: バイト数
    bool Parse(const u8* data, size_t size);

    const MidiHeader&          Header()     const { return header_; }
    int                        TrackCount() const { return static_cast<int>(tracks_.size()); }
    const MidiTrack&           Track(int i) const { return tracks_[i]; }
    const std::string&         ErrorMessage() const { return errorMsg_; }

private:
    MidiHeader              header_{};
    std::vector<MidiTrack>  tracks_;
    std::string             errorMsg_;

    // 'MIDh' チャンクをパースして ppq を設定する
    bool ParseMIDh(BinaryReader& r, u32 chunkSize);

    // 'MIDt' チャンクをパースして MidiTrack を生成する
    bool ParseMIDt(BinaryReader& r, u32 chunkSize);
};

} // namespace XArkMidi
