#include <cstdio>
#include <vector>
#include <map>
#include <tuple>
#include <limits>
#include <cstdint>

#include "../X-ArkMidiEngine/src/common/BinaryReader.h"

using namespace XArkMidi;

static std::vector<u8> ReadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return {};
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<u8> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);
    return buf;
}

static u32 MakeFourCC(const char* s) {
    return (static_cast<u32>(s[0]))
         | (static_cast<u32>(s[1]) << 8)
         | (static_cast<u32>(s[2]) << 16)
         | (static_cast<u32>(s[3]) << 24);
}

struct ConnStats {
    int count = 0;
    i32 minScale = std::numeric_limits<i32>::max();
    i32 maxScale = std::numeric_limits<i32>::min();
};

using ConnKey = std::tuple<u16, u16, u16, u16>;

static void ParseArtList(BinaryReader& r, std::map<ConnKey, ConnStats>& counts) {
    while (!r.IsEof() && r.Remaining() >= 8) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId == MakeFourCC("art1") || chunkId == MakeFourCC("art2")) {
            auto art = r.ReadSlice(chunkSize);
            if (art.Remaining() < 8) continue;
            art.ReadU32LE();
            u32 connCount = art.ReadU32LE();
            for (u32 i = 0; i < connCount && art.Remaining() >= 12; ++i) {
                u16 source = art.ReadU16LE();
                u16 control = art.ReadU16LE();
                u16 dest = art.ReadU16LE();
                u16 transform = art.ReadU16LE();
                i32 scale = static_cast<i32>(art.ReadU32LE());
                auto& stats = counts[{source, control, dest, transform}];
                stats.count++;
                stats.minScale = std::min(stats.minScale, scale);
                stats.maxScale = std::max(stats.maxScale, scale);
            }
        } else if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto sub = r.ReadSlice(chunkSize - 4);
            ParseArtList(sub, counts);
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;
    auto data = ReadFile(argv[1]);
    BinaryReader r(data.data(), data.size());
    r.ReadU32LE();
    u32 riffSize = r.ReadU32LE();
    r.ReadU32LE();

    std::map<ConnKey, ConnStats> counts;
    size_t end = r.Tell() + (riffSize - 4);
    while (r.Tell() < end && !r.IsEof()) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto sub = r.ReadSlice(chunkSize - 4);
            ParseArtList(sub, counts);
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }

    for (const auto& [key, stats] : counts) {
        const auto& [source, control, dest, transform] = key;
        std::printf(
            "source=0x%04X control=0x%04X dest=0x%04X transform=0x%04X count=%d minScale=%d maxScale=%d\n",
            source, control, dest, transform, stats.count, stats.minScale, stats.maxScale);
    }
    return 0;
}

