#include <cstdio>
#include <vector>
#include <map>
#include <tuple>

#include "../X-ArkMidiEngine/src/common/BinaryReader.h"

using namespace XArkMidi;

static std::vector<u8> ReadFile(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return {};
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<u8> data(size);
    std::fread(data.data(), 1, size, f);
    std::fclose(f);
    return data;
}

static u32 MakeFourCC(const char* s) {
    return (static_cast<u32>(s[0]))
         | (static_cast<u32>(s[1]) << 8)
         | (static_cast<u32>(s[2]) << 16)
         | (static_cast<u32>(s[3]) << 24);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <input.sf2>\n", argv[0]);
        return 1;
    }

    auto data = ReadFile(argv[1]);
    if (data.empty()) {
        std::fprintf(stderr, "Failed to read file.\n");
        return 1;
    }

    BinaryReader r(data.data(), data.size());
    if (r.ReadU32LE() != MakeFourCC("RIFF")) return 1;
    u32 riffSize = r.ReadU32LE();
    if (r.ReadU32LE() != MakeFourCC("sfbk")) return 1;

    std::map<std::tuple<u16, u16, u16, u16>, int> counts;
    size_t end = r.Tell() + (riffSize - 4);
    while (r.Tell() < end && !r.IsEof()) {
        u32 chunkId = r.ReadU32LE();
        u32 chunkSize = r.ReadU32LE();
        if (chunkId == MakeFourCC("LIST")) {
            u32 listType = r.ReadU32LE();
            auto list = r.ReadSlice(chunkSize - 4);
            if (listType == MakeFourCC("pdta")) {
                while (!list.IsEof() && list.Remaining() >= 8) {
                    u32 subId = list.ReadU32LE();
                    u32 subSize = list.ReadU32LE();
                    auto sub = list.ReadSlice(subSize);
                    if (subId == MakeFourCC("pmod") || subId == MakeFourCC("imod")) {
                        while (!sub.IsEof() && sub.Remaining() >= 10) {
                            u16 src = sub.ReadU16LE();
                            u16 dst = sub.ReadU16LE();
                            i16 amt = static_cast<i16>(sub.ReadU16LE());
                            u16 amtSrc = sub.ReadU16LE();
                            u16 trans = sub.ReadU16LE();
                            counts[{src, dst, amtSrc, trans}]++;
                            (void)amt;
                        }
                    }
                    if (subSize & 1) list.Skip(1);
                }
            }
        } else {
            r.Skip(chunkSize);
        }
        if (chunkSize & 1) r.Skip(1);
    }

    for (const auto& [key, count] : counts) {
        const auto& [src, dst, amtSrc, trans] = key;
        std::printf("src=0x%04X dst=%u amtSrc=0x%04X trans=%u count=%d\n",
                    src, dst, amtSrc, trans, count);
    }
    return 0;
}

