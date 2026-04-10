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

## 想定ユースケース

- MIDI から WAV 相当の PCM データを生成したい
- SF2/DLS 音源を使ったオフラインレンダリングを行いたい
- C++ または C# アプリへ MIDI 再生エンジンを組み込みたい
