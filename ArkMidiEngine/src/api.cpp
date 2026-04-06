#include "../include/ArkMidiEngine.h"
#include "midi/MidiFile.h"
#include "sf2/Sf2File.h"
#include "dls/DlsFile.h"
#include "synth/Synthesizer.h"
#include <memory>
#include <string>
#include <cwctype>

using namespace ArkMidi;

struct AmeEngine_ {
    MidiFile                      midiFile;
    std::unique_ptr<SoundBank>    soundBank;
    Synthesizer                   synthesizer;
    u32                           sampleRate  = 44100;
    u32                           numChannels = 2;
    bool                          initialized = false;
};

static std::string g_lastError;

static void SetError(const std::string& msg) {
    g_lastError = msg;
}

static std::wstring ToLower(std::wstring s) {
    for (auto& ch : s)
        ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

static SoundBankKind ResolveKind(AmeSoundBankKind kind, const std::wstring& path) {
    if (kind == AME_SOUNDBANK_SF2) return SoundBankKind::Sf2;
    if (kind == AME_SOUNDBANK_DLS) return SoundBankKind::Dls;

    std::wstring lower = ToLower(path);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".sf2")
        return SoundBankKind::Sf2;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".dls")
        return SoundBankKind::Dls;
    return SoundBankKind::Auto;
}

AmeResult AmeCreateEngineFromPaths(
    const wchar_t* midiPath,
    const wchar_t* soundBankPath,
    AmeSoundBankKind soundBankKind,
    unsigned int sampleRate,
    unsigned int numChannels,
    AmeEngine* outEngine)
{
    if (!midiPath || !midiPath[0] || !soundBankPath || !soundBankPath[0] || !outEngine) {
        SetError("MIDI path and sound bank path are required");
        return AME_ERR_INVALID_ARG;
    }
    if (numChannels < 1 || numChannels > 2) {
        SetError("numChannels must be 1 or 2");
        return AME_ERR_INVALID_ARG;
    }
    if (sampleRate == 0) {
        SetError("sampleRate must be > 0");
        return AME_ERR_INVALID_ARG;
    }

    SoundBankKind resolvedKind = ResolveKind(soundBankKind, soundBankPath);
    if (resolvedKind == SoundBankKind::Auto) {
        SetError("Unable to determine sound bank type. Specify SF2 or DLS explicitly.");
        return AME_ERR_UNSUPPORTED;
    }

    AmeEngine_* eng = nullptr;
    try {
        eng = new AmeEngine_();
    } catch (...) {
        SetError("Out of memory");
        return AME_ERR_OUT_OF_MEM;
    }

    eng->sampleRate = sampleRate;
    eng->numChannels = numChannels;

    if (!eng->midiFile.LoadFromFile(midiPath)) {
        SetError("MIDI parse error: " + eng->midiFile.ErrorMessage());
        delete eng;
        return AME_ERR_PARSE_MIDI;
    }

    try {
        if (resolvedKind == SoundBankKind::Sf2) {
            auto sf2 = std::make_unique<Sf2File>();
            if (!sf2->LoadFromFile(soundBankPath)) {
                SetError("SF2 parse error: " + sf2->ErrorMessage());
                delete eng;
                return AME_ERR_PARSE_SF2;
            }
            eng->soundBank = std::move(sf2);
        } else {
            auto dls = std::make_unique<DlsFile>();
            if (!dls->LoadFromFile(soundBankPath)) {
                SetError("DLS parse error: " + dls->ErrorMessage());
                delete eng;
                return AME_ERR_PARSE_DLS;
            }
            eng->soundBank = std::move(dls);
        }
    } catch (...) {
        SetError("Out of memory");
        delete eng;
        return AME_ERR_OUT_OF_MEM;
    }

    if (!eng->synthesizer.Init(&eng->midiFile, eng->soundBank.get(), sampleRate, numChannels)) {
        SetError("Synthesizer init error: " + eng->synthesizer.ErrorMessage());
        delete eng;
        return AME_ERR_NOT_INIT;
    }

    eng->initialized = true;
    *outEngine = eng;
    return AME_OK;
}

AmeResult AmeRender(
    AmeEngine engine,
    short* outBuffer,
    unsigned int numFrames,
    unsigned int* outWritten)
{
    if (!engine || !engine->initialized) {
        SetError("Engine not initialized");
        return AME_ERR_NOT_INIT;
    }
    if (!outBuffer || numFrames == 0) {
        SetError("Invalid argument");
        return AME_ERR_INVALID_ARG;
    }

    u32 written = engine->synthesizer.Render(reinterpret_cast<i16*>(outBuffer), numFrames);
    if (outWritten) *outWritten = written;
    return AME_OK;
}

int AmeIsFinished(AmeEngine engine) {
    if (!engine || !engine->initialized) return 1;
    return engine->synthesizer.IsFinished() ? 1 : 0;
}

void AmeDestroyEngine(AmeEngine engine) {
    delete engine;
}

const char* AmeGetVersion(void) {
    return "2.0.0";
}

const char* AmeGetLastError(void) {
    return g_lastError.c_str();
}
