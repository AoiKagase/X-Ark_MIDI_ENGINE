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
 */

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdint.h>
#include <string>

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
                 "[--max-seconds <sec>] [--progress-seconds <sec>]\n",
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
        std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
        PrintUsage(argv[0]);
        return 1;
    }

    // エンジン生成（UTF-8 API を使用）
    const unsigned int SAMPLE_RATE  = 44100;
    const unsigned int NUM_CHANNELS = 2;
    XAmeCreateOptions options{};
    options.structSize = sizeof(options);
    options.compatibilityFlags = XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER;

    XAmeEngine engine = nullptr;
    XAmeResult r = XAmeCreateEngineWithOptionsUtf8(
        midiPath,
        soundBankPath,
        XAME_SOUNDBANK_AUTO,
        SAMPLE_RATE, NUM_CHANNELS,
        &options,
        &engine);

    if (r != XAME_OK) {
        fprintf(stderr, "XAmeCreateEngine failed: %s\n", XAmeGetLastError());
        return 1;
    }

    printf("Engine created. Rendering...\n");

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
    WriteWavHeader(wavFile, SAMPLE_RATE, static_cast<uint16_t>(NUM_CHANNELS), 16, 0);

    // レンダリングループ
    std::vector<short> buf(chunkFrames * NUM_CHANNELS);
    uint32_t totalFrames = 0;
    const uint32_t maxFrames =
        (maxSeconds > 0.0)
            ? static_cast<uint32_t>(maxSeconds * static_cast<double>(SAMPLE_RATE) + 0.5)
            : 0u;
    const uint32_t progressEveryFrames =
        (progressSeconds > 0.0)
            ? static_cast<uint32_t>(progressSeconds * static_cast<double>(SAMPLE_RATE) + 0.5)
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
        fwrite(buf.data(), sizeof(short) * NUM_CHANNELS, written, wavFile);
        totalFrames += written;

        if (progressEveryFrames != 0 && totalFrames >= nextProgressFrame) {
            printf("  %.1f sec rendered...\n", static_cast<double>(totalFrames) / SAMPLE_RATE);
            nextProgressFrame += progressEveryFrames;
        }
    }

    // WAV ヘッダーを正しいサイズで上書き
    uint32_t dataBytes = totalFrames * NUM_CHANNELS * sizeof(short);
    fseek(wavFile, 0, SEEK_SET);
    WriteWavHeader(wavFile, SAMPLE_RATE, static_cast<uint16_t>(NUM_CHANNELS), 16, dataBytes);
    fclose(wavFile);

    printf("Done! %.2f sec (%u frames) -> %s\n",
           static_cast<double>(totalFrames) / SAMPLE_RATE, totalFrames, wavPath);

    XAmeDestroyEngine(engine);
    return 0;
}


