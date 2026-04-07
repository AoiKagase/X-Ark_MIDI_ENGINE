#pragma once
#include "Types.h"
#include <stdexcept>

namespace XArkMidi {

// バイト列のカーソル付き読み取りクラス
// MIDI用ビッグエンディアン / SF2用リトルエンディアン の両方に対応
class BinaryReader {
public:
    BinaryReader(const u8* data, size_t size)
        : data_(data), size_(size), pos_(0) {}

    // ---- ビッグエンディアン読み取り（MIDI用） ----
    u8  ReadU8();
    u16 ReadU16BE();
    u32 ReadU32BE();

    // ---- リトルエンディアン読み取り（SF2/RIFF用） ----
    u16 ReadU16LE();
    u32 ReadU32LE();

    // ---- MIDIデルタタイム可変長読み取り（VLQ） ----
    u32 ReadVLQ();

    // ---- 位置操作 ----
    void   Skip(size_t n);
    void   Seek(size_t pos);
    size_t Tell() const { return pos_; }
    size_t Size() const { return size_; }
    size_t Remaining() const { return (pos_ < size_) ? (size_ - pos_) : 0; }
    bool   IsEof() const { return pos_ >= size_; }

    // ---- サブリーダー生成（チャンク解析用） ----
    // 現在位置から length バイトのスライスを返し、現在位置を length 進める
    BinaryReader ReadSlice(size_t length);

    // ---- 生ポインタアクセス（サンプルデータ等の大容量コピー用） ----
    const u8* CurrentPtr() const { return data_ + pos_; }

private:
    void CheckAvailable(size_t n) const {
        if (pos_ + n > size_)
            throw std::runtime_error("BinaryReader: unexpected end of data");
    }

    const u8* data_;
    size_t    size_;
    size_t    pos_;
};

} // namespace XArkMidi

