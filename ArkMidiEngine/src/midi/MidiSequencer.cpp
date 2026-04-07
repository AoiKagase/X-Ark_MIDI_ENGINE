#include "MidiSequencer.h"
#include <algorithm>
#include <queue>
#include <cmath>
#include <stdexcept>

namespace ArkMidi {

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

u32 MidiSequencer::SamplesToNextEvent() const {
    if (IsFinished()) return 0;
    double diff = nextEventSample_ - currentSample_;
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

} // namespace ArkMidi
