# SF2_RENDER_TUNED Benchmark Harness

This document defines the Phase 1 benchmark harness for `SF2_RENDER_TUNED`.

## Scope

- Stabilize command-line benchmark flow for repeated A/B runs.
- Compare `XAME_COMPAT_MODE_SF2_SPEC_204` vs `XAME_COMPAT_MODE_SF2_RENDER_TUNED`.
- Support dry-first analysis (`InternalEffect OFF`) and wet follow-up.
- Export simple comparison logs and clipping analysis logs.

This harness does not change DSP behavior.

## Benchmark Inputs

- SF2: `FluidR3_GM2-2.SF2`
- MIDI: `fighting-the-spirit.mid`
- Channel focus: Ch2 (`--solo 2`)
- Output sample rate: 44100 Hz (default)
- Output channels: stereo (default)

Program 81 (Saw Wave in GM) is benchmark material only. No program-specific branching is allowed in renderer logic.

## Build Required Tools

```bat
rtk cmake --build build/cmake --target XArkMidiTest compare_wav_diff analyze_wav_clipping
```

## One-command Harness Run

```bat
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools/sf2_render_benchmark.ps1 ^
  -MidiPath "path\to\fighting-the-spirit.mid" ^
  -SoundFontPath "path\to\FluidR3_GM2-2.SF2" ^
  -OutputDir "build\bench\sf2_render_tuned" ^
  -SoloChannel 2
```

Timidity baseline run:

```bat
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools/sf2_render_benchmark.ps1 ^
  -MidiPath "H:\musix\fighting-the-spirit.mid" ^
  -SoundFontPath "H:\musix\FluidR3_GM2-2.SF2" ^
  -OutputDir "build\bench\sf2_render_tuned_timidity" ^
  -SoloChannel 2 ^
  -ReferenceWavPath "H:\musix\fighting-the-spirit_timidity.wav" ^
  -ReferenceLabel "timidity"
```

Dry-only run:

```bat
rtk powershell -NoProfile -ExecutionPolicy Bypass -File tools/sf2_render_benchmark.ps1 ^
  -MidiPath "path\to\fighting-the-spirit.mid" ^
  -SoundFontPath "path\to\FluidR3_GM2-2.SF2" ^
  -OutputDir "build\bench\sf2_render_tuned_dry" ^
  -SoloChannel 2 ^
  -DryOnly
```

## Output Artifacts

The script writes per-case outputs under `<OutputDir>\dry` and `<OutputDir>\wet`:

- `spec_204.wav`
- `render_tuned.wav`
- `render_spec.log`
- `render_tuned.log`
- `compare_spec_vs_tuned.log` (`compare_wav_diff`)
- `compare_spec_vs_<reference>.log` (`compare_wav_diff`, optional)
- `compare_tuned_vs_<reference>.log` (`compare_wav_diff`, optional)
- `clip_spec.log` (`analyze_wav_clipping`)
- `clip_tuned.log` (`analyze_wav_clipping`)
- `summary.txt`

## Notes

- `XArkMidiTest` now supports `--compat-mode` for deterministic mode selection.
- `XArkMidiTest` now supports `--output-stage` (`standard`, `enhanced-loud`, `enhanced-natural`, `enhanced-warm`).
- Dry run uses `--disable-internal-effects` to isolate source rendering behavior first.
- Wet run keeps internal effects enabled for follow-up balance checks.
- `-ReferenceWavPath` is optional and enables direct `spec/tuned vs reference` comparison in one run.
- `-SpecOutputStage` / `-TunedOutputStage` can be used to A/B output-stage thickness against reference renders.
