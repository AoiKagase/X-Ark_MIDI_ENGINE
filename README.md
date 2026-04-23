# X-ArkMIDIEngine
[![Build](https://github.com/AoiKagase/X-Ark_MIDI_ENGINE/actions/workflows/build.yml/badge.svg)](https://github.com/AoiKagase/X-Ark_MIDI_ENGINE/actions/workflows/build.yml)

MIDI ファイルを SF2 / DLS サウンドバンクで PCM 音声へレンダリングする共有ライブラリです。Windows では DLL、Linux / macOS では共有ライブラリとして利用できます。

## 概要

- 入力: Standard MIDI File (`.mid`), SoundFont 2 (`.sf2`), Downloadable Sounds (`.dls`)
- 出力: 16-bit interleaved PCM
- 公開 API: C API (`include/XArkMidiEngine.h`)
- 同梱ラッパー: C# P/Invoke (`XArkMidiEngine.cs`)
- 対応: x64, C++17

主な用途:

- MIDI をオフラインでレンダリングして WAV 生成処理へ渡す
- ネイティブアプリや .NET アプリに MIDI 音源機能を組み込む
- チャンネルごとの状態監視付きでレンダリングする

## 主な機能

- SF2 / DLS の自動判定または明示指定
- UTF-16 / UTF-8 の両方のパス API
- レンダリング中のチャンネル mute / solo 制御
- チャンネルごとの program / 発音中ノート / アクティブキー取得
- note on / off イベントキュー取得
- 曲の現在フレーム位置 / 概算長取得
- 最終エラー文字列とライブラリバージョン取得
- AVX2 利用可能環境での SIMD 最適化

## ビルド

### Windows (MSVC / Visual Studio 2022)

```powershell
# Build main DLL
msbuild msvc/XArkMidiEngine.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo /v:minimal

# Build from solution
msbuild msvc/XArkMidiEngine.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo
```

- 出力: `build/msvc/bin/Release/XArkMidiEngine.dll`
- ツールセット: v145
- 言語: C++17
- 対応プラットフォーム: x64

### Linux / macOS (CMake)

```bash
cmake -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
# Output: build/cmake/libXArkMidiEngine.so
```

要件:

- GCC 9+ または Clang 10+
- C++17
- x86-64

## ディレクトリ構成

- `include/` 公開 API ヘッダ
- `src/` 実装本体
- `tests/` テストコードとダンプツール
- `msvc/` Visual Studio ソリューション / プロジェクト
- `tools/` 補助ツール
- `build/` 生成物
- `XArkMidiEngine.cs` C# P/Invoke ラッパー

## C API

公開 API は [include/XArkMidiEngine.h](include/XArkMidiEngine.h) に定義されています。

### 基本型

- `XAmeResult`
  - `XAME_OK`
  - `XAME_ERR_INVALID_ARG`
  - `XAME_ERR_PARSE_MIDI`
  - `XAME_ERR_PARSE_SF2`
  - `XAME_ERR_OUT_OF_MEM`
  - `XAME_ERR_NOT_INIT`
  - `XAME_ERR_PARSE_DLS`
  - `XAME_ERR_UNSUPPORTED`
  - `XAME_ERR_IO`
- `XAmeSoundBankKind`
  - `XAME_SOUNDBANK_AUTO`
  - `XAME_SOUNDBANK_SF2`
  - `XAME_SOUNDBANK_DLS`
- `XAmeCompatibilityFlags`
  - `XAME_COMPAT_SF2_ZERO_LENGTH_LOOP_RETRIGGER`
  - `XAME_COMPAT_ENABLE_SF2_SAMPLE_PITCH_CORRECTION`
  - `XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS`
  - `XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS`

### エンジン生成 API

- `XAmeCreateEngineFromPaths()`
  - UTF-16 パス版。Windows 向け。
- `XAmeCreateEngineWithOptions()`
  - UTF-16 パス版 + `XAmeCreateOptions` 指定。
- `XAmeCreateEngineFromPathsUtf8()`
  - UTF-8 パス版。Linux 向けだが全プラットフォームで使用可能。
- `XAmeCreateEngineWithOptionsUtf8()`
  - UTF-8 パス版 + `XAmeCreateOptions` 指定。

### `XAmeCreateOptions`

`options` を使う場合は `structSize = sizeof(XAmeCreateOptions)` を必ず設定してください。

指定できる項目:

- `maxSampleDataBytes`
- `maxSf2PdtaEntries`
- `maxDlsPoolTableEntries`
- `compatibilityFlags`
- `sf2RomBankPath`
- `sf2RomBankPathUtf8`

`sf2RomBankPath` / `sf2RomBankPathUtf8` は、ROM 参照サンプルを持つ SF2 のために外部 SF2 ROM バンクを指定するフィールドです。

### レンダリング API

- `XAmeRender()`
  - `short*` のインターリーブ PCM バッファへ最大 `numFrames` フレームを書き込みます。
- `XAmeIsFinished()`
  - 全音声のレンダリング完了後に非 0 を返します。
- `XAmeDestroyEngine()`
  - エンジンを破棄します。

### チャンネル制御 / 状態取得 API

- `XAmeSetChannelMuteMask()`
- `XAmeSetChannelSoloMask()`
- `XAmeGetChannelMuteMask()`
- `XAmeGetChannelSoloMask()`
- `XAmeGetChannelProgram()`
- `XAmeGetChannelActiveNoteCount()`
- `XAmeGetChannelActiveKeyMaskWord()`
- `XAmePopChannelKeyEvent()`

`channelMask` は 16bit マスクで、bit 0 が MIDI channel 0、bit 15 が channel 15 に対応します。

### 再生位置 / 補助 API

- `XAmeGetCurrentFramePosition()`
- `XAmeGetLengthFramesEstimate()`
- `XAmeGetVersion()`
- `XAmeGetLastError()`

`XAmeGetLengthFramesEstimate()` はエフェクトテールを除いた概算値です。

## C API 使用例

```c
#include "XArkMidiEngine.h"
#include <stdlib.h>

int main(void) {
    XAmeEngine engine = NULL;
    XAmeResult result = XAmeCreateEngineFromPathsUtf8(
        "example.mid",
        "example.sf2",
        XAME_SOUNDBANK_AUTO,
        44100,
        2,
        &engine
    );
    if (result != XAME_OK) {
        const char* err = XAmeGetLastError();
        return 1;
    }

    short buffer[4096 * 2];
    while (!XAmeIsFinished(engine)) {
        unsigned int written = 0;
        result = XAmeRender(engine, buffer, 4096, &written);
        if (result != XAME_OK) {
            XAmeDestroyEngine(engine);
            return 1;
        }
        if (written == 0) {
            break;
        }
        /* buffer[0 .. written * 2 - 1] を利用 */
    }

    XAmeDestroyEngine(engine);
    return 0;
}
```

## C# ラッパー

[XArkMidiEngine.cs](XArkMidiEngine.cs) に管理ラッパーを同梱しています。

公開している主な要素:

- `XArkMidiEngine.XAmeResult`
- `XArkMidiEngine.SoundBankKind`
- `XArkMidiEngine.CompatibilityFlags`
- `XArkMidiEngine.CreateOptions`
- `XArkMidiEngine.ChannelKeyEvent`
- `XArkMidiEngine.Engine`
- `XArkMidiEngine.XArkMidiException`

`Engine` クラスの主な機能:

- UTF-8 API を使ったエンジン生成
- `Render()`
- `RenderAll()`
- `RenderAllBytes()`
- `IsFinished`
- `ChannelMuteMask`
- `ChannelSoloMask`
- `GetChannelProgram()`
- `GetChannelActiveNoteCount()`
- `GetChannelActiveKeyMaskWord()`
- `TryPopChannelKeyEvent()`
- `CurrentFramePosition`
- `LengthFramesEstimate`

注意:

- 現在の C# ラッパーは UTF-8 版 API を使います。
- `CreateOptions` は `StructSize`、各種上限値、`CompatibilityFlags` を公開しています。
- C ヘッダにある `sf2RomBankPath` / `sf2RomBankPathUtf8` は、現時点の C# `CreateOptions` では公開していません。

### C# 使用例

```csharp
using var engine = new XArkMidiEngine.Engine(
    "example.mid",
    "example.sf2",
    XArkMidiEngine.SoundBankKind.Auto,
    44100,
    2);

short[] pcm = engine.RenderAll();
```

互換オプション付きの例:

```csharp
var options = XArkMidiEngine.CreateOptions.Default();
options.CompatibilityFlags =
    XArkMidiEngine.CompatibilityFlags.EnableSf2SamplePitchCorrection |
    XArkMidiEngine.CompatibilityFlags.ApplySf2ChannelDefaultModulators;

using var engine = new XArkMidiEngine.Engine(
    "example.mid",
    "example.sf2",
    XArkMidiEngine.SoundBankKind.Auto,
    44100,
    2,
    options);
```

## テスト

### Windows でテスト用ツールをビルド

```powershell
msbuild msvc/Sf2Dump.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /nologo
```

### MIDI レンダリングテスト

```powershell
XArkMidiTest.exe <input.mid> <input.sf2> <output.wav>
```

### 補助テスト / 検証コード

- `tests/sf2_compliance.cpp`
  - synthetic SF2 を使って SF2 実装の互換性を検証します。
- `tests/dump_sf2_zone.cpp`
  - 読み込んだ SF2 に未対応 modulator がある場合、その件数を確認できます。

## SF2 実装メモ

現時点では次を優先して実装しています。

- generator の既定値マージ
- key / velocity 強制
- 主要 default modulator
  - velocity -> initial attenuation
  - velocity -> initial filter cutoff
  - CC1 -> vibrato LFO pitch
- ランタイム再評価が必要な source
  - CC
  - poly pressure
  - channel pressure
  - pitch bend
  - pitch wheel sensitivity
- 再評価対象 destination
  - pitch
  - filter
  - attenuation
  - pan
  - volume / mod envelope
  - modulation / vibrato LFO
  - reverb / chorus send
  - tuning / root / exclusive class

既知の制限:

- 一部の modulator destination は未対応です。
- `sfModTransOper` は `linear` と `absolute` のみ対応しています。
- CC7 / CC10 / CC11 の implicit default modulator は既定で無効です。
  必要なら `XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS` を指定してください。
- SF2 send と MIDI send の最終ミキシング方針は既定で加算です。
  `XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS` を指定すると乗算に切り替えられます。

## 注意事項

- `sampleRate` は 0 より大きい値を指定してください。
- `numChannels` は `1` または `2` のみ対応です。
- Windows では UTF-16 API、Linux / macOS では UTF-8 API の利用を推奨します。
- `XAmeRender()` の出力バッファは `numFrames * channelCount` サンプル以上必要です。
- 作成失敗時やレンダリング失敗時の詳細は `XAmeGetLastError()` で取得できます。

## License

このリポジトリ内のソースコードは、別途明記がない限り `MPL-2.0` (Mozilla Public License 2.0) の下で提供します。

- ライセンス本文は [LICENSE](LICENSE) を参照してください。
- このライブラリを変更して再配布する場合、変更した MPL 対象ファイルは MPL-2.0 の条件に従って提供する必要があります。
- このライブラリを利用するアプリケーション全体が直ちに MPL-2.0 になるわけではありません。
- SF2 / DLS 音源、MIDI データ、テスト素材などを別途同梱または配布する場合、それらのライセンスは別途確認してください。
