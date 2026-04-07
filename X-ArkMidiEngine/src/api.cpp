#include "../include/XArkMidiEngine.h"
#include "midi/MidiFile.h"
#include "sf2/Sf2File.h"
#include "dls/DlsFile.h"
#include "synth/Synthesizer.h"
#include <exception>
#include <memory>
#include <string>
#include <cwctype>

using namespace XArkMidi;

namespace {

constexpr size_t kDefaultMaxSampleDataBytes = 512ull * 1024ull * 1024ull;
constexpr u32 kDefaultMaxSf2PdtaEntries = 1u << 20;
constexpr u32 kDefaultMaxDlsPoolTableEntries = 1u << 20;

struct CreateLimits {
    size_t maxSampleDataBytes = kDefaultMaxSampleDataBytes;
    u32 maxSf2PdtaEntries = kDefaultMaxSf2PdtaEntries;
    u32 maxDlsPoolTableEntries = kDefaultMaxDlsPoolTableEntries;
};

CreateLimits ResolveCreateLimits(const XAmeCreateOptions* options) {
    CreateLimits limits;
    if (!options)
        return limits;

    if (options->structSize < sizeof(XAmeCreateOptions))
        return limits;

    if (options->maxSampleDataBytes != 0)
        limits.maxSampleDataBytes = static_cast<size_t>(options->maxSampleDataBytes);
    if (options->maxSf2PdtaEntries != 0)
        limits.maxSf2PdtaEntries = options->maxSf2PdtaEntries;
    if (options->maxDlsPoolTableEntries != 0)
        limits.maxDlsPoolTableEntries = options->maxDlsPoolTableEntries;
    return limits;
}

} // namespace

struct XAmeEngine_ {
    MidiFile                      midiFile;
    std::unique_ptr<SoundBank>    soundBank;
    Synthesizer                   synthesizer;
    u32                           sampleRate  = 44100;
    u32                           numChannels = 2;
    bool                          initialized = false;
};

static thread_local std::string g_lastError;

static void SetError(const std::string& msg) {
    g_lastError = msg;
}

static std::wstring ToLower(std::wstring s) {
    for (auto& ch : s)
        ch = static_cast<wchar_t>(std::towlower(ch));
    return s;
}

static SoundBankKind ResolveKind(XAmeSoundBankKind kind, const std::wstring& path) {
    if (kind == XAME_SOUNDBANK_SF2) return SoundBankKind::Sf2;
    if (kind == XAME_SOUNDBANK_DLS) return SoundBankKind::Dls;

    std::wstring lower = ToLower(path);
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".sf2")
        return SoundBankKind::Sf2;
    if (lower.size() >= 4 && lower.substr(lower.size() - 4) == L".dls")
        return SoundBankKind::Dls;
    return SoundBankKind::Auto;
}

XAmeResult XAmeCreateEngineFromPaths(
    const wchar_t* midiPath,
    const wchar_t* soundBankPath,
    XAmeSoundBankKind soundBankKind,
    unsigned int sampleRate,
    unsigned int numChannels,
    XAmeEngine* outEngine)
{
    return XAmeCreateEngineWithOptions(
        midiPath,
        soundBankPath,
        soundBankKind,
        sampleRate,
        numChannels,
        nullptr,
        outEngine);
}

XAmeResult XAmeCreateEngineWithOptions(
    const wchar_t* midiPath,
    const wchar_t* soundBankPath,
    XAmeSoundBankKind soundBankKind,
    unsigned int sampleRate,
    unsigned int numChannels,
    const XAmeCreateOptions* options,
    XAmeEngine* outEngine)
{
    if (!midiPath || !midiPath[0] || !soundBankPath || !soundBankPath[0] || !outEngine) {
        SetError("MIDI path and sound bank path are required");
        return XAME_ERR_INVALID_ARG;
    }
    *outEngine = nullptr;
    if (numChannels < 1 || numChannels > 2) {
        SetError("numChannels must be 1 or 2");
        return XAME_ERR_INVALID_ARG;
    }
    if (sampleRate == 0) {
        SetError("sampleRate must be > 0");
        return XAME_ERR_INVALID_ARG;
    }

    SoundBankKind resolvedKind = ResolveKind(soundBankKind, soundBankPath);
    if (resolvedKind == SoundBankKind::Auto) {
        SetError("Unable to determine sound bank type. Specify SF2 or DLS explicitly.");
        return XAME_ERR_UNSUPPORTED;
    }
    const CreateLimits limits = ResolveCreateLimits(options);
    XAmeEngine_* eng = nullptr;
    try {
        eng = new XAmeEngine_();
        eng->sampleRate = sampleRate;
        eng->numChannels = numChannels;

        if (!eng->midiFile.LoadFromFile(midiPath)) {
            SetError("MIDI parse error: " + eng->midiFile.ErrorMessage());
            delete eng;
            return XAME_ERR_PARSE_MIDI;
        }

        if (resolvedKind == SoundBankKind::Sf2) {
            auto sf2 = std::make_unique<Sf2File>();
            sf2->SetResourceLimits(limits.maxSampleDataBytes, limits.maxSf2PdtaEntries);
            if (!sf2->LoadFromFile(soundBankPath)) {
                SetError("SF2 parse error: " + sf2->ErrorMessage());
                delete eng;
                return XAME_ERR_PARSE_SF2;
            }
            eng->soundBank = std::move(sf2);
        } else {
            auto dls = std::make_unique<DlsFile>();
            dls->SetResourceLimits(limits.maxSampleDataBytes, limits.maxDlsPoolTableEntries);
            if (!dls->LoadFromFile(soundBankPath)) {
                SetError("DLS parse error: " + dls->ErrorMessage());
                delete eng;
                return XAME_ERR_PARSE_DLS;
            }
            eng->soundBank = std::move(dls);
        }

        if (!eng->synthesizer.Init(&eng->midiFile, eng->soundBank.get(), sampleRate, numChannels)) {
            SetError("Synthesizer init error: " + eng->synthesizer.ErrorMessage());
            delete eng;
            return XAME_ERR_NOT_INIT;
        }

        eng->initialized = true;
        *outEngine = eng;
        return XAME_OK;
    } catch (const std::bad_alloc&) {
        SetError("Out of memory");
        delete eng;
        return XAME_ERR_OUT_OF_MEM;
    } catch (const std::exception& e) {
        SetError(std::string("Unhandled exception during engine creation: ") + e.what());
        delete eng;
        return XAME_ERR_NOT_INIT;
    } catch (...) {
        SetError("Unhandled unknown exception during engine creation");
        delete eng;
        return XAME_ERR_NOT_INIT;
    }
}

XAmeResult XAmeRender(
    XAmeEngine engine,
    short* outBuffer,
    unsigned int numFrames,
    unsigned int* outWritten)
{
    if (!engine || !engine->initialized) {
        SetError("Engine not initialized");
        return XAME_ERR_NOT_INIT;
    }
    if (!outBuffer || numFrames == 0) {
        SetError("Invalid argument");
        return XAME_ERR_INVALID_ARG;
    }

    u32 written = engine->synthesizer.Render(reinterpret_cast<i16*>(outBuffer), numFrames);
    if (outWritten) *outWritten = written;
    return XAME_OK;
}

int XAmeIsFinished(XAmeEngine engine) {
    if (!engine || !engine->initialized) return 1;
    return engine->synthesizer.IsFinished() ? 1 : 0;
}

void XAmeDestroyEngine(XAmeEngine engine) {
    delete engine;
}

const char* XAmeGetVersion(void) {
    return "2.0.0";
}

const char* XAmeGetLastError(void) {
    return g_lastError.c_str();
}

