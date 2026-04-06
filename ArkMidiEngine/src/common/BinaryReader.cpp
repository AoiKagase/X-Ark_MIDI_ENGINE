#include "BinaryReader.h"

namespace ArkMidi {

u8 BinaryReader::ReadU8() {
    CheckAvailable(1);
    return data_[pos_++];
}

u16 BinaryReader::ReadU16BE() {
    CheckAvailable(2);
    u16 v = (static_cast<u16>(data_[pos_]) << 8)
           | static_cast<u16>(data_[pos_ + 1]);
    pos_ += 2;
    return v;
}

u32 BinaryReader::ReadU32BE() {
    CheckAvailable(4);
    u32 v = (static_cast<u32>(data_[pos_    ]) << 24)
           | (static_cast<u32>(data_[pos_ + 1]) << 16)
           | (static_cast<u32>(data_[pos_ + 2]) <<  8)
           |  static_cast<u32>(data_[pos_ + 3]);
    pos_ += 4;
    return v;
}

u16 BinaryReader::ReadU16LE() {
    CheckAvailable(2);
    u16 v =  static_cast<u16>(data_[pos_    ])
           | (static_cast<u16>(data_[pos_ + 1]) << 8);
    pos_ += 2;
    return v;
}

u32 BinaryReader::ReadU32LE() {
    CheckAvailable(4);
    u32 v =  static_cast<u32>(data_[pos_    ])
           | (static_cast<u32>(data_[pos_ + 1]) <<  8)
           | (static_cast<u32>(data_[pos_ + 2]) << 16)
           | (static_cast<u32>(data_[pos_ + 3]) << 24);
    pos_ += 4;
    return v;
}

u32 BinaryReader::ReadVLQ() {
    u32 value = 0;
    for (int i = 0; i < 4; ++i) {
        u8 b = ReadU8();
        value = (value << 7) | (b & 0x7F);
        if ((b & 0x80) == 0)
            return value;
    }
    throw std::runtime_error("BinaryReader: VLQ too long");
}

void BinaryReader::Skip(size_t n) {
    CheckAvailable(n);
    pos_ += n;
}

void BinaryReader::Seek(size_t pos) {
    if (pos > size_)
        throw std::runtime_error("BinaryReader: seek out of range");
    pos_ = pos;
}

BinaryReader BinaryReader::ReadSlice(size_t length) {
    CheckAvailable(length);
    BinaryReader slice(data_ + pos_, length);
    pos_ += length;
    return slice;
}

} // namespace ArkMidi
