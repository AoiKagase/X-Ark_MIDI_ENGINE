/*
 * Analyze WAV tonal balance using simple one-pole band split.
 *
 * Usage:
 *   analyze_wav_tone.exe <input.wav> [start_sec] [end_sec]
 *
 * Notes:
 * - 16-bit PCM mono/stereo WAV only.
 * - Band split is approximate (one-pole filters), intended for relative comparisons.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

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

inline int16_t ReadI16LE(const uint8_t* p) {
    return static_cast<int16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

inline double OnePoleAlpha(double cutoffHz, double sampleRate) {
    if (!(cutoffHz > 0.0) || !(sampleRate > 0.0)) return 1.0;
    const double x = -2.0 * 3.14159265358979323846 * cutoffHz / sampleRate;
    return 1.0 - std::exp(x);
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input.wav> [start_sec] [end_sec]\n";
        return 1;
    }

    const std::string path = argv[1];
    const double startSec = (argc >= 3) ? std::max(0.0, std::atof(argv[2])) : 0.0;
    const double endSec = (argc >= 4) ? std::max(startSec, std::atof(argv[3])) : std::numeric_limits<double>::infinity();

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::cerr << "Failed to open: " << path << "\n";
        return 1;
    }

    WavInfo info{};
    std::string err;
    if (!ParseWav(in, info, err)) {
        std::cerr << "Parse error: " << err << "\n";
        return 1;
    }

    const uint32_t bytesPerFrame = static_cast<uint32_t>(info.channels) * 2u;
    const uint32_t totalFrames = (bytesPerFrame != 0) ? (info.dataSize / bytesPerFrame) : 0u;
    const uint32_t startFrame = static_cast<uint32_t>(
        std::min<double>(totalFrames, std::max(0.0, startSec * static_cast<double>(info.sampleRate))));
    const uint32_t endFrame = static_cast<uint32_t>(
        std::min<double>(totalFrames, endSec * static_cast<double>(info.sampleRate)));

    if (endFrame <= startFrame) {
        std::cerr << "No frames in selected range\n";
        return 1;
    }

    in.clear();
    in.seekg(static_cast<std::streamoff>(info.dataOffset) + static_cast<std::streamoff>(startFrame) * bytesPerFrame, std::ios::beg);

    const double sr = static_cast<double>(info.sampleRate);
    const double a250 = OnePoleAlpha(250.0, sr);
    const double a1000 = OnePoleAlpha(1000.0, sr);
    const double a4000 = OnePoleAlpha(4000.0, sr);
    const double a12000 = OnePoleAlpha(12000.0, sr);

    double lp250 = 0.0;
    double lp1000 = 0.0;
    double lp4000 = 0.0;
    double lp12000 = 0.0;

    double sumSqTotal = 0.0;
    double sumSqB0 = 0.0; // <250
    double sumSqB1 = 0.0; // 250-1k
    double sumSqB2 = 0.0; // 1k-4k
    double sumSqB3 = 0.0; // 4k-12k
    double sumSqB4 = 0.0; // >12k
    double peakAbs = 0.0;
    uint64_t clipCount = 0;

    const uint32_t framesToRead = endFrame - startFrame;
    constexpr uint32_t kChunkFrames = 8192;
    std::string chunk;
    chunk.resize(static_cast<size_t>(kChunkFrames) * bytesPerFrame);

    uint32_t frameBase = 0;
    while (frameBase < framesToRead) {
        const uint32_t framesThis = std::min(kChunkFrames, framesToRead - frameBase);
        const size_t bytesThis = static_cast<size_t>(framesThis) * bytesPerFrame;
        in.read(&chunk[0], static_cast<std::streamsize>(bytesThis));
        if (in.gcount() != static_cast<std::streamsize>(bytesThis)) {
            break;
        }

        const uint8_t* p = reinterpret_cast<const uint8_t*>(chunk.data());
        for (uint32_t i = 0; i < framesThis; ++i) {
            const int16_t l = ReadI16LE(p + 0);
            const int16_t r = (info.channels == 2) ? ReadI16LE(p + 2) : l;
            p += bytesPerFrame;

            if (l == 32767 || l == -32768) ++clipCount;
            if (r == 32767 || r == -32768) ++clipCount;

            const double mono = 0.5 * (static_cast<double>(l) + static_cast<double>(r)) / 32768.0;
            const double absMono = std::abs(mono);
            peakAbs = std::max(peakAbs, absMono);
            sumSqTotal += mono * mono;

            lp250 += a250 * (mono - lp250);
            lp1000 += a1000 * (mono - lp1000);
            lp4000 += a4000 * (mono - lp4000);
            lp12000 += a12000 * (mono - lp12000);

            const double b0 = lp250;
            const double b1 = lp1000 - lp250;
            const double b2 = lp4000 - lp1000;
            const double b3 = lp12000 - lp4000;
            const double b4 = mono - lp12000;

            sumSqB0 += b0 * b0;
            sumSqB1 += b1 * b1;
            sumSqB2 += b2 * b2;
            sumSqB3 += b3 * b3;
            sumSqB4 += b4 * b4;
        }

        frameBase += framesThis;
    }

    const double frames = static_cast<double>(frameBase);
    if (frames <= 0.0) {
        std::cerr << "No frames processed\n";
        return 1;
    }

    const double rms = std::sqrt(sumSqTotal / frames);
    const double totalBandEnergy = sumSqB0 + sumSqB1 + sumSqB2 + sumSqB3 + sumSqB4;
    const auto ratio = [&](double e) {
        return (totalBandEnergy > 0.0) ? (100.0 * e / totalBandEnergy) : 0.0;
    };

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "file: " << path << "\n";
    std::cout << "sample_rate: " << info.sampleRate << "\n";
    std::cout << "channels: " << info.channels << "\n";
    std::cout << "frames: " << frameBase << "\n";
    std::cout << "range_sec: " << (static_cast<double>(startFrame) / sr)
              << " - " << (static_cast<double>(startFrame + frameBase) / sr) << "\n";
    std::cout << "rms_mono: " << rms << "\n";
    std::cout << "peak_abs_mono: " << peakAbs << "\n";
    std::cout << "clip_count_samples: " << clipCount << "\n";
    std::cout << "band_ratio_<250Hz: " << ratio(sumSqB0) << "\n";
    std::cout << "band_ratio_250_1k: " << ratio(sumSqB1) << "\n";
    std::cout << "band_ratio_1k_4k: " << ratio(sumSqB2) << "\n";
    std::cout << "band_ratio_4k_12k: " << ratio(sumSqB3) << "\n";
    std::cout << "band_ratio_>12k: " << ratio(sumSqB4) << "\n";

    return 0;
}

