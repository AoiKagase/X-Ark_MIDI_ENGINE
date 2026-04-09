# What is this?
MIDIファイルとSF2/DLSファイルを読み込んでPCMデータを出力するWindows用DLLです。

# 
ClaudeCodeとCodexを利用して作成しました。ソースは全く読んでも見てもいません。
AI純正です。

---

以下、Codexが追記

## プロジェクト概要

X-ArkMIDIEngine は、MIDI ファイルを SF2/DLS サウンドバンクを用いて PCM 音声へレンダリングする Windows 向け DLL です。

- 入力: MIDI ファイル、SF2 または DLS ファイル
- 出力: PCM オーディオデータ
- 形式: Windows DLL
- 主用途: ネイティブアプリや .NET アプリからの MIDI レンダリング

## 主な特徴

- C API によるシンプルな利用形態
- UTF-16 パス対応
- x64 / C++17 ベース
- AVX2 SIMD カーネルを利用したビルド構成
- C# から利用できる P/Invoke ラッパーを同梱

## ビルド

Visual Studio 2022 環境を前提としています。

### DLL をビルド

```powershell
msbuild X-ArkMidiEngine/X-ArkMidiEngine.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo /v:minimal
```

### ソリューション全体をビルド

```powershell
msbuild X-ArkMidiEngine.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo
```

- 出力先: `output/Release/XArkMidiEngine.dll`
- ツールセット: v145
- 対応プラットフォーム: x64 のみ

## ディレクトリ構成

- `X-ArkMidiEngine/`
  コア DLL プロジェクト
- `X-ArkMidiEngine/include/`
  公開 API ヘッダー
- `X-ArkMidiEngine/src/`
  実装本体
- `XArkMidiEngine.cs`
  C# P/Invoke ラッパー
- `test/`
  テスト用実行ファイルや検証コード
- `output/`
  ビルド成果物
- `intermediate/`
  中間生成物

## API 概要

公開 C API は `include/XArkMidiEngine.h` に定義されています。

- `XAmeCreateEngineFromPaths()`
- `XAmeCreateEngineWithOptions()`
- `XAmeRender()`
- `XAmeIsFinished()`
- `XAmeDestroyEngine()`

### 注意点

- パスは `wchar_t*` を用いた UTF-16 形式
- `numChannels` は `1` または `2`
- デフォルトサンプルレートは `44100Hz`

## C# からの利用

`XArkMidiEngine.cs` に `Engine` クラスがあり、P/Invoke 経由で DLL を利用できます。`RenderAll()` ヘルパーにより、全フレームのレンダリングをまとめて扱えます。

## テスト

### テストアプリのビルド

```powershell
msbuild test/Sf2Dump.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /nologo
```

### MIDI レンダリングテスト

```powershell
XArkMidiTest.exe <input.mid> <input.sf2> <output.wav>
```

## 想定ユースケース

- MIDI から WAV 相当の PCM データを生成したい
- SF2/DLS 音源を使ったオフラインレンダリングを行いたい
- C++ または C# アプリへ MIDI 再生エンジンを組み込みたい
