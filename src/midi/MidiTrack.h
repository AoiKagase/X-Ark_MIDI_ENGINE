#pragma once
#include "MidiEvent.h"
#include "../common/BinaryReader.h"
#include <vector>
#include <string>

namespace XArkMidi {

class MidiTrack {
public:
    // チャンクデータをパースしてイベントリストを構築する
    // r はチャンクデータ（MThd 先頭 8 バイト除く）のスライス
    bool Parse(BinaryReader& r);

    const std::vector<MidiEvent>& Events() const { return events_; }

    // 外部から直接イベントを追加する (MIDI 2.0 ダウンコンバート用)
    void AddEvent(const MidiEvent& ev) { events_.push_back(ev); }

    // パースエラーメッセージ
    const std::string& ErrorMessage() const { return errorMsg_; }

private:
    std::vector<MidiEvent> events_;
    std::string            errorMsg_;

    // ランニングステータス保持（直前のステータスバイト）
    u8 runningStatus_ = 0;

    bool ParseEvent(BinaryReader& r, u32 currentTick);
};

} // namespace XArkMidi

