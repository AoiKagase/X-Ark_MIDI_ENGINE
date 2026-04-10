# X-ArkMIDIEngine

Windows DLL that renders MIDI files to PCM audio using SF2/DLS sound banks.

## Build

```powershell
# Build main DLL
msbuild X-ArkMidiEngine/X-ArkMidiEngine.vcxproj /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo /v:minimal

# Build from solution
msbuild X-ArkMidiEngine.sln /p:Configuration=Release /p:Platform=x64 /t:Rebuild /nologo
```

- **Output**: `output/Release/XArkMidiEngine.dll`
- **Toolset**: v145 (VS 2022), C++17, x64 only
- **Dependencies**: AVX2 SIMD kernels compile with `/arch:AVX2`

## Project Structure

- `X-ArkMidiEngine/` - Core C++ DLL
  - `include/` - Public API header
  - `src/` - Implementation (midi/, sf2/, dls/, synth/, common/)
- `XArkMidiEngine.cs` - C# P/Invoke wrapper
- `test/` - Test executables (Sf2Dump, XArkMidiTest)
- `output/`, `intermediate/` - Build artifacts

## Test

```powershell
# Build test app (requires DLL first)
msbuild test/Sf2Dump.vcxproj /p:Configuration=Debug /p:Platform=x64 /t:Rebuild /nologo

# Run MIDI render test
XArkMidiTest.exe <input.mid> <input.sf2> <output.wav>
```

## API

C API in `include/XArkMidiEngine.h`:
- `XAmeCreateEngineFromPaths()` / `XAmeCreateEngineWithOptions()`
- `XAmeRender()` - Render frames to `short[]` buffer
- `XAmeIsFinished()` / `XAmeDestroyEngine()`

C# wrapper: `XArkMidiEngine.cs` with `Engine` class and `RenderAll()` helper.

## Notes

- Paths use UTF-16 wide strings (`wchar_t*`)
- `numChannels` must be 1 or 2
- Default sample rate: 44100Hz
