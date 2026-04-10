/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "UmpTypes.h"
#include "../midi/MidiEvent.h"
#include <vector>

namespace XArkMidi {

// UMP (Universal MIDI Packet) を MIDI 1.0 互換の MidiEvent 列にダウンコンバートする。
//
// 変換規則:
//   MT=0x2 (MIDI 1.0 チャンネルメッセージ): ほぼそのまま変換
//   MT=0x4 (MIDI 2.0 チャンネルメッセージ): 高精度値を 7-bit/14-bit にスケールダウン
//     - Velocity    : 16-bit (0-65535) → 7-bit  (>> 9)
//     - CC 値       : 32-bit (0-4294967295) → 7-bit  (>> 25)
//     - Pitch Bend  : 32-bit (center=0x80000000) → 14-bit (>> 18)
//     - 圧力値      : 32-bit → 7-bit  (>> 25)
//     - RPN         : 32-bit → CC101/100/6/38 シーケンスに展開
//     - NRPN        : 32-bit → CC99/98/6/38  シーケンスに展開
//     - Program Change with bank: CC0/CC32 + PC に展開
//   MT=0xD (Flex Data): テンポ・拍子記号をメタイベントとして出力
//   それ以外 (System, SysEx 等): スキップ
class UmpDownConverter {
public:
    // UMP ワード列を変換し out に追加する。
    // words: UMP 32-bit ワード配列 (ビッグエンディアン解釈済み)
    // wordCount: ワード数
    // absoluteTick: このパケットの絶対 tick
    void Convert(const u32* words, int wordCount,
                 u32 absoluteTick, std::vector<MidiEvent>& out);

private:
    void ConvertMidi1Channel(u32 word, u32 tick, std::vector<MidiEvent>& out);
    void ConvertMidi2Channel(u32 word0, u32 word1, u32 tick, std::vector<MidiEvent>& out);
    void ConvertFlexData(const u32* words, u32 tick, std::vector<MidiEvent>& out);
};

} // namespace XArkMidi
