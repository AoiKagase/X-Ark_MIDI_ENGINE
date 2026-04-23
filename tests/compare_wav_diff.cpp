/*
 * Compare two 16-bit PCM WAV files (mono/stereo) and report max sample diff.
 *
 * Usage:
 *   compare_wav_diff.exe <a.wav> <b.wav> [report_threshold]
 */

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

uint16_t ReadU16LE(std::istream& in) {
    uint8_t b[2] = {};
    in.read(reinterpret_cast<char*>(b), 2);
    return static_cast<uint16_t>(b[0] | (static_cast<uint16_t>(b[1]) << 8));
}

uint32_t ReadU32LE(std::istream& in) {
    uint8_t b[4] = {};
    in.read(reinterpret_cast<char*>(b), 4);
    return static_cast<uint32_t>(b[0] |
                                 (static_cast<uint32_t>(b[1]) << 8) |
                                 (static_cast<uint32_t>(b[2]) << 16) |
                                 (static_cast<uint32_t>(b[3]) << 24));
}

bool ReadFourCC(std::istream& in, char out[5]) {
    char id[4] = {};
    in.read(id, 4);
    if (!in) return false;
    std::memcpy(out, id, 4);
    out[4] = '\0';
    return true;
}

struct WavInfo {
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    uint32_t dataOffset = 0;
    uint32_t dataSize = 0;
};

bool ParseWav(std::istream& in, WavInfo& info, std::string& err) {
    char riff[5] = {};
    if (!ReadFourCC(in, riff) || std::strcmp(riff, "RIFF") != 0) {
        err = "Not RIFF";
        return false;
    }
    (void)ReadU32LE(in);
    char wave[5] = {};
    if (!ReadFourCC(in, wave) || std::strcmp(wave, "WAVE") != 0) {
        err = "Not WAVE";
        return false;
    }

    bool gotFmt = false;
    bool gotData = false;
    while (in && (!gotFmt || !gotData)) {
        char chunkId[5] = {};
        if (!ReadFourCC(in, chunkId)) break;
        const uint32_t chunkSize = ReadU32LE(in);
        if (!in) break;
        const std::streamoff chunkDataPos = in.tellg();
        if (std::strcmp(chunkId, "fmt ") == 0) {
            if (chunkSize < 16) {
                err = "fmt too small";
                return false;
            }
            const uint16_t audioFormat = ReadU16LE(in);
            info.channels = ReadU16LE(in);
            info.sampleRate = ReadU32LE(in);
            (void)ReadU32LE(in);
            (void)ReadU16LE(in);
            info.bitsPerSample = ReadU16LE(in);
            if (audioFormat != 1) {
                err = "Not PCM";
                return false;
            }
            gotFmt = true;
        } else if (std::strcmp(chunkId, "data") == 0) {
            info.dataOffset = static_cast<uint32_t>(chunkDataPos);
            info.dataSize = chunkSize;
            gotData = true;
        }
        std::streamoff next = chunkDataPos + static_cast<std::streamoff>(chunkSize);
        if ((chunkSize & 1u) != 0u) next += 1;
        in.seekg(next, std::ios::beg);
    }

    if (!gotFmt || !gotData) {
        err = "Missing fmt/data";
        return false;
    }
    if (info.bitsPerSample != 16) {
        err = "Only 16-bit supported";
        return false;
    }
    if (info.channels != 1 && info.channels != 2) {
        err = "Only mono/stereo supported";
        return false;
    }
    return true;
}

int16_t ReadI16LE(const uint8_t* p) {
    return static_cast<int16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

int AbsI32(int v) { return v < 0 ? -v : v; }

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <a.wav> <b.wav> [report_threshold]\n";
        return 1;
    }

    const std::string aPath = argv[1];
    const std::string bPath = argv[2];
    const int reportThreshold = (argc >= 4) ? std::max(1, std::atoi(argv[3])) : 2000;

    std::ifstream a(aPath, std::ios::binary);
    std::ifstream b(bPath, std::ios::binary);
    if (!a) {
        std::cerr << "Failed to open: " << aPath << "\n";
        return 1;
    }
    if (!b) {
        std::cerr << "Failed to open: " << bPath << "\n";
        return 1;
    }

    WavInfo ai{}, bi{};
    std::string err;
    if (!ParseWav(a, ai, err)) {
        std::cerr << "Parse error (a): " << err << "\n";
        return 1;
    }
    if (!ParseWav(b, bi, err)) {
        std::cerr << "Parse error (b): " << err << "\n";
        return 1;
    }

    if (ai.channels != bi.channels || ai.sampleRate != bi.sampleRate || ai.bitsPerSample != bi.bitsPerSample) {
        std::cerr << "WAV formats differ\n";
        return 1;
    }

    const uint32_t bytesPerFrame = (ai.channels * (ai.bitsPerSample / 8u));
    const uint32_t framesA = (bytesPerFrame != 0) ? (ai.dataSize / bytesPerFrame) : 0u;
    const uint32_t framesB = (bytesPerFrame != 0) ? (bi.dataSize / bytesPerFrame) : 0u;
    const uint32_t frames = std::min(framesA, framesB);

    a.clear();
    b.clear();
    a.seekg(ai.dataOffset, std::ios::beg);
    b.seekg(bi.dataOffset, std::ios::beg);

    std::vector<uint8_t> bufA, bufB;
    constexpr uint32_t kChunkFrames = 8192;
    bufA.resize(static_cast<size_t>(kChunkFrames) * bytesPerFrame);
    bufB.resize(static_cast<size_t>(kChunkFrames) * bytesPerFrame);

    int maxDiffL = 0, maxDiffR = 0;
    uint32_t maxAtFrameL = 0, maxAtFrameR = 0;
    uint64_t largeDiffFrames = 0;

    uint32_t frameBase = 0;
    while (frameBase < frames) {
        const uint32_t framesThis = std::min(kChunkFrames, frames - frameBase);
        const size_t bytesThis = static_cast<size_t>(framesThis) * bytesPerFrame;
        a.read(reinterpret_cast<char*>(bufA.data()), static_cast<std::streamsize>(bytesThis));
        b.read(reinterpret_cast<char*>(bufB.data()), static_cast<std::streamsize>(bytesThis));
        if (a.gcount() != static_cast<std::streamsize>(bytesThis) ||
            b.gcount() != static_cast<std::streamsize>(bytesThis)) {
            break;
        }

        for (uint32_t f = 0; f < framesThis; ++f) {
            const uint8_t* pa = bufA.data() + static_cast<size_t>(f) * bytesPerFrame;
            const uint8_t* pb = bufB.data() + static_cast<size_t>(f) * bytesPerFrame;
            const int16_t aL = ReadI16LE(pa + 0);
            const int16_t bL = ReadI16LE(pb + 0);
            const int diffL = static_cast<int>(aL) - static_cast<int>(bL);
            const int absDiffL = AbsI32(diffL);
            if (absDiffL > maxDiffL) {
                maxDiffL = absDiffL;
                maxAtFrameL = frameBase + f;
            }

            int absDiffR = 0;
            if (ai.channels == 2) {
                const int16_t aR = ReadI16LE(pa + 2);
                const int16_t bR = ReadI16LE(pb + 2);
                const int diffR = static_cast<int>(aR) - static_cast<int>(bR);
                absDiffR = AbsI32(diffR);
                if (absDiffR > maxDiffR) {
                    maxDiffR = absDiffR;
                    maxAtFrameR = frameBase + f;
                }
            }

            if (absDiffL >= reportThreshold || absDiffR >= reportThreshold) {
                ++largeDiffFrames;
            }
        }

        frameBase += framesThis;
    }

    const double sr = static_cast<double>(ai.sampleRate);
    std::cout << "Compared frames: " << frames << "\n";
    std::cout << "Max abs diff L: " << maxDiffL << " at t=" << (static_cast<double>(maxAtFrameL) / sr) << "s\n";
    if (ai.channels == 2) {
        std::cout << "Max abs diff R: " << maxDiffR << " at t=" << (static_cast<double>(maxAtFrameR) / sr) << "s\n";
    }
    std::cout << "Frames with abs diff >= " << reportThreshold << ": " << largeDiffFrames << "\n";

    return 0;
}

