/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

/*
 * X-ArkMidiEngine テスト用コンソールアプリ
 * WAVファイルに書き出すことで動作確認できる
 *
 * 使用方法:
 *   X-ArkMidiTest.exe <input.mid> <input.sf2|input.dls> <output.wav>
 *   X-ArkMidiTest.exe <input.mid> <input.sf2|input.dls> <output.wav> [--solo <1-16>] [--mute <1-16>] [--chunk <frames>]
 *                                                     [--max-seconds <sec>] [--progress-seconds <sec>]
 *                                                     [--compat-mode <engine-default|sf2-legacy|sf2-spec-204|sf2-render-tuned|0-3>]
 *                                                     [--disable-internal-effects] [--sample-rate <hz>] [--channels <1|2>]
 *                                                     [--output-stage <standard|enhanced-loud|enhanced-natural|enhanced-warm>]
 */

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdint.h>
#include <string>
#include <cctype>

// DLL ヘッダー（DLL をビルド後にパスを通すこと）
#include "../include/XArkMidiEngine.h"

// ---- WAV ヘッダー書き出しユーティリティ ----

static void WriteU16LE(FILE* f, uint16_t v) {
    uint8_t b[2] = { static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8) };
    fwrite(b, 1, 2, f);
}

static void WriteU32LE(FILE* f, uint32_t v) {
    uint8_t b[4] = {
        static_cast<uint8_t>(v),       static_cast<uint8_t>(v >> 8),
        static_cast<uint8_t>(v >> 16), static_cast<uint8_t>(v >> 24)
    };
    fwrite(b, 1, 4, f);
}

static void WriteWavHeader(FILE* f, uint32_t sampleRate, uint16_t channels,
                            uint16_t bitsPerSample, uint32_t dataBytes) {
    uint32_t byteRate    = sampleRate * channels * bitsPerSample / 8;
    uint16_t blockAlign  = channels * bitsPerSample / 8;
    uint32_t chunkSize   = 36 + dataBytes;

    fwrite("RIFF", 1, 4, f);
    WriteU32LE(f, chunkSize);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    WriteU32LE(f, 16);           // fmt chunk size
    WriteU16LE(f, 1);            // PCM
    WriteU16LE(f, channels);
    WriteU32LE(f, sampleRate);
    WriteU32LE(f, byteRate);
    WriteU16LE(f, blockAlign);
    WriteU16LE(f, bitsPerSample);
    fwrite("data", 1, 4, f);
    WriteU32LE(f, dataBytes);
}

// ---- メイン ----

namespace {

void PrintUsage(const char* exeName) {
    std::fprintf(stderr,
                 "Usage: %s <input.mid> <input.sf2|input.dls> <output.wav> "
                 "[--solo <1-16>] [--mute <1-16>] [--chunk <frames>] "
                 "[--max-seconds <sec>] [--progress-seconds <sec>] "
                 "[--compat-mode <engine-default|sf2-legacy|sf2-spec-204|sf2-render-tuned|0-3>] "
                 "[--disable-internal-effects] [--sample-rate <hz>] [--channels <1|2>] "
                 "[--output-stage <standard|enhanced-loud|enhanced-natural|enhanced-warm>]\n",
                 exeName);
}

bool TryParsePositiveLong(const char* valueText, const char* optionName, long& outValue) {
    const long value = std::strtol(valueText, nullptr, 10);
    if (value <= 0) {
        std::fprintf(stderr, "Invalid %s value: %s\n", optionName, valueText);
        return false;
    }
    outValue = value;
    return true;
}

bool TryParsePositiveDouble(const char* valueText, const char* optionName, double& outValue) {
    const double value = std::strtod(valueText, nullptr);
    if (!(value > 0.0)) {
        std::fprintf(stderr, "Invalid %s value: %s\n", optionName, valueText);
        return false;
    }
    outValue = value;
    return true;
}

bool TryParseChannelMaskArgument(const char* valueText, const char* optionName, unsigned int& mask) {
    long channel = 0;
    if (!TryParsePositiveLong(valueText, optionName, channel) || channel > 16) {
        std::fprintf(stderr, "Invalid %s channel: %s\n", optionName, valueText);
        return false;
    }
    mask |= (1u << static_cast<unsigned int>(channel - 1));
    return true;
}

bool TryParseCompatibilityModeArgument(const char* valueText, unsigned int& outMode) {
    char* numericEnd = nullptr;
    const long modeValue = std::strtol(valueText, &numericEnd, 10);
    if (numericEnd != valueText && numericEnd != nullptr && *numericEnd == '\0') {
        if (modeValue >= static_cast<long>(XAME_COMPAT_MODE_ENGINE_DEFAULT) &&
            modeValue <= static_cast<long>(XAME_COMPAT_MODE_SF2_RENDER_TUNED)) {
            outMode = static_cast<unsigned int>(modeValue);
            return true;
        }
        std::fprintf(stderr, "Invalid --compat-mode numeric value: %s\n", valueText);
        return false;
    }

    std::string normalized(valueText);
    for (char& c : normalized) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '_') {
            c = '-';
        }
    }

    if (normalized == "engine-default" || normalized == "default") {
        outMode = XAME_COMPAT_MODE_ENGINE_DEFAULT;
        return true;
    }
    if (normalized == "sf2-legacy" || normalized == "legacy") {
        outMode = XAME_COMPAT_MODE_SF2_LEGACY;
        return true;
    }
    if (normalized == "sf2-spec-204" || normalized == "sf2-spec" || normalized == "spec-204" ||
        normalized == "spec204") {
        outMode = XAME_COMPAT_MODE_SF2_SPEC_204;
        return true;
    }
    if (normalized == "sf2-render-tuned" || normalized == "sf2-tuned" || normalized == "render-tuned" ||
        normalized == "tuned") {
        outMode = XAME_COMPAT_MODE_SF2_RENDER_TUNED;
        return true;
    }

    std::fprintf(stderr, "Invalid --compat-mode value: %s\n", valueText);
    return false;
}

bool TryParseChannelCountArgument(const char* valueText, unsigned int& outValue) {
    long channelCount = 0;
    if (!TryParsePositiveLong(valueText, "--channels", channelCount)) {
        return false;
    }
    if (channelCount != 1 && channelCount != 2) {
        std::fprintf(stderr, "Invalid --channels value: %s (must be 1 or 2)\n", valueText);
        return false;
    }
    outValue = static_cast<unsigned int>(channelCount);
    return true;
}

enum class OutputStageOption {
    Standard,
    EnhancedLoud,
    EnhancedNatural,
    EnhancedWarm
};

bool TryParseOutputStageArgument(const char* valueText, OutputStageOption& outValue) {
    std::string normalized(valueText);
    for (char& c : normalized) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (c == '_') {
            c = '-';
        }
    }

    if (normalized == "standard" || normalized == "off" || normalized == "none") {
        outValue = OutputStageOption::Standard;
        return true;
    }
    if (normalized == "enhanced-loud" || normalized == "loud") {
        outValue = OutputStageOption::EnhancedLoud;
        return true;
    }
    if (normalized == "enhanced-natural" || normalized == "natural") {
        outValue = OutputStageOption::EnhancedNatural;
        return true;
    }
    if (normalized == "enhanced-warm" || normalized == "warm") {
        outValue = OutputStageOption::EnhancedWarm;
        return true;
    }

    std::fprintf(stderr, "Invalid --output-stage value: %s\n", valueText);
    return false;
}

}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        PrintUsage(argv[0]);
        return 1;
    }

    const char* midiPath = argv[1];
    const char* soundBankPath = argv[2];
    const char* wavPath       = argv[3];

    unsigned int chunkFrames = 4096;
    unsigned int soloMask = 0;
    unsigned int muteMask = 0;
    double maxSeconds = 0.0;
    double progressSeconds = 5.0;
    unsigned int compatibilityMode = XAME_COMPAT_MODE_ENGINE_DEFAULT;
    bool hasCompatibilityMode = false;
    bool disableInternalEffects = false;
    unsigned int sampleRate = 44100;
    unsigned int numChannels = 2;
    OutputStageOption outputStage = OutputStageOption::Standard;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--chunk") == 0 && i + 1 < argc) {
            long value = 0;
            if (!TryParsePositiveLong(argv[i + 1], "--chunk", value)) {
                return 1;
            }
            chunkFrames = static_cast<unsigned int>(value);
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--solo") == 0 && i + 1 < argc) {
            if (!TryParseChannelMaskArgument(argv[i + 1], "--solo", soloMask)) {
                return 1;
            }
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--mute") == 0 && i + 1 < argc) {
            if (!TryParseChannelMaskArgument(argv[i + 1], "--mute", muteMask)) {
                return 1;
            }
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--max-seconds") == 0 && i + 1 < argc) {
            if (!TryParsePositiveDouble(argv[i + 1], "--max-seconds", maxSeconds)) {
                return 1;
            }
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--progress-seconds") == 0 && i + 1 < argc) {
            if (!TryParsePositiveDouble(argv[i + 1], "--progress-seconds", progressSeconds)) {
                return 1;
            }
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--compat-mode") == 0 && i + 1 < argc) {
            if (!TryParseCompatibilityModeArgument(argv[i + 1], compatibilityMode)) {
                return 1;
            }
            hasCompatibilityMode = true;
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--disable-internal-effects") == 0) {
            disableInternalEffects = true;
            continue;
        }
        if (std::strcmp(argv[i], "--sample-rate") == 0 && i + 1 < argc) {
            long value = 0;
            if (!TryParsePositiveLong(argv[i + 1], "--sample-rate", value)) {
                return 1;
            }
            sampleRate = static_cast<unsigned int>(value);
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--channels") == 0 && i + 1 < argc) {
            if (!TryParseChannelCountArgument(argv[i + 1], numChannels)) {
                return 1;
            }
            ++i;
            continue;
        }
        if (std::strcmp(argv[i], "--output-stage") == 0 && i + 1 < argc) {
            if (!TryParseOutputStageArgument(argv[i + 1], outputStage)) {
                return 1;
            }
            ++i;
            continue;
        }
        std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        PrintUsage(argv[0]);
        return 1;
    }

    // エンジン生成（UTF-8 API を使用）
    XAmeCreateOptions options{};
    options.structSize = sizeof(options);
    options.compatibilityFlags = XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER;
    if (disableInternalEffects) {
        options.compatibilityFlags |= XAME_COMPAT_DISABLE_INTERNAL_EFFECTS;
    }
    if (hasCompatibilityMode) {
        options.compatibilityMode = compatibilityMode;
    }
    switch (outputStage) {
    case OutputStageOption::EnhancedLoud:
        options.compatibilityFlags |= XAME_COMPAT_ENABLE_ENHANCED_OUTPUT_STAGE;
        break;
    case OutputStageOption::EnhancedNatural:
        options.compatibilityFlags |= XAME_COMPAT_ENABLE_ENHANCED_OUTPUT_STAGE;
        options.compatibilityFlags |= XAME_COMPAT_ENHANCED_OUTPUT_STAGE_NATURAL;
        break;
    case OutputStageOption::EnhancedWarm:
        options.compatibilityFlags |= XAME_COMPAT_ENABLE_ENHANCED_OUTPUT_STAGE;
        options.compatibilityFlags |= XAME_COMPAT_ENHANCED_OUTPUT_STAGE_WARM;
        break;
    case OutputStageOption::Standard:
    default:
        break;
    }

    XAmeEngine engine = nullptr;
    XAmeResult r = XAmeCreateEngineWithOptionsUtf8(
        midiPath,
        soundBankPath,
        XAME_SOUNDBANK_AUTO,
        sampleRate,
        numChannels,
        &options,
        &engine);

    if (r != XAME_OK) {
        fprintf(stderr, "XAmeCreateEngine failed: %s\n", XAmeGetLastError());
        return 1;
    }

    std::printf("Engine created. Rendering...\n");
    std::printf("Render config: sampleRate=%u channels=%u compatMode=%u flags=0x%08X\n",
                sampleRate, numChannels, options.compatibilityMode, options.compatibilityFlags);

    if (muteMask != 0) {
        XAmeSetChannelMuteMask(engine, muteMask);
    }
    if (soloMask != 0) {
        XAmeSetChannelSoloMask(engine, soloMask);
    }

    // WAV ファイルを開く（先にヘッダーを仮書き込み）
    FILE* wavFile = fopen(wavPath, "wb");
    if (!wavFile) {
        fprintf(stderr, "Failed to open output file: %s\n", wavPath);
        XAmeDestroyEngine(engine);
        return 1;
    }

    // 仮ヘッダー（dataBytes = 0）
    WriteWavHeader(wavFile, sampleRate, static_cast<uint16_t>(numChannels), 16, 0);

    // レンダリングループ
    std::vector<short> buf(chunkFrames * numChannels);
    uint32_t totalFrames = 0;
    const uint32_t maxFrames =
        (maxSeconds > 0.0)
            ? static_cast<uint32_t>(maxSeconds * static_cast<double>(sampleRate) + 0.5)
            : 0u;
    const uint32_t progressEveryFrames =
        (progressSeconds > 0.0)
            ? static_cast<uint32_t>(progressSeconds * static_cast<double>(sampleRate) + 0.5)
            : 0u;
    uint32_t nextProgressFrame = progressEveryFrames;

    while (!XAmeIsFinished(engine) && (maxFrames == 0 || totalFrames < maxFrames)) {
        unsigned int written = 0;
        unsigned int requestFrames = chunkFrames;
        if (maxFrames != 0) {
            const uint32_t remaining = maxFrames - totalFrames;
            requestFrames = std::min<unsigned int>(requestFrames, remaining);
        }
        XAmeRender(engine, buf.data(), requestFrames, &written);
        fwrite(buf.data(), sizeof(short) * numChannels, written, wavFile);
        totalFrames += written;

        if (progressEveryFrames != 0 && totalFrames >= nextProgressFrame) {
            std::printf("  %.1f sec rendered...\n", static_cast<double>(totalFrames) / sampleRate);
            nextProgressFrame += progressEveryFrames;
        }
    }

    // WAV ヘッダーを正しいサイズで上書き
    uint32_t dataBytes = totalFrames * numChannels * sizeof(short);
    fseek(wavFile, 0, SEEK_SET);
    WriteWavHeader(wavFile, sampleRate, static_cast<uint16_t>(numChannels), 16, dataBytes);
    fclose(wavFile);

    std::printf("Done! %.2f sec (%u frames) -> %s\n",
                static_cast<double>(totalFrames) / sampleRate, totalFrames, wavPath);

    XAmeDestroyEngine(engine);
    return 0;
}


