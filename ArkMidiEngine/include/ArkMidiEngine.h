#pragma once

#ifdef ARKMIDIENGINE_EXPORTS
#  define AME_API __declspec(dllexport)
#else
#  define AME_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum AmeResult_ {
    AME_OK               =  0,
    AME_ERR_INVALID_ARG  = -1,
    AME_ERR_PARSE_MIDI   = -2,
    AME_ERR_PARSE_SF2    = -3,
    AME_ERR_OUT_OF_MEM   = -4,
    AME_ERR_NOT_INIT     = -5,
    AME_ERR_PARSE_DLS    = -6,
    AME_ERR_UNSUPPORTED  = -7,
    AME_ERR_IO           = -8,
} AmeResult;

typedef enum AmeSoundBankKind_ {
    AME_SOUNDBANK_AUTO = 0,
    AME_SOUNDBANK_SF2  = 1,
    AME_SOUNDBANK_DLS  = 2,
} AmeSoundBankKind;

typedef struct AmeEngine_* AmeEngine;

AME_API AmeResult AmeCreateEngineFromPaths(
    const wchar_t*     midiPath,
    const wchar_t*     soundBankPath,
    AmeSoundBankKind   soundBankKind,
    unsigned int       sampleRate,
    unsigned int       numChannels,
    AmeEngine*         outEngine
);

AME_API AmeResult AmeRender(
    AmeEngine      engine,
    short*         outBuffer,
    unsigned int   numFrames,
    unsigned int*  outWritten
);

AME_API int AmeIsFinished(AmeEngine engine);
AME_API void AmeDestroyEngine(AmeEngine engine);
AME_API const char* AmeGetVersion(void);
AME_API const char* AmeGetLastError(void);

#ifdef __cplusplus
}
#endif
