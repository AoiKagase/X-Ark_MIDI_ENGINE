# SF2_RENDER_TUNED Roadmap

## 1. Purpose

`SF2_RENDER_TUNED` is an independent tuned rendering mode for SF2 playback.

It should:

- use `SF2_SPEC_204` as its base
- preserve SoundFont 2.04 specification-oriented resolution
- allow future independent tuning of rendering behavior
- improve practical playback balance where possible
- avoid claiming compatibility with specific hardware

It is not:

- Sound Blaster emulation
- Audigy emulation
- Creative compatibility mode
- EMU8000 compatibility mode
- a program-specific hack layer
- a replacement for `SF2_SPEC_204`

At the moment, `SF2_RENDER_TUNED` may behave the same as `SF2_SPEC_204`.

## 2. Non-goals

- Do not emulate specific hardware.
- Do not add program-number-specific corrections.
- Do not add SF2-file-specific corrections.
- Do not change `SF2_SPEC_204` to make a single benchmark sound closer.
- Do not use external synthesizer implementation code.
- Do not tune by hardcoding GM program names or numbers.
- Do not collapse or normalize ProgramLayer voices as a tuning shortcut.

## 3. Benchmark Material

The first benchmark case is:

- SF2: `FluidR3_GM2-2.SF2`
- MIDI: `fighting-the-spirit.mid`
- Channel: Ch2
- Program: 81 / Saw Wave
- Test mode: Ch2 solo
- Compare outputs from:
- X-Ark `SF2_SPEC_204`
- X-Ark `SF2_RENDER_TUNED`
- SoundBlaster/Audigy recording
- FluidSynth output
- BASSMIDI output
- TiMidity++ output

Important:

Program 81 is a benchmark only. It must not become a special-case branch.

## 4. First Diagnostic Priority

Always compare dry output first.

### Step 1

Use:

- InternalEffect OFF
- Ch2 solo
- RMS-normalized comparison
- same SF2
- same MIDI
- same sample rate

### Step 2

Only after dry output is closer, compare:

- InternalEffect ON
- chorus thickness
- reverb tail
- effect clipping
- wet/dry balance

## 5. Hypotheses

The current quality gap may come from one or more of these areas.

### 5.1 Sample interpolation / resampler

Potential issue:

- short saw loops may sound thin, sharp, or rough
- high sample step may expose interpolation artifacts
- aliasing may create 8-16 kHz harshness

Possible future experiments:

- current interpolation
- cubic interpolation
- 4-point Lagrange interpolation
- optional anti-alias lowpass
- optional oversampling test

### 5.2 Short loop handling

Potential issue:

- very short looped waveforms are sensitive to loop boundary interpolation
- loop end must not be played as a normal loop sample
- interpolation across loop boundaries must be continuous

Diagnostic data:

- loop start
- loop end
- loop length
- sample step
- root key
- pitch correction

### 5.3 Filter response

Potential issue:

- `SF2_SPEC_204` filter Q may be specification-oriented but not tuned for practical renderer balance
- overly sharp top-end may remain
- 1-8 kHz body may be weak
- 8-16 kHz roughness may be too prominent

Possible future experiments:

- Q scale variants
- cutoff curve variants
- high-frequency smoothing
- DC gain compensation variants

Do not change `SF2_SPEC_204` filter behavior.

### 5.4 Internal effects headroom

Potential issue:

- resolver-correct send values may drive InternalEffect hotter than before
- chorus/reverb feedback paths may clip
- effect headroom may need tuning

Existing work:

- keep the InternalEffect clipping mitigation already implemented
- do not remove current tank input stabilization

Possible future experiments:

- reverb tank headroom
- chorus tank headroom
- effect input shaping
- send scaling in `SF2_RENDER_TUNED` only

### 5.5 Output stage

Potential issue:

- final output may be too sharp or too thin
- simple clipping or excessive limiting should be avoided

Possible future experiments:

- gentle output shaping
- headroom policy
- peak protection
- no aggressive compression

## 6. Diagnostic Logging TODO

Add optional diagnostics later, not in this commit.

Useful per-NoteOn data:

- bank
- program
- key
- velocity
- zone count
- sample name
- loop start
- loop end
- loop length
- sample step
- coarse tune
- fine tune
- initial pitch add cents
- filter cutoff
- filter Q
- reverb send
- chorus send
- pan
- layer relative gain
- detected short-loop risk
- detected aliasing risk

Diagnostics must not depend on Program 81.

## 7. Tuning Rule

Future `SF2_RENDER_TUNED` tuning must be property-based.

Allowed examples:

- if loop length is very short and sample step is high, use a safer interpolation path
- if filter Q is high and cutoff is near a sensitive range, apply tuned filter behavior in render-tuned mode
- if effect send is very high, use render-tuned effect headroom

Disallowed examples:

- if program == 81
- if GM name == Saw Wave
- if SF2 file name == FluidR3_GM2-2.SF2
- if sample name contains Saw, then apply fixed EQ
- if MIDI file name matches fighting-the-spirit.mid

## 8. Test Strategy

### Existing tests

Keep:

- `sf2_compliance`
- SF2 golden audio regression checkpoints
- modulator resolver tests
- NRPN tests
- Filter Q tests
- Initial Pitch tests

### Future tests

Add later:

- `SF2_RENDER_TUNED` resolver selection tests
- short-loop diagnostic tests
- interpolation mode tests
- effect headroom tests
- output stage tests
- benchmark audio signature tests

Golden audio should not require exact waveform identity if DSP tuning changes are intentional.
Prefer signatures and metrics:

- RMS
- peak
- spectral centroid
- 1-8 kHz energy ratio
- 8-16 kHz energy ratio
- L/R correlation
- clipping count
- selected tap samples

## 9. Suggested Phased Work

### Phase 0: Documentation only

- Add this roadmap.
- Add TODO entries.
- Do not change DSP behavior.

### Phase 1: Benchmark harness

- Stabilize benchmark files and command line.
- Add optional metrics extraction.
- Compare `SF2_SPEC_204` vs reference outputs.

### Phase 2: Dry output analysis

- InternalEffect OFF.
- Compare Pg81 benchmark.
- Identify whether the biggest gap is interpolation, filter, layer balance, or output stage.

### Phase 3: Resampler experiments

- Add render-tuned interpolation option internally.
- Do not affect `SF2_SPEC_204`.
- A/B compare short-loop saw material.

### Phase 4: Filter experiments

- Add render-tuned filter response option internally.
- Do not affect `SF2_SPEC_204`.
- Compare 1-8 kHz body and 8-16 kHz roughness.

### Phase 5: InternalEffect experiments

- Only after dry output is close.
- Adjust effect headroom if needed.
- Avoid reducing chorus thickness too much.

### Phase 6: Output stage experiments

- Add gentle protection if required.
- Avoid aggressive compression.
- Keep `SF2_SPEC_204` unchanged.

## 10. AGENTS.md Reminder

When SF2_RENDER_TUNED implementation starts, keep these constraints in `AGENTS.md` aligned:

- Do not copy, translate, or port external synthesizer code.
- Do not add program-number-specific tuning.
- Do not claim Sound Blaster/Audigy/Creative/EMU compatibility.
- Keep `SF2_SPEC_204` unchanged unless explicitly requested.
- Keep `SF2_RENDER_TUNED` changes isolated.
- Run `sf2_compliance` after SF2-related changes.
- Prefer small commits.
