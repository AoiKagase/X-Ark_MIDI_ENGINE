# Tasks

## Session Handoff

### Completed In Recent Sessions

- [x] Loader and structural validation hardening landed in `ca996ce` (`Enforce SF2 loader structural validation`).
- [x] Zone and generator resolution hardening landed in `00db349` (`Enforce SF2 zone and generator resolution rules`).
- [x] Modulator deduplication and linked-destination chaining landed in `0611f06` (`Implement SF2 modulator chaining and deduplication`).
- [x] Remaining SF2 default modulators and supersede/add compliance tests landed in `86bb4c8` (`Implement remaining SF2 default modulators`).
- [x] SF2 stereo sample link parsing/playback and compliance coverage landed in `7bc22d5` (`Implement SF2 stereo sample link playback`).
- [x] SF2 synth-side pan/send/pitch precedence is now aligned in the working tree, with updated compliance coverage for channel-mix and pitch ownership.
- [x] Loader-side validation for terminal `Instrument` / `sampleID` references, ROM sample rejection, and `sm24` version/size checks is now aligned in the working tree.
- [x] Loader now rejects inert ROM metadata (`irom` / `iver` without ROM-backed sample headers) and malformed truncated `INFO` / `sdta` subchunks, with compliance coverage.
- [x] Unsupported SF2 modulator amount sources are now counted and ignored during zone resolution instead of falling back to full-scale application.
- [x] Loader now enforces mandatory `isng` / `INAM` / `smpl` chunk presence in addition to `ifil`, with compliance coverage for missing-chunk rejection.
- [x] Loader now rejects duplicate singleton `INFO` / `sdta` subchunks such as repeated `ifil`, `irom`, `iver`, `smpl`, or `sm24` instead of silently accepting later duplicates.
- [x] Loader now rejects duplicate top-level `INFO` / `sdta` / `pdta` LIST chunks instead of concatenating multiple sections from malformed banks.
- [x] Public API docs now pin `XAME_COMPAT_MULTIPLY_SF2_MIDI_EFFECTS_SENDS` as an explicit non-spec legacy mode, while SF2 pan stays on the spec path with no separate compat toggle.
- [x] Deferred fidelity audit now has direct compliance coverage for envelope release recomputation and filter/LFO initialization paths.

### Recommended Next Start Point

- [x] No non-deferred follow-up remains in the current SF2 compliance pass.

### Notes For Next Session

- `Sf2Compliance` currently passes after the uncommitted loader validation follow-up.
- Loader now rejects paired `irom` / `iver` when no ROM-backed sample headers exist, so SF2 ROM metadata is treated as spec-significant rather than inert.
- Keep untracked spec/task artifacts out of commits unless explicitly requested:
- `sfspec24.pdf`
- `sfspec24.txt`
- `tools/XArkMidiGuiPlayer/XArkMidiGuiPlayer.sln`

## Must Fix For Spec Compliance

### Loader And Structural Validation

- [x] Enforce mandatory `INFO` validation for SF2 loads. Reject banks missing or corrupt mandatory `ifil` instead of ignoring the entire `INFO` list.
- [x] Enforce presence of the remaining mandatory `INFO` / `sdta` chunks (`isng`, `INAM`, `smpl`) instead of accepting partial metadata lists.
- [x] Reject duplicate singleton `INFO` / `sdta` subchunks instead of silently overwriting earlier `ifil` / `isng` / `INAM` / `irom` / `iver` / `smpl` / `sm24` data.
- [x] Reject duplicate top-level `INFO` / `sdta` / `pdta` LIST chunks instead of merging multiple metadata/data sections into one load.
- [x] Add strict Hydra structural validation. Reject non-monotonic or size-mismatched `phdr/pbag/pmod/pgen/inst/ibag/imod/igen/shdr` indices instead of clamping malformed data through.
- [x] Enforce terminal record consistency for preset, instrument, bag, generator, modulator, and sample tables per `sfspec24`.
- [x] Reject structurally unsound `Instrument` / `sampleID` references that point at or beyond terminal records.
- [x] Revisit ROM sample handling against `irom` / `iver` requirements and reject/terminate ROM-backed playback as required by the spec.
- [x] Reject inert ROM metadata (`irom` / `iver`) when no ROM-backed sample headers are present.
- [x] Validate `sm24` handling against `ifil` version and chunk-size rules, not just presence detection.
- [x] Reject truncated or size-inconsistent `INFO` / `sdta` subchunks instead of clamping malformed sizes through parsing.

### Zone And Generator Resolution

- [x] Enforce zone terminal-generator rules. Non-global preset zones must end in `Instrument`; non-global instrument zones must end in `sampleID`; generators after those terminals must be ignored.
- [x] Ignore illegal preset-level sample generators. `sampleModes`, `overridingRootKey`, `exclusiveClass`, and sample address offset generators must not affect preset zones.
- [x] Implement duplicate generator handling per zone. When the same `sfGenOper` appears twice, keep the later one and ignore the earlier one.

### Modulator Resolution

- [x] Implement duplicate modulator handling per zone. When source + destination + amount-source + transform are identical, keep the later modulator and ignore the earlier one.
- [x] Implement linked modulator destination support (`sfModDestOper` top-bit chaining), including invalid-link and circular-link rejection.
- [x] Implement missing instrument-level default modulators from the SF2 spec:
- [x] `Channel Pressure -> VibLFO pitch depth`
- [x] `CC7 -> InitialAttenuation`
- [x] `CC10 -> Pan`
- [x] `CC11 -> InitialAttenuation`
- [x] `CC91 -> ReverbEffectsSend`
- [x] `CC93 -> ChorusEffectsSend`
- [x] `Pitch Wheel -> InitialPitch` with `Pitch Wheel Sensitivity` as amount source
- [x] Verify channel pressure / `CC1` interaction with `VibLfoToPitch` when explicit `IMOD` / `PMOD` entries supersede default modulators.

### Playback Semantics

- [x] Preserve and validate `wSampleLink` in parsed sample headers so stereo/link semantics are available to the synth.
- [x] Implement stereo sample-pair behavior from `wSampleLink` / `sfSampleType` for left/right samples, including synchronized playback and pitch ownership rules.
- [x] Rework channel pan handling to match SoundFont modulator semantics instead of applying an engine-side post-pan layer.
- [x] Rework channel reverb/chorus send handling to follow SoundFont default-modulator behavior instead of mixer-side additive/multiplicative policy.
- [x] Verify pitch-bend behavior against SF2 modulator precedence so engine-side bend application does not diverge from modulator supersede/add rules.

### Verification

- [x] Add compliance tests for missing default modulators and modulator supersede/add precedence.
- [x] Add compliance tests for stereo sample links.
- [x] Add compliance tests for SF2 channel-mix and pitch precedence against resolved modulator output.
- [x] Add compliance tests for linked modulators, illegal preset sample generators, and terminal-generator rules.
- [x] Add compliance tests for invalid terminal references, ROM sample rejection, and `sm24` version/size validation.
- [x] Add compliance tests for inert ROM metadata rejection and truncated `smpl` chunk rejection.
- [x] Add compliance tests proving unsupported modulator amount sources are reported and ignored.
- [x] Add compliance tests for missing mandatory `isng`, `INAM`, and `smpl` chunk rejection.
- [x] Add compliance tests for duplicate mandatory `ifil` and `smpl` chunk rejection.
- [x] Add compliance tests for duplicate top-level `INFO`, `sdta`, and `pdta` LIST rejection.
- [x] Add compliance tests for envelope release-rate recomputation and filter/LFO initialization state.

## Acceptable As Compat Option Only

- [x] Unsupported modulator source / amount-source / transform definitions are counted for diagnostics and ignored during resolution instead of being coerced into active modulation.
- [x] `multiplySf2MidiEffectsSends` remains exposed only as an explicit non-spec legacy compatibility mode and is documented as such in the public API.
- [x] No non-spec SF2 mixer-side pan compatibility mode remains exposed; the SF2 path keeps spec-driven pan semantics only.

## Can Be Deferred

- [x] Re-check envelope timing and shape accuracy against `sfspec24`, with direct compliance coverage for sustain/decay scaling and release-rate recomputation in vol/mod envelopes.
- [x] Audit broader SF2 synth-model fidelity beyond current findings, with direct compliance coverage for filter/LFO initialization and interaction entry points.
