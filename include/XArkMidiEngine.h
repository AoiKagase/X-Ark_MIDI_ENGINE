/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#pragma once

#if defined(_WIN32)
#  ifdef XARKMIDIENGINE_EXPORTS
#    define XAME_API __declspec(dllexport)
#  else
#    define XAME_API __declspec(dllimport)
#  endif
#else
#  define XAME_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum XAmeResult_ {
    XAME_OK               =  0,
    XAME_ERR_INVALID_ARG  = -1,
    XAME_ERR_PARSE_MIDI   = -2,
    XAME_ERR_PARSE_SF2    = -3,
    XAME_ERR_OUT_OF_MEM   = -4,
    XAME_ERR_NOT_INIT     = -5,
    XAME_ERR_PARSE_DLS    = -6,
    XAME_ERR_UNSUPPORTED  = -7,
    XAME_ERR_IO           = -8,
} XAmeResult;

typedef enum XAmeSoundBankKind_ {
    XAME_SOUNDBANK_AUTO = 0,
    XAME_SOUNDBANK_SF2  = 1,
    XAME_SOUNDBANK_DLS  = 2,
} XAmeSoundBankKind;

typedef enum XAmeCompatibilityFlags_ {
    XAME_COMPAT_NONE = 0,
    XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER = 1u << 0,
    XAME_COMPAT_ENABLE_SF2_SAMPLE_PITCH_CORRECTION = 1u << 1,
} XAmeCompatibilityFlags;

typedef struct XAmeCreateOptions_ {
    unsigned int        structSize;
    unsigned long long  maxSampleDataBytes;
    unsigned int        maxSf2PdtaEntries;
    unsigned int        maxDlsPoolTableEntries;
    unsigned int        compatibilityFlags;
} XAmeCreateOptions;

typedef struct XAmeChannelKeyEvent_ {
    unsigned char channel;
    unsigned char key;
    unsigned char isNoteOn;
    unsigned char reserved;
    unsigned short velocity;
    unsigned short reserved2;
} XAmeChannelKeyEvent;

typedef struct XAmeEngine_* XAmeEngine;

XAME_API XAmeResult XAmeCreateEngineFromPaths(
    const wchar_t*     midiPath,
    const wchar_t*     soundBankPath,
    XAmeSoundBankKind   soundBankKind,
    unsigned int       sampleRate,
    unsigned int       numChannels,
    XAmeEngine*         outEngine
);

XAME_API XAmeResult XAmeCreateEngineWithOptions(
    const wchar_t*            midiPath,
    const wchar_t*            soundBankPath,
    XAmeSoundBankKind          soundBankKind,
    unsigned int              sampleRate,
    unsigned int              numChannels,
    const XAmeCreateOptions*   options,
    XAmeEngine*                outEngine
);

// UTF-8 path variants (preferred on Linux; available on all platforms)
XAME_API XAmeResult XAmeCreateEngineFromPathsUtf8(
    const char*        midiPath,
    const char*        soundBankPath,
    XAmeSoundBankKind   soundBankKind,
    unsigned int       sampleRate,
    unsigned int       numChannels,
    XAmeEngine*         outEngine
);

XAME_API XAmeResult XAmeCreateEngineWithOptionsUtf8(
    const char*               midiPath,
    const char*               soundBankPath,
    XAmeSoundBankKind          soundBankKind,
    unsigned int              sampleRate,
    unsigned int              numChannels,
    const XAmeCreateOptions*   options,
    XAmeEngine*                outEngine
);

XAME_API XAmeResult XAmeRender(
    XAmeEngine      engine,
    short*         outBuffer,
    unsigned int   numFrames,
    unsigned int*  outWritten
);

XAME_API XAmeResult XAmeSetChannelMuteMask(XAmeEngine engine, unsigned int channelMask);
XAME_API XAmeResult XAmeSetChannelSoloMask(XAmeEngine engine, unsigned int channelMask);
XAME_API unsigned int XAmeGetChannelMuteMask(XAmeEngine engine);
XAME_API unsigned int XAmeGetChannelSoloMask(XAmeEngine engine);
XAME_API int XAmeGetChannelProgram(XAmeEngine engine, unsigned int channel);
XAME_API unsigned int XAmeGetChannelActiveNoteCount(XAmeEngine engine, unsigned int channel);
XAME_API unsigned int XAmeGetChannelActiveKeyMaskWord(XAmeEngine engine, unsigned int channel, unsigned int wordIndex);
XAME_API int XAmePopChannelKeyEvent(XAmeEngine engine, XAmeChannelKeyEvent* outEvent);

XAME_API int XAmeIsFinished(XAmeEngine engine);
XAME_API void XAmeDestroyEngine(XAmeEngine engine);
XAME_API const char* XAmeGetVersion(void);
XAME_API const char* XAmeGetLastError(void);

#ifdef __cplusplus
}
#endif

