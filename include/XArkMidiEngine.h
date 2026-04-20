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
    /* Operation completed successfully. 正常に完了しました。 */
    XAME_OK               =  0,
    /* One or more arguments were invalid. 1 つ以上の引数が不正です。 */
    XAME_ERR_INVALID_ARG  = -1,
    /* Failed to parse the input MIDI file. 入力 MIDI ファイルの解析に失敗しました。 */
    XAME_ERR_PARSE_MIDI   = -2,
    /* Failed to parse the input SF2 sound bank. 入力 SF2 サウンドバンクの解析に失敗しました。 */
    XAME_ERR_PARSE_SF2    = -3,
    /* Memory allocation failed. メモリ確保に失敗しました。 */
    XAME_ERR_OUT_OF_MEM   = -4,
    /* Engine or required subsystem was not initialized. エンジンまたは必要なサブシステムが初期化されていません。 */
    XAME_ERR_NOT_INIT     = -5,
    /* Failed to parse the input DLS sound bank. 入力 DLS サウンドバンクの解析に失敗しました。 */
    XAME_ERR_PARSE_DLS    = -6,
    /* Requested feature or input format is not supported. 要求された機能または入力形式は未対応です。 */
    XAME_ERR_UNSUPPORTED  = -7,
    /* I/O error while reading an input file. 入力ファイルの読み込み中に I/O エラーが発生しました。 */
    XAME_ERR_IO           = -8,
} XAmeResult;

typedef enum XAmeSoundBankKind_ {
    /* Detect the sound bank type from the file contents or extension. ファイル内容または拡張子から自動判定します。 */
    XAME_SOUNDBANK_AUTO = 0,
    /* Force SoundFont 2 parsing. SoundFont 2 として解析します。 */
    XAME_SOUNDBANK_SF2  = 1,
    /* Force DLS parsing. DLS として解析します。 */
    XAME_SOUNDBANK_DLS  = 2,
} XAmeSoundBankKind;

typedef enum XAmeCompatibilityFlags_ {
    /* Use the engine's default behavior. エンジンの既定動作を使用します。 */
    XAME_COMPAT_NONE = 0,
    /* Retrigger zero-length SF2 loops for compatibility with some banks. 一部バンク互換のため長さ 0 の SF2 ループを再トリガーします。 */
    XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER = 1u << 0,
    /* Apply pitch correction for SF2 sample playback. SF2 サンプル再生時のピッチ補正を有効にします。 */
    XAME_COMPAT_ENABLE_SF2_SAMPLE_PITCH_CORRECTION = 1u << 1,
    /* Non-spec legacy compatibility mode: multiply SF2 preset send with MIDI channel send instead of using the default modulator-driven SF2 behavior. 非仕様の旧互換モードとして、既定の SF2 modulator 駆動動作の代わりに SF2 send と MIDI チャンネル send を乗算合成します。 */
    XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS = 1u << 2,
    /* Apply the SF2 implicit CC7/CC10/CC11 default modulators instead of treating them as global channel controls. SF2 の暗黙 CC7/CC10/CC11 default modulator を有効化し、グローバルなチャンネル音量・パン処理の代わりに使用します。 */
    XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS = 1u << 3,
} XAmeCompatibilityFlags;

/* Optional limits and compatibility overrides used when creating an engine. エンジン生成時の任意制限値と互換設定です。 */
typedef struct XAmeCreateOptions_ {
    /* Size of this structure in bytes. Set to sizeof(XAmeCreateOptions). 構造体サイズをバイト単位で指定します。 */
    unsigned int        structSize;
    /* Maximum total decoded sample data allowed for the loaded bank. 0 uses the default. 読み込むバンクのデコード済みサンプル総量上限です。 */
    unsigned long long  maxSampleDataBytes;
    /* Maximum number of SF2 pdta entries allowed while parsing. 0 uses the default. SF2 の pdta エントリ数上限です。 */
    unsigned int        maxSf2PdtaEntries;
    /* Maximum number of DLS pool table entries allowed while parsing. 0 uses the default. DLS の pool table エントリ数上限です。 */
    unsigned int        maxDlsPoolTableEntries;
    /* Bitwise OR of XAmeCompatibilityFlags values. XAmeCompatibilityFlags の OR 値です。 */
    unsigned int        compatibilityFlags;
    /* Optional UTF-16 path to an external SF2 ROM sample bank used by SoundFont ROM-backed samples. NULL disables ROM sample lookup. SF2 の ROM サンプル参照に使う外部 SF2 ROM バンクの UTF-16 パスです。 */
    const wchar_t*      sf2RomBankPath;
    /* Optional UTF-8 path to an external SF2 ROM sample bank used by SoundFont ROM-backed samples. Used when sf2RomBankPath is NULL. sf2RomBankPath が NULL の場合に使う UTF-8 パスです。 */
    const char*         sf2RomBankPathUtf8;
} XAmeCreateOptions;

/* Key event popped from the engine's per-channel event queue. チャンネル別イベントキューから取得したキーイベントです。 */
typedef struct XAmeChannelKeyEvent_ {
    /* MIDI channel number in the range [0, 15]. MIDI チャンネル番号です。 */
    unsigned char channel;
    /* MIDI key number in the range [0, 127]. MIDI キー番号です。 */
    unsigned char key;
    /* Non-zero for note-on, zero for note-off. 0 以外で note-on、0 で note-off を表します。 */
    unsigned char isNoteOn;
    /* Reserved for future use. 将来拡張用の予約領域です。 */
    unsigned char reserved;
    /* MIDI velocity value associated with the event. イベントに対応するベロシティ値です。 */
    unsigned short velocity;
    /* Reserved for future use. 将来拡張用の予約領域です。 */
    unsigned short reserved2;
} XAmeChannelKeyEvent;

/* Opaque engine handle returned by the create functions. 生成関数が返す不透明なエンジンハンドルです。 */
typedef struct XAmeEngine_* XAmeEngine;

/*
 * Create a rendering engine from UTF-16 file system paths.
 * UTF-16 のファイルパスからレンダリングエンジンを生成します。
 * Preferred on Windows.
 * Windows での利用を推奨します。
 *
 * sampleRate must be greater than 0.
 * sampleRate は 0 より大きい必要があります。
 * numChannels must be 1 (mono) or 2 (stereo).
 * numChannels は 1 (mono) または 2 (stereo) を指定してください。
 * On success, outEngine receives a handle that must be released with XAmeDestroyEngine().
 * 成功時は outEngine にハンドルが返り、XAmeDestroyEngine() で解放する必要があります。
 */
XAME_API XAmeResult XAmeCreateEngineFromPaths(
    const wchar_t*     midiPath,
    const wchar_t*     soundBankPath,
    XAmeSoundBankKind   soundBankKind,
    unsigned int       sampleRate,
    unsigned int       numChannels,
    XAmeEngine*         outEngine
);

/*
 * Create a rendering engine from UTF-16 file system paths using explicit options.
 * UTF-16 のファイルパスと明示的なオプションからレンダリングエンジンを生成します。
 * Preferred on Windows.
 * Windows での利用を推奨します。
 *
 * If options is not NULL, options->structSize must be set to sizeof(XAmeCreateOptions).
 * options が NULL でない場合、options->structSize に sizeof(XAmeCreateOptions) を設定してください。
 * On success, outEngine receives a handle that must be released with XAmeDestroyEngine().
 * 成功時は outEngine にハンドルが返り、XAmeDestroyEngine() で解放する必要があります。
 */
XAME_API XAmeResult XAmeCreateEngineWithOptions(
    const wchar_t*            midiPath,
    const wchar_t*            soundBankPath,
    XAmeSoundBankKind          soundBankKind,
    unsigned int              sampleRate,
    unsigned int              numChannels,
    const XAmeCreateOptions*   options,
    XAmeEngine*                outEngine
);

/*
 * Create a rendering engine from UTF-8 file system paths.
 * UTF-8 のファイルパスからレンダリングエンジンを生成します。
 * Preferred on Linux and available on all platforms.
 * Linux での利用を推奨し、全プラットフォームで使用できます。
 *
 * sampleRate must be greater than 0.
 * sampleRate は 0 より大きい必要があります。
 * numChannels must be 1 (mono) or 2 (stereo).
 * numChannels は 1 (mono) または 2 (stereo) を指定してください。
 * On success, outEngine receives a handle that must be released with XAmeDestroyEngine().
 * 成功時は outEngine にハンドルが返り、XAmeDestroyEngine() で解放する必要があります。
 */
XAME_API XAmeResult XAmeCreateEngineFromPathsUtf8(
    const char*        midiPath,
    const char*        soundBankPath,
    XAmeSoundBankKind   soundBankKind,
    unsigned int       sampleRate,
    unsigned int       numChannels,
    XAmeEngine*         outEngine
);

/*
 * Create a rendering engine from UTF-8 file system paths using explicit options.
 * UTF-8 のファイルパスと明示的なオプションからレンダリングエンジンを生成します。
 * Preferred on Linux and available on all platforms.
 * Linux での利用を推奨し、全プラットフォームで使用できます。
 *
 * If options is not NULL, options->structSize must be set to sizeof(XAmeCreateOptions).
 * options が NULL でない場合、options->structSize に sizeof(XAmeCreateOptions) を設定してください。
 * On success, outEngine receives a handle that must be released with XAmeDestroyEngine().
 * 成功時は outEngine にハンドルが返り、XAmeDestroyEngine() で解放する必要があります。
 */
XAME_API XAmeResult XAmeCreateEngineWithOptionsUtf8(
    const char*               midiPath,
    const char*               soundBankPath,
    XAmeSoundBankKind          soundBankKind,
    unsigned int              sampleRate,
    unsigned int              numChannels,
    const XAmeCreateOptions*   options,
    XAmeEngine*                outEngine
);

/*
 * Render up to numFrames audio frames into outBuffer.
 * outBuffer に最大 numFrames フレームの音声を書き込みます。
 *
 * outBuffer must have room for numFrames * channelCount samples.
 * outBuffer は numFrames * チャンネル数 分のサンプル領域を確保してください。
 * outWritten receives the number of frames actually written.
 * outWritten には実際に書き込まれたフレーム数が返ります。
 * When the end of the song is reached, outWritten may be 0 and XAmeIsFinished() becomes non-zero.
 * 曲末に到達すると outWritten が 0 になり、XAmeIsFinished() が非 0 になります。
 */
XAME_API XAmeResult XAmeRender(
    XAmeEngine      engine,
    short*         outBuffer,
    unsigned int   numFrames,
    unsigned int*  outWritten
);

/* Set a 16-bit MIDI channel mute mask. Bit 0 controls channel 0, bit 15 controls channel 15. 16bit の MIDI チャンネルミュートマスクを設定します。 */
XAME_API XAmeResult XAmeSetChannelMuteMask(XAmeEngine engine, unsigned int channelMask);
/* Set a 16-bit MIDI channel solo mask. When non-zero, only selected channels are rendered. 16bit の MIDI チャンネルソロマスクを設定します。 */
XAME_API XAmeResult XAmeSetChannelSoloMask(XAmeEngine engine, unsigned int channelMask);
/* Get the current 16-bit MIDI channel mute mask. 現在のミュートマスクを取得します。 */
XAME_API unsigned int XAmeGetChannelMuteMask(XAmeEngine engine);
/* Get the current 16-bit MIDI channel solo mask. 現在のソロマスクを取得します。 */
XAME_API unsigned int XAmeGetChannelSoloMask(XAmeEngine engine);
/* Get the current program number for a MIDI channel, or a negative value on failure. 指定チャンネルの現在のプログラム番号を取得します。 */
XAME_API int XAmeGetChannelProgram(XAmeEngine engine, unsigned int channel);
/* Get the number of currently active notes for a MIDI channel. 指定チャンネルで現在発音中のノート数を取得します。 */
XAME_API unsigned int XAmeGetChannelActiveNoteCount(XAmeEngine engine, unsigned int channel);
/* Get one 32-bit word of the active key bitset for a MIDI channel. アクティブキーのビットセット 32bit 分を取得します。 */
XAME_API unsigned int XAmeGetChannelActiveKeyMaskWord(XAmeEngine engine, unsigned int channel, unsigned int wordIndex);
/* Pop the oldest queued channel key event. Returns non-zero if an event was written to outEvent. 最も古いキーイベントを取り出します。 */
XAME_API int XAmePopChannelKeyEvent(XAmeEngine engine, XAmeChannelKeyEvent* outEvent);

/* Return non-zero once all audio has been rendered. 全音声のレンダリング完了後に非 0 を返します。 */
XAME_API int XAmeIsFinished(XAmeEngine engine);
/* Destroy an engine handle created by one of the XAmeCreateEngine* functions. XAmeCreateEngine* 系で生成したハンドルを破棄します。 */
XAME_API void XAmeDestroyEngine(XAmeEngine engine);
/* Get a null-terminated version string for the library. ライブラリのバージョン文字列を取得します。 */
XAME_API const char* XAmeGetVersion(void);
/* Get the last human-readable error message generated by the library. 最後に発生した可読エラーメッセージを取得します。 */
XAME_API const char* XAmeGetLastError(void);

#ifdef __cplusplus
}
#endif

