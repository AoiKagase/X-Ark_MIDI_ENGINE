/*
 * ArkMidiEngine テスト用コンソールアプリ
 * WAVファイルに書き出すことで動作確認できる
 *
 * 使用方法:
 *   ArkMidiTest.exe <input.mid> <input.sf2> <output.wav>
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <stdint.h>
#include <string>

// DLL ヘッダー（DLL をビルド後にパスを通すこと）
#include "../ArkMidiEngine/include/ArkMidiEngine.h"

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

// ---- ファイル読み込みユーティリティ ----

static std::wstring ToWide(const char* path) {
    std::wstring result;
    while (*path) {
        result.push_back(static_cast<unsigned char>(*path));
        ++path;
    }
    return result;
}

// ---- メイン ----

int main(int argc, char* argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <input.mid> <input.sf2> <output.wav>\n", argv[0]);
        return 1;
    }

    const char* midiPath = argv[1];
    const char* sf2Path  = argv[2];
    const char* wavPath  = argv[3];

    // エンジン生成
    const unsigned int SAMPLE_RATE  = 44100;
    const unsigned int NUM_CHANNELS = 2;
    std::wstring midiPathW = ToWide(midiPath);
    std::wstring sf2PathW = ToWide(sf2Path);

    AmeEngine engine = nullptr;
    AmeResult r = AmeCreateEngineFromPaths(
        midiPathW.c_str(),
        sf2PathW.c_str(),
        AME_SOUNDBANK_AUTO,
        SAMPLE_RATE, NUM_CHANNELS,
        &engine);

    if (r != AME_OK) {
        fprintf(stderr, "AmeCreateEngine failed: %s\n", AmeGetLastError());
        return 1;
    }

    printf("Engine created. Rendering...\n");

    // WAV ファイルを開く（先にヘッダーを仮書き込み）
    FILE* wavFile = fopen(wavPath, "wb");
    if (!wavFile) {
        fprintf(stderr, "Failed to open output file: %s\n", wavPath);
        AmeDestroyEngine(engine);
        return 1;
    }

    // 仮ヘッダー（dataBytes = 0）
    WriteWavHeader(wavFile, SAMPLE_RATE, static_cast<uint16_t>(NUM_CHANNELS), 16, 0);

    // レンダリングループ
    const unsigned int FRAMES_PER_CHUNK = 4096;
    std::vector<short> buf(FRAMES_PER_CHUNK * NUM_CHANNELS);
    uint32_t totalFrames = 0;

    while (!AmeIsFinished(engine)) {
        unsigned int written = 0;
        AmeRender(engine, buf.data(), FRAMES_PER_CHUNK, &written);
        fwrite(buf.data(), sizeof(short) * NUM_CHANNELS, written, wavFile);
        totalFrames += written;

        if (totalFrames % (SAMPLE_RATE * 5) < FRAMES_PER_CHUNK) {
            printf("  %.1f sec rendered...\n", static_cast<double>(totalFrames) / SAMPLE_RATE);
        }
    }

    // WAV ヘッダーを正しいサイズで上書き
    uint32_t dataBytes = totalFrames * NUM_CHANNELS * sizeof(short);
    fseek(wavFile, 0, SEEK_SET);
    WriteWavHeader(wavFile, SAMPLE_RATE, static_cast<uint16_t>(NUM_CHANNELS), 16, dataBytes);
    fclose(wavFile);

    printf("Done! %.2f sec (%u frames) -> %s\n",
           static_cast<double>(totalFrames) / SAMPLE_RATE, totalFrames, wavPath);

    AmeDestroyEngine(engine);
    return 0;
}
