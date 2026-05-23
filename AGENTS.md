# X-ArkMIDIEngine

Shared library that renders MIDI files to PCM audio using SF2/DLS sound banks.
Supports Windows (DLL) and Linux (shared object).

## Build

### Windows (MSVC)

```powershell
# Build main DLL
msbuild msvc/XArkMidiEngine.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo /v:minimal

# Build from solution
msbuild msvc/XArkMidiEngine.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo
```

- **Output**: `build/msvc/bin/Release/XArkMidiEngine.dll`
- **Toolset**: v145 (VS 2022), C++17, x64 only
- **Dependencies**: AVX2 SIMD kernels compile with `/arch:AVX2`

### Linux / macOS (CMake + GCC/Clang)

```bash
cmake -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake
# Output: build/cmake/libXArkMidiEngine.so
```

Requirements: GCC 9+ or Clang 10+, C++17, x86-64 (AVX2 optional)

## Project Structure

- `include/` - Public API header
- `src/` - Implementation (midi/, midi2/, sf2/, dls/, synth/, common/)
- `tests/` - Test executables and dump tools
- `msvc/` - Visual Studio solution/project files
- `build/` - Generated artifacts only
- `XArkMidiEngine.cs` - C# P/Invoke wrapper

## Test

```powershell
# Build test app (requires DLL first)
msbuild msvc/Sf2Dump.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /nologo

# Run MIDI render test
XArkMidiTest.exe <input.mid> <input.sf2> <output.wav>
```

## API

C API in `include/XArkMidiEngine.h`:
- `XAmeCreateEngineFromPaths()` / `XAmeCreateEngineWithOptions()` — `wchar_t*` paths (Windows推奨)
- `XAmeCreateEngineFromPathsUtf8()` / `XAmeCreateEngineWithOptionsUtf8()` — UTF-8 `char*` paths (Linux推奨、全プラットフォーム対応)
- `XAmeRender()` - Render frames to `short[]` buffer
- `XAmeIsFinished()` / `XAmeDestroyEngine()`

C# wrapper: `XArkMidiEngine.cs` with `Engine` class and `RenderAll()` helper.

## Notes

- Paths: Windows は `wchar_t*` (UTF-16)、Linux は `char*` UTF-8 API を推奨
- `numChannels` must be 1 or 2
- Default sample rate: 44100Hz
- AVX2 が利用可能な場合は自動的に有効化される（ランタイム検出）