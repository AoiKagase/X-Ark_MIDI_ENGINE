#include "../include/XArkMidiEngine.h"

#include <algorithm>
#include <cctype>
#include <conio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

namespace {

constexpr unsigned int kSampleRate = 44100;
constexpr unsigned int kNumChannels = 2;
constexpr unsigned int kFramesPerBuffer = 2048;
constexpr unsigned int kBufferCount = 4;

struct AudioBuffer {
    std::vector<short> samples;
    WAVEHDR header{};
};

std::string Trim(const std::string& value) {
    size_t start = 0;
    while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    size_t end = value.size();
    while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(start, end - start);
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

bool ParseUserChannel(const std::string& token, unsigned int& channelBit) {
    char* end = nullptr;
    const long channel = std::strtol(token.c_str(), &end, 10);
    if (!end || *end != '\0' || channel < 1 || channel > 16) {
        return false;
    }
    channelBit = 1u << static_cast<unsigned int>(channel - 1);
    return true;
}

void PrintStatus(XAmeEngine engine) {
    const unsigned int muteMask = XAmeGetChannelMuteMask(engine);
    const unsigned int soloMask = XAmeGetChannelSoloMask(engine);
    std::printf("mute=0x%04X solo=0x%04X\n", muteMask & 0xFFFFu, soloMask & 0xFFFFu);
}

void PrintHelp() {
    std::puts("commands:");
    std::puts("  solo <1-16>   : solo one channel");
    std::puts("  solo off      : clear solo");
    std::puts("  mute <1-16>   : add mute for one channel");
    std::puts("  unmute <1-16> : remove mute for one channel");
    std::puts("  mute off      : clear mute");
    std::puts("  status        : show current masks");
    std::puts("  quit          : stop playback");
}

bool QueueBuffer(HWAVEOUT waveOut, XAmeEngine engine, AudioBuffer& buffer) {
    unsigned int written = 0;
    const XAmeResult result = XAmeRender(engine, buffer.samples.data(), kFramesPerBuffer, &written);
    if (result != XAME_OK) {
        std::fprintf(stderr, "XAmeRender failed: %s\n", XAmeGetLastError());
        return false;
    }
    if (written == 0) {
        return false;
    }
    buffer.header.dwBufferLength = written * kNumChannels * sizeof(short);
    buffer.header.dwFlags &= ~WHDR_DONE;
    MMRESULT mmr = waveOutWrite(waveOut, &buffer.header, sizeof(buffer.header));
    if (mmr != MMSYSERR_NOERROR) {
        std::fprintf(stderr, "waveOutWrite failed: %u\n", static_cast<unsigned int>(mmr));
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <input.mid> <input.sf2>\n", argv[0]);
        return 1;
    }

    XAmeCreateOptions options{};
    options.structSize = sizeof(options);
    options.compatibilityFlags = XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER;

    XAmeEngine engine = nullptr;
    const XAmeResult createResult = XAmeCreateEngineWithOptionsUtf8(
        argv[1], argv[2], XAME_SOUNDBANK_SF2, kSampleRate, kNumChannels, &options, &engine);
    if (createResult != XAME_OK) {
        std::fprintf(stderr, "XAmeCreateEngine failed: %s\n", XAmeGetLastError());
        return 1;
    }

    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = static_cast<WORD>(kNumChannels);
    format.nSamplesPerSec = kSampleRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * (format.wBitsPerSample / 8));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    HWAVEOUT waveOut = nullptr;
    MMRESULT mmr = waveOutOpen(&waveOut, WAVE_MAPPER, &format, 0, 0, CALLBACK_NULL);
    if (mmr != MMSYSERR_NOERROR) {
        std::fprintf(stderr, "waveOutOpen failed: %u\n", static_cast<unsigned int>(mmr));
        XAmeDestroyEngine(engine);
        return 1;
    }

    std::vector<AudioBuffer> buffers(kBufferCount);
    for (auto& buffer : buffers) {
        buffer.samples.resize(kFramesPerBuffer * kNumChannels);
        std::memset(&buffer.header, 0, sizeof(buffer.header));
        buffer.header.lpData = reinterpret_cast<LPSTR>(buffer.samples.data());
        buffer.header.dwBufferLength = static_cast<DWORD>(buffer.samples.size() * sizeof(short));
        mmr = waveOutPrepareHeader(waveOut, &buffer.header, sizeof(buffer.header));
        if (mmr != MMSYSERR_NOERROR) {
            std::fprintf(stderr, "waveOutPrepareHeader failed: %u\n", static_cast<unsigned int>(mmr));
            waveOutClose(waveOut);
            XAmeDestroyEngine(engine);
            return 1;
        }
    }

    auto handleCommand = [&](const std::string& line, bool& stopRequested) {
        std::istringstream iss(Trim(line));
        std::string command;
        iss >> command;
        command = ToLower(command);
        if (command.empty()) {
            return;
        }
        if (command == "quit" || command == "exit") {
            stopRequested = true;
            return;
        }
        if (command == "status") {
            PrintStatus(engine);
            return;
        }
        if (command == "help") {
            PrintHelp();
            return;
        }

        std::string arg;
        iss >> arg;
        arg = ToLower(arg);
        if (command == "solo") {
            if (arg == "off" || arg == "clear") {
                XAmeSetChannelSoloMask(engine, 0);
                PrintStatus(engine);
                return;
            }
            unsigned int bit = 0;
            if (!ParseUserChannel(arg, bit)) {
                std::puts("solo expects 1-16, off, or clear");
                return;
            }
            XAmeSetChannelSoloMask(engine, bit);
            PrintStatus(engine);
            return;
        }
        if (command == "mute") {
            if (arg == "off" || arg == "clear") {
                XAmeSetChannelMuteMask(engine, 0);
                PrintStatus(engine);
                return;
            }
            unsigned int bit = 0;
            if (!ParseUserChannel(arg, bit)) {
                std::puts("mute expects 1-16, off, or clear");
                return;
            }
            XAmeSetChannelMuteMask(engine, XAmeGetChannelMuteMask(engine) | bit);
            PrintStatus(engine);
            return;
        }
        if (command == "unmute") {
            unsigned int bit = 0;
            if (!ParseUserChannel(arg, bit)) {
                std::puts("unmute expects 1-16");
                return;
            }
            XAmeSetChannelMuteMask(engine, XAmeGetChannelMuteMask(engine) & ~bit);
            PrintStatus(engine);
            return;
        }
        std::puts("unknown command");
    };

    PrintHelp();
    PrintStatus(engine);
    std::string pendingCommand;
    bool stopRequested = false;
    bool playbackFinished = false;
    while (!stopRequested) {
        while (_kbhit() != 0) {
            const int ch = _getch();
            if (ch == '\r' || ch == '\n') {
                std::putchar('\n');
                handleCommand(pendingCommand, stopRequested);
                pendingCommand.clear();
                continue;
            }
            if (ch == '\b') {
                if (!pendingCommand.empty()) {
                    pendingCommand.pop_back();
                    std::fputs("\b \b", stdout);
                    std::fflush(stdout);
                }
                continue;
            }
            if (ch >= 32 && ch < 127) {
                pendingCommand.push_back(static_cast<char>(ch));
                std::putchar(ch);
                std::fflush(stdout);
            }
        }

        unsigned int queuedCount = 0;
        for (auto& buffer : buffers) {
            if ((buffer.header.dwFlags & WHDR_INQUEUE) != 0) {
                ++queuedCount;
                continue;
            }
            if (playbackFinished) {
                continue;
            }
            if (XAmeIsFinished(engine) != 0) {
                playbackFinished = true;
                continue;
            }
            if (QueueBuffer(waveOut, engine, buffer)) {
                ++queuedCount;
            } else if (XAmeIsFinished(engine) != 0) {
                playbackFinished = true;
            }
        }

        if (playbackFinished && queuedCount == 0) {
            break;
        }
        Sleep(5);
    }
    waveOutReset(waveOut);
    for (auto& buffer : buffers) {
        waveOutUnprepareHeader(waveOut, &buffer.header, sizeof(buffer.header));
    }
    waveOutClose(waveOut);
    XAmeDestroyEngine(engine);
    return 0;
}
