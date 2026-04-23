/*
 * X-ArkMidiEngine: WAV clipping analyzer (16-bit PCM only)
 *
 * Usage:
 *   analyze_wav_clipping.exe <input.wav> [window_ms] [top_n] [start_sec] [end_sec]
 *
 * Prints time windows with the most clipped samples per channel.
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

struct WindowStat {
    double   timeSec = 0.0;
    uint32_t frames = 0;
    uint32_t clipL = 0;
    uint32_t clipR = 0;
    int      maxAbsL = 0;
    int      maxAbsR = 0;
    double   rmsL = 0.0;
    double   rmsR = 0.0;
};

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
        err = "Not a RIFF file";
        return false;
    }
    (void)ReadU32LE(in); // riff size
    char wave[5] = {};
    if (!ReadFourCC(in, wave) || std::strcmp(wave, "WAVE") != 0) {
        err = "Not a WAVE file";
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
                err = "fmt chunk too small";
                return false;
            }
            const uint16_t audioFormat = ReadU16LE(in);
            info.channels = ReadU16LE(in);
            info.sampleRate = ReadU32LE(in);
            (void)ReadU32LE(in); // byte rate
            (void)ReadU16LE(in); // block align
            info.bitsPerSample = ReadU16LE(in);
            if (audioFormat != 1) {
                err = "Only PCM WAV is supported";
                return false;
            }
            gotFmt = true;
        } else if (std::strcmp(chunkId, "data") == 0) {
            info.dataOffset = static_cast<uint32_t>(chunkDataPos);
            info.dataSize = chunkSize;
            gotData = true;
        }

        // Seek to next chunk (word-aligned)
        std::streamoff next = chunkDataPos + static_cast<std::streamoff>(chunkSize);
        if ((chunkSize & 1u) != 0u) next += 1;
        in.seekg(next, std::ios::beg);
    }

    if (!gotFmt) {
        err = "Missing fmt chunk";
        return false;
    }
    if (!gotData) {
        err = "Missing data chunk";
        return false;
    }
    if (info.channels != 1 && info.channels != 2) {
        err = "Only mono/stereo WAV is supported";
        return false;
    }
    if (info.bitsPerSample != 16) {
        err = "Only 16-bit PCM WAV is supported";
        return false;
    }
    return true;
}

int AbsI16(int16_t v) {
    const int x = static_cast<int>(v);
    return x < 0 ? -x : x;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.wav> [window_ms] [top_n] [start_sec] [end_sec]\n";
        return 1;
    }

    const std::string path = argv[1];
    const int windowMs = (argc >= 3) ? std::max(1, std::atoi(argv[2])) : 250;
    const int topN = (argc >= 4) ? std::max(1, std::atoi(argv[3])) : 15;
    const double startSec = (argc >= 5) ? std::max(0.0, std::atof(argv[4])) : 0.0;
    const double endSec = (argc >= 6) ? std::max(startSec, std::atof(argv[5])) : std::numeric_limits<double>::infinity();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    WavInfo info{};
    std::string err;
    if (!ParseWav(in, info, err)) {
        std::cerr << "WAV parse error: " << err << "\n";
        return 1;
    }

    const uint32_t bytesPerSample = info.bitsPerSample / 8u;
    const uint32_t bytesPerFrame = bytesPerSample * info.channels;
    const uint32_t totalFrames = (bytesPerFrame != 0) ? (info.dataSize / bytesPerFrame) : 0u;
    const uint32_t windowFrames = std::max<uint32_t>(1u, static_cast<uint32_t>(
        (static_cast<uint64_t>(info.sampleRate) * static_cast<uint64_t>(windowMs) + 500u) / 1000u));

    in.clear();
    in.seekg(info.dataOffset, std::ios::beg);

    std::vector<WindowStat> windows;
    windows.reserve((totalFrames / windowFrames) + 2);

    std::vector<char> buffer;
    buffer.resize(static_cast<size_t>(windowFrames) * bytesPerFrame);

    uint32_t frameBase = 0;
    while (frameBase < totalFrames) {
        const uint32_t remaining = totalFrames - frameBase;
        const uint32_t framesThis = std::min(windowFrames, remaining);
        const size_t bytesThis = static_cast<size_t>(framesThis) * bytesPerFrame;

        in.read(buffer.data(), static_cast<std::streamsize>(bytesThis));
        if (in.gcount() != static_cast<std::streamsize>(bytesThis)) {
            break;
        }

        WindowStat stat;
        stat.timeSec = static_cast<double>(frameBase) / static_cast<double>(info.sampleRate);
        stat.frames = framesThis;
        if (stat.timeSec + (static_cast<double>(framesThis) / static_cast<double>(info.sampleRate)) <= startSec ||
            stat.timeSec >= endSec) {
            frameBase += framesThis;
            continue;
        }
        double sumSqL = 0.0;
        double sumSqR = 0.0;
        for (uint32_t f = 0; f < framesThis; ++f) {
            const uint8_t* p = reinterpret_cast<const uint8_t*>(buffer.data() + static_cast<size_t>(f) * bytesPerFrame);
            const int16_t sL = static_cast<int16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
            const int16_t sR = (info.channels == 2)
                ? static_cast<int16_t>(p[2] | (static_cast<uint16_t>(p[3]) << 8))
                : sL;

            const int aL = AbsI16(sL);
            const int aR = AbsI16(sR);
            stat.maxAbsL = std::max(stat.maxAbsL, aL);
            stat.maxAbsR = std::max(stat.maxAbsR, aR);
            if (sL == 32767 || sL == -32768) ++stat.clipL;
            if (sR == 32767 || sR == -32768) ++stat.clipR;
            sumSqL += static_cast<double>(aL) * static_cast<double>(aL);
            sumSqR += static_cast<double>(aR) * static_cast<double>(aR);
        }

        if (framesThis > 0) {
            stat.rmsL = std::sqrt(sumSqL / static_cast<double>(framesThis));
            stat.rmsR = std::sqrt(sumSqR / static_cast<double>(framesThis));
        }
        windows.push_back(stat);
        frameBase += framesThis;
    }

    if (windows.empty()) {
        std::cout << "No windows read from " << path
                  << " in range [" << startSec << ", " << endSec << ")\n";
        return 0;
    }

    const auto printWindows = [&](const char* title, const std::vector<WindowStat>& list) {
        std::cout << title << "\n";
        const int limit = std::min<int>(topN, static_cast<int>(list.size()));
        for (int i = 0; i < limit; ++i) {
            const auto& w = list[static_cast<size_t>(i)];
            const double ratioMax = (w.maxAbsL > 0) ? (static_cast<double>(w.maxAbsR) / static_cast<double>(w.maxAbsL)) : 0.0;
            const double ratioRms = (w.rmsL > 0.0) ? (w.rmsR / w.rmsL) : 0.0;
            std::cout
                << "t=" << w.timeSec << "s"
                << " clipL=" << w.clipL
                << " clipR=" << w.clipR
                << " maxL=" << w.maxAbsL
                << " maxR=" << w.maxAbsR
                << " rmsL=" << w.rmsL
                << " rmsR=" << w.rmsR
                << " maxR/maxL=" << ratioMax
                << " rmsR/rmsL=" << ratioRms
                << "\n";
        }
    };

    std::vector<WindowStat> clipped;
    clipped.reserve(windows.size());
    for (const auto& w : windows) {
        if (w.clipL != 0 || w.clipR != 0) clipped.push_back(w);
    }
    if (clipped.empty()) {
        std::cout << "No int16-clipped samples found (window_ms=" << windowMs << ").\n";
    } else {
        std::sort(clipped.begin(), clipped.end(), [](const WindowStat& a, const WindowStat& b) {
            if (a.clipR != b.clipR) return a.clipR > b.clipR;
            if (a.clipL != b.clipL) return a.clipL > b.clipL;
            return a.timeSec < b.timeSec;
        });
        printWindows("Top clipped windows:", clipped);
    }

    {
        auto byRatio = windows;
        std::sort(byRatio.begin(), byRatio.end(), [](const WindowStat& a, const WindowStat& b) {
            const double ra = (a.rmsL > 0.0) ? (a.rmsR / a.rmsL) : 0.0;
            const double rb = (b.rmsL > 0.0) ? (b.rmsR / b.rmsL) : 0.0;
            if (ra != rb) return ra > rb;
            return a.timeSec < b.timeSec;
        });
        printWindows("Top right-heavy windows (by rmsR/rmsL):", byRatio);
    }

    {
        auto byPeak = windows;
        std::sort(byPeak.begin(), byPeak.end(), [](const WindowStat& a, const WindowStat& b) {
            const int pa = std::max(a.maxAbsL, a.maxAbsR);
            const int pb = std::max(b.maxAbsL, b.maxAbsR);
            if (pa != pb) return pa > pb;
            return a.timeSec < b.timeSec;
        });
        printWindows("Top peak windows (by max(maxL,maxR)):", byPeak);
    }

    {
        auto byRightMinusLeft = windows;
        std::sort(byRightMinusLeft.begin(), byRightMinusLeft.end(), [](const WindowStat& a, const WindowStat& b) {
            const int da = a.maxAbsR - a.maxAbsL;
            const int db = b.maxAbsR - b.maxAbsL;
            if (da != db) return da > db;
            return a.timeSec < b.timeSec;
        });
        printWindows("Top right-dominant peak windows (by maxR-maxL):", byRightMinusLeft);
    }

    return 0;
}
