#pragma once

#ifdef XARKMIDIENGINE_EXPORTS
#  define XAME_API __declspec(dllexport)
#else
#  define XAME_API __declspec(dllimport)
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
} XAmeCompatibilityFlags;

typedef struct XAmeCreateOptions_ {
    unsigned int        structSize;
    unsigned long long  maxSampleDataBytes;
    unsigned int        maxSf2PdtaEntries;
    unsigned int        maxDlsPoolTableEntries;
    unsigned int        compatibilityFlags;
} XAmeCreateOptions;

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

XAME_API XAmeResult XAmeRender(
    XAmeEngine      engine,
    short*         outBuffer,
    unsigned int   numFrames,
    unsigned int*  outWritten
);

XAME_API int XAmeIsFinished(XAmeEngine engine);
XAME_API void XAmeDestroyEngine(XAmeEngine engine);
XAME_API const char* XAmeGetVersion(void);
XAME_API const char* XAmeGetLastError(void);

#ifdef __cplusplus
}
#endif

