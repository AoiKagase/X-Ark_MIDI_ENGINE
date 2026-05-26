/*
 * Compare high-frequency spectral peaks between two WAV files.
 *
 * Usage:
 *   analyze_wav_high_peaks.exe <base.wav> <target.wav> [start_sec] [end_sec] [min_hz] [max_hz]
 *
 * Notes:
 * - 16-bit PCM mono/stereo WAV only.
 * - Reports bins where target has stronger magnitude than base.
 */

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

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

bool LoadMonoRange(const std::string& path, double startSec, double endSec,
                   uint32_t& outSampleRate, std::vector<double>& outMono,
                   std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "Failed to open";
        return false;
    }

    WavInfo info{};
    if (!ParseWav(in, info, err)) {
        return false;
    }
    outSampleRate = info.sampleRate;

    const uint32_t bytesPerFrame = static_cast<uint32_t>(info.channels) * 2u;
    const uint32_t totalFrames = (bytesPerFrame == 0) ? 0u : (info.dataSize / bytesPerFrame);
    const uint32_t startFrame = static_cast<uint32_t>(
        std::min<double>(totalFrames, std::max(0.0, startSec * static_cast<double>(info.sampleRate))));
    const uint32_t endFrame = static_cast<uint32_t>(
        std::min<double>(totalFrames, endSec * static_cast<double>(info.sampleRate)));
    if (endFrame <= startFrame) {
        err = "No frames in selected range";
        return false;
    }

    in.clear();
    in.seekg(static_cast<std::streamoff>(info.dataOffset) + static_cast<std::streamoff>(startFrame) * bytesPerFrame,
             std::ios::beg);

    const uint32_t framesToRead = endFrame - startFrame;
    outMono.resize(framesToRead);
    std::vector<uint8_t> frameBytes(bytesPerFrame);
    for (uint32_t i = 0; i < framesToRead; ++i) {
        in.read(reinterpret_cast<char*>(frameBytes.data()), bytesPerFrame);
        if (!in) {
            err = "Unexpected EOF";
            return false;
        }
        const int16_t l = ReadI16LE(frameBytes.data());
        const int16_t r = (info.channels == 2) ? ReadI16LE(frameBytes.data() + 2) : l;
        outMono[i] = 0.5 * (static_cast<double>(l) + static_cast<double>(r)) / 32768.0;
    }

    return true;
}

void FFTInplace(std::vector<std::complex<double>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(a[i], a[j]);
        }
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<double> u = a[i + j];
                const std::complex<double> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

std::vector<double> ComputeAverageSpectrum(const std::vector<double>& mono, uint32_t sampleRate) {
    constexpr size_t kFftSize = 4096;
    constexpr size_t kHop = kFftSize / 2;
    std::vector<double> accum(kFftSize / 2 + 1, 0.0);
    if (mono.size() < kFftSize) {
        return accum;
    }

    std::vector<double> window(kFftSize, 0.0);
    for (size_t i = 0; i < kFftSize; ++i) {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / static_cast<double>(kFftSize - 1));
    }

    size_t frames = 0;
    std::vector<std::complex<double>> buf(kFftSize);
    for (size_t offset = 0; offset + kFftSize <= mono.size(); offset += kHop) {
        for (size_t i = 0; i < kFftSize; ++i) {
            buf[i] = std::complex<double>(mono[offset + i] * window[i], 0.0);
        }
        FFTInplace(buf);
        for (size_t k = 0; k <= kFftSize / 2; ++k) {
            const double mag2 = std::norm(buf[k]);
            accum[k] += mag2;
        }
        ++frames;
    }

    if (frames == 0) {
        return accum;
    }
    for (double& v : accum) {
        v /= static_cast<double>(frames);
    }
    (void)sampleRate;
    return accum;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0]
                  << " <base.wav> <target.wav> [start_sec] [end_sec] [min_hz] [max_hz]\n";
        return 1;
    }

    const std::string basePath = argv[1];
    const std::string targetPath = argv[2];
    const double startSec = (argc >= 4) ? std::max(0.0, std::atof(argv[3])) : 0.0;
    const double endSec = (argc >= 5) ? std::max(startSec, std::atof(argv[4])) : 120.0;
    const double minHz = (argc >= 6) ? std::max(0.0, std::atof(argv[5])) : 1500.0;
    const double maxHz = (argc >= 7) ? std::max(minHz, std::atof(argv[6])) : 18000.0;

    uint32_t srBase = 0;
    uint32_t srTarget = 0;
    std::vector<double> baseMono;
    std::vector<double> targetMono;
    std::string err;

    if (!LoadMonoRange(basePath, startSec, endSec, srBase, baseMono, err)) {
        std::cerr << "Base load error: " << err << "\n";
        return 1;
    }
    if (!LoadMonoRange(targetPath, startSec, endSec, srTarget, targetMono, err)) {
        std::cerr << "Target load error: " << err << "\n";
        return 1;
    }
    if (srBase != srTarget) {
        std::cerr << "Sample rate mismatch: " << srBase << " vs " << srTarget << "\n";
        return 1;
    }
    if (baseMono.empty() || targetMono.empty()) {
        std::cerr << "Empty signal range\n";
        return 1;
    }

    const size_t minFrames = std::min(baseMono.size(), targetMono.size());
    baseMono.resize(minFrames);
    targetMono.resize(minFrames);

    const std::vector<double> specBase = ComputeAverageSpectrum(baseMono, srBase);
    const std::vector<double> specTarget = ComputeAverageSpectrum(targetMono, srBase);
    if (specBase.size() != specTarget.size() || specBase.empty()) {
        std::cerr << "Spectrum compute failed\n";
        return 1;
    }

    constexpr size_t kFftSize = 4096;
    struct PeakRow {
        double freq = 0.0;
        double diffDb = 0.0;
        double targetDb = 0.0;
        double baseDb = 0.0;
    };
    std::vector<PeakRow> peaks;
    peaks.reserve(specBase.size());
    const double eps = 1.0e-18;
    for (size_t k = 1; k < specBase.size(); ++k) {
        const double freq = static_cast<double>(k) * static_cast<double>(srBase) / static_cast<double>(kFftSize);
        if (freq < minHz || freq > maxHz) {
            continue;
        }
        const double baseDb = 10.0 * std::log10(specBase[k] + eps);
        const double targetDb = 10.0 * std::log10(specTarget[k] + eps);
        const double diffDb = targetDb - baseDb;
        if (targetDb < -90.0) {
            continue;
        }
        peaks.push_back(PeakRow{freq, diffDb, targetDb, baseDb});
    }

    std::sort(peaks.begin(), peaks.end(), [](const PeakRow& a, const PeakRow& b) {
        return a.diffDb > b.diffDb;
    });

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "base: " << basePath << "\n";
    std::cout << "target: " << targetPath << "\n";
    std::cout << "sample_rate: " << srBase << "\n";
    std::cout << "range_sec: " << startSec << " - " << endSec << "\n";
    std::cout << "freq_range_hz: " << minHz << " - " << maxHz << "\n";
    std::cout << "top_target_stronger_peaks:\n";
    const size_t limit = std::min<size_t>(20, peaks.size());
    for (size_t i = 0; i < limit; ++i) {
        const auto& p = peaks[i];
        std::cout << "  freq_hz=" << p.freq
                  << " diff_db=" << p.diffDb
                  << " target_db=" << p.targetDb
                  << " base_db=" << p.baseDb << "\n";
    }

    return 0;
}
