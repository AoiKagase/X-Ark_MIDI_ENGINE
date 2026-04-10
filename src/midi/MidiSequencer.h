/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once
#include "MidiFile.h"
#include <vector>

namespace XArkMidi {

// テンポマップエントリ
struct TempoEntry {
    u32    tick;          // このテンポが始まる絶対tick
    u32    tempoUs;       // μsec/beat
    double sampleOffset;  // このtickが対応するサンプルオフセット（累積）
};

// MIDIシーケンサー
// Format 0/1 のイベントを絶対tick順にマージし、
// テンポ変化を考慮して tick→サンプル変換を提供する
class MidiSequencer {
public:
    bool Init(const MidiFile* file, u32 sampleRate);

    // 次のイベントまで何サンプルあるか（0なら即時処理すべきイベントが存在）
    u32 SamplesToNextEvent() const;

    // 現在位置のイベントを取得して カーソルを進める
    // IsFinished() が true の場合は nullptr を返す
    const MidiEvent* ConsumeEvent();

    bool IsFinished() const;

    // tick -> サンプルオフセット変換（テンポマップ参照）
    double TickToSample(u32 tick) const;

    // 現在のサンプル位置
    double CurrentSample() const { return currentSample_; }

    // サンプル位置を進める（Render ループから呼ぶ）
    void AdvanceSamples(u32 count) { currentSample_ += count; }

    const std::string& ErrorMessage() const { return errorMsg_; }

private:
    u32    sampleRate_ = 44100;
    u16    division_   = 480;

    std::vector<TempoEntry> tempoMap_;
    std::vector<MidiEvent>  mergedEvents_;
    size_t                  eventCursor_ = 0;
    double                  currentSample_ = 0.0;
    double                  nextEventSample_ = 0.0;

    std::string errorMsg_;

    void BuildTempoMap(const MidiFile* file);
    void MergeTracks(const MidiFile* file);
};

} // namespace XArkMidi

