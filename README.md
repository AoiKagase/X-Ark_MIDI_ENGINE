# What is this?
[![Build](https://github.com/AoiKagase/X-Ark_MIDI_ENGINE/actions/workflows/build.yml/badge.svg)](https://github.com/AoiKagase/X-Ark_MIDI_ENGINE/actions/workflows/build.yml)

MIDIファイルとSF2/DLSファイルを読み込んでPCMデータを出力するWindows用DLLです。

# 
ClaudeCodeとCodexを利用して作成しました。ソースは全く読んでも見てもいません。
AI純正です。

---

以下、Codexが追記

## プロジェクト概要

X-ArkMIDIEngine は、MIDI ファイルを SF2/DLS サウンドバンクを用いて PCM 音声へレンダリングする DLL / 共有ライブラリです。

- 入力: MIDI ファイル、SF2 または DLS ファイル
- 出力: PCM オーディオデータ
- 形式: Windows DLL / Linux shared object
- 主用途: ネイティブアプリや .NET アプリからの MIDI レンダリング

## 主な特徴

- C API によるシンプルな利用形態
- UTF-16 / UTF-8 パス対応
- x64 / C++17 ベース
- AVX2 SIMD カーネルを利用したビルド構成
- C# から利用できる P/Invoke ラッパーを同梱

## ビルド

Visual Studio 2022 環境を前提としています。

### DLL をビルド

```powershell
msbuild msvc/XArkMidiEngine.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo /v:minimal
```

### ソリューション全体をビルド

```powershell
msbuild msvc/XArkMidiEngine.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo
```

- 出力先: `build/msvc/bin/Release/XArkMidiEngine.dll`
- ツールセット: v145
- 対応プラットフォーム: x64 のみ

### CMake

```bash
cmake -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
# Output: build/cmake/libXArkMidiEngine.so
```

## ディレクトリ構成

- `include/`
  公開 API ヘッダー
- `src/`
  実装本体
- `tests/`
  テスト用実行ファイルとダンプツール
- `msvc/`
  Visual Studio ソリューション / プロジェクト
- `build/`
  生成物専用
- `XArkMidiEngine.cs`
  C# P/Invoke ラッパー

## API 概要

公開 C API は `include/XArkMidiEngine.h` に定義されています。

- `XAmeCreateEngineFromPaths()`
- `XAmeCreateEngineWithOptions()`
- `XAmeCreateEngineFromPathsUtf8()`
- `XAmeCreateEngineWithOptionsUtf8()`
- `XAmeRender()`
- `XAmeIsFinished()`
- `XAmeDestroyEngine()`

### 注意点

- パスは Windows では `wchar_t*`、Linux では UTF-8 `char*` 推奨
- `numChannels` は `1` または `2`
- デフォルトサンプルレートは `44100Hz`

## C# からの利用

`XArkMidiEngine.cs` に `Engine` クラスがあり、P/Invoke 経由で DLL を利用できます。`RenderAll()` ヘルパーにより、全フレームのレンダリングをまとめて扱えます。

## テスト

### テストアプリのビルド

```powershell
msbuild msvc/Sf2Dump.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /nologo
```

### MIDI レンダリングテスト

```powershell
XArkMidiTest.exe <input.mid> <input.sf2> <output.wav>
```

## SF2 準拠メモ

現時点の SF2 実装では、次を優先して追従しています。

- generator の既定値マージ
- key/velocity 強制
- default modulator の主要系
  - velocity -> initial attenuation
  - velocity -> initial filter cutoff
  - CC1 -> vibrato LFO pitch
- runtime 再評価が必要な modulator source
  - CC
  - poly pressure
  - channel pressure
  - pitch bend
  - pitch wheel sensitivity
- 再評価対象 destination
  - pitch/filter/attenuation/pan
  - volume/mod envelope
  - modulation/vibrato LFO
  - reverb/chorus send
  - tuning/root/exclusive class

補助確認:

- `tests/dump_sf2_zone.cpp` は、読み込んだ SF2 に未対応 modulator がある場合、その件数と未対応 transform 件数を表示します。

残件:

- 一部の modulator destination は未対応です。
- `sfModTransOper` は linear と absolute のみ対応しています。
- CC7/CC10/CC11 の implicit default modulator は既定では無効です。必要なら `XAME_COMPAT_APPLY_SF2_CHANNEL_DEFAULT_MODULATORS` を指定してください。
- SF2 send と MIDI send の最終ミキシング方針は、既定では加算です。
- `XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS` を指定すると、send 合成を乗算へ切り替えられます。

テスト:

- `tests/sf2_compliance.cpp` は synthetic SF2 を使って、forced velocity、default modulator、absolute transform、unsupported transform 集計、send mixing policy を検証します。
- 追加で、`mod env to pitch`、`keynum-to-hold / keynum-to-decay`、poly pressure、channel pressure、pitch wheel sensitivity amount source も検証します。

## 想定ユースケース

- MIDI から WAV 相当の PCM データを生成したい
- SF2/DLS 音源を使ったオフラインレンダリングを行いたい
- C++ または C# アプリへ MIDI 再生エンジンを組み込みたい

## License

このリポジトリ内のソースコードは、別途明記がない限り `MPL-2.0`
(Mozilla Public License 2.0) の下で提供します。

- ライセンス本文は [LICENSE](LICENSE) を参照してください。
- このライブラリを変更して再配布する場合、変更した MPL 対象ファイルは
  MPL-2.0 の条件に従って提供する必要があります。
- このライブラリを利用するアプリケーション全体が、直ちに MPL-2.0 になる
  わけではありません。
- SF2/DLS 音源、MIDI データ、テスト用素材などを別途同梱または配布する場合、
  それらのライセンスは本リポジトリのコードとは別に確認してください。
