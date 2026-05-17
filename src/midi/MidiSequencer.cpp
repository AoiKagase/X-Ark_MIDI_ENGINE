/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "MidiSequencer.h"
#include <algorithm>
#include <queue>
#include <cmath>
#include <stdexcept>

namespace XArkMidi {

bool MidiSequencer::Init(const MidiFile* file, u32 sampleRate) {
    mergedEvents_.clear();
    tempoMap_.clear();
    eventCursor_   = 0;
    currentSample_ = 0.0;
    nextEventSample_ = 0.0;
    sampleRate_    = sampleRate;
    division_      = file->Header().division;
    errorMsg_.clear();

    if (division_ == 0) {
        errorMsg_ = "Invalid MIDI division (0)";
        return false;
    }

    // テンポマップを先行構築（トラック0のテンポイベントを使用）
    BuildTempoMap(file);

    // 全トラックをマージ
    MergeTracks(file);

    if (!mergedEvents_.empty()) {
        nextEventSample_ = TickToSample(mergedEvents_[0].absoluteTick);
    }

    return true;
}

void MidiSequencer::Reset() {
    eventCursor_ = 0;
    currentSample_ = 0.0;
    nextEventSample_ = mergedEvents_.empty() ? 0.0 : TickToSample(mergedEvents_[0].absoluteTick);
}

void MidiSequencer::ResetToLoopStart() {
    if (!loopRangeEnabled_) {
        Reset();
        return;
    }
    currentSample_ = loopStartSample_;
    eventCursor_ = 0;
    while (eventCursor_ < mergedEvents_.size() && mergedEvents_[eventCursor_].absoluteTick < loopStartTick_) {
        ++eventCursor_;
    }
    nextEventSample_ = IsFinished() ? currentSample_ : TickToSample(mergedEvents_[eventCursor_].absoluteTick);
}

bool MidiSequencer::SetLoopRangeTicks(u32 startTick, u32 endTick) {
    if (startTick >= endTick) {
        ClearLoopRange();
        return false;
    }
    loopStartTick_ = startTick;
    loopEndTick_ = endTick;
    loopStartSample_ = TickToSample(startTick);
    loopEndSample_ = TickToSample(endTick);
    if (loopStartSample_ >= loopEndSample_) {
        ClearLoopRange();
        return false;
    }
    loopRangeEnabled_ = true;
    return true;
}

void MidiSequencer::ClearLoopRange() {
    loopRangeEnabled_ = false;
    loopStartTick_ = 0;
    loopEndTick_ = 0;
    loopStartSample_ = 0.0;
    loopEndSample_ = 0.0;
}

bool MidiSequencer::IsAtLoopEnd() const {
    return loopRangeEnabled_ && currentSample_ >= loopEndSample_;
}

void MidiSequencer::BuildTempoMap(const MidiFile* file) {
    // デフォルトテンポ（120 BPM = 500000 μs/beat）
    tempoMap_.push_back({ 0, MIDI_DEFAULT_TEMPO_US, 0.0 });

    // テンポイベントを収集（Format 1 はトラック0に存在することが多いが全トラック探索）
    std::vector<MidiEvent> tempoEvents;
    for (int t = 0; t < file->TrackCount(); ++t) {
        for (const auto& ev : file->Track(t).Events()) {
            if (ev.type == MidiEventType::MetaTempo)
                tempoEvents.push_back(ev);
        }
    }
    std::stable_sort(tempoEvents.begin(), tempoEvents.end(),
        [](const MidiEvent& a, const MidiEvent& b) {
            return a.absoluteTick < b.absoluteTick;
        });

    for (const auto& ev : tempoEvents) {
        if (ev.absoluteTick == 0) {
            // tick=0 のテンポはデフォルトを上書き
            tempoMap_[0].tempoUs = ev.tempoUs;
            continue;
        }
        // ダングリング参照を防ぐためコピーで取得（push_back でベクターが再確保される可能性がある）
        const u32    prevTick         = tempoMap_.back().tick;
        const u32    prevTempoUs      = tempoMap_.back().tempoUs;
        const double prevSampleOffset = tempoMap_.back().sampleOffset;

        double tickDelta    = static_cast<double>(ev.absoluteTick - prevTick);
        double secPerTick   = prevTempoUs / 1000000.0 / division_;
        double sampleOffset = prevSampleOffset + tickDelta * secPerTick * sampleRate_;
        tempoMap_.push_back({ ev.absoluteTick, ev.tempoUs, sampleOffset });
    }
}

void MidiSequencer::MergeTracks(const MidiFile* file) {
    // 総イベント数を事前計算して push_back による再確保を防ぐ
    size_t totalEvents = 0;
    const int trackCount = file->TrackCount();
    for (int t = 0; t < trackCount; ++t)
        totalEvents += file->Track(t).Events().size();
    mergedEvents_.reserve(totalEvents);

    // N-way マージ: priority_queue<{tick, trackIndex, eventIndex}>
    using Entry = std::tuple<u32, int, size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    for (int t = 0; t < trackCount; ++t) {
        if (!file->Track(t).Events().empty())
            pq.push({ file->Track(t).Events()[0].absoluteTick, t, 0 });
    }

    while (!pq.empty()) {
        auto [tick, trackIdx, evIdx] = pq.top();
        pq.pop();

        mergedEvents_.push_back(file->Track(trackIdx).Events()[evIdx]);

        size_t next = evIdx + 1;
        if (next < file->Track(trackIdx).Events().size()) {
            pq.push({
                file->Track(trackIdx).Events()[next].absoluteTick,
                trackIdx,
                next
            });
        }
    }
}

double MidiSequencer::TickToSample(u32 tick) const {
    // テンポマップを逆引き: tick を含む区間を探す
    const TempoEntry* entry = &tempoMap_[0];
    for (size_t i = 1; i < tempoMap_.size(); ++i) {
        if (tempoMap_[i].tick > tick) break;
        entry = &tempoMap_[i];
    }
    double tickDelta  = static_cast<double>(tick - entry->tick);
    double secPerTick = entry->tempoUs / 1000000.0 / division_;
    return entry->sampleOffset + tickDelta * secPerTick * sampleRate_;
}

double MidiSequencer::TotalSamples() const {
    if (mergedEvents_.empty()) {
        return 0.0;
    }
    return TickToSample(mergedEvents_.back().absoluteTick);
}

u32 MidiSequencer::SamplesToNextEvent() const {
    double targetSample = 0.0;
    if (IsFinished()) {
        if (!loopRangeEnabled_) return 0;
        targetSample = loopEndSample_;
    } else {
        targetSample = nextEventSample_;
        if (loopRangeEnabled_) {
            targetSample = std::min(targetSample, loopEndSample_);
        }
    }
    double diff = targetSample - currentSample_;
    if (diff <= 0.0) return 0;
    return static_cast<u32>(diff);
}

const MidiEvent* MidiSequencer::ConsumeEvent() {
    if (IsFinished()) return nullptr;
    const MidiEvent* ev = &mergedEvents_[eventCursor_++];
    if (!IsFinished()) {
        nextEventSample_ = TickToSample(mergedEvents_[eventCursor_].absoluteTick);
    }
    return ev;
}

bool MidiSequencer::IsFinished() const {
    return eventCursor_ >= mergedEvents_.size();
}

} // namespace XArkMidi

