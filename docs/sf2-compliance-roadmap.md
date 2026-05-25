# SF2 compliance roadmap

## Documentation checkpoint

- Baseline commit: `c69fe45` (Separate SF2 initial-pitch modulator node from coarse/fine)
- Scope of this checkpoint: document current SF2 compliance state without changing synthesis behavior

## Implemented features

- split default modulator compatibility
- spec-like modulator resolver
- linked modulator chains
- preset/instrument modulator precedence
- unsupported destination rejection
- separate initial pitch summing node
- spec-like Filter Q
- stricter NRPN generator ranges/units
- realtime controller refresh coverage
- public compatibility mode design
- `SF2_RENDER_TUNED` public mode plumbing (currently equivalent to `SF2_SPEC_204` resolver selection)
- golden audio regression tests

## Remaining work

- future `SF2_RENDER_TUNED` rendering adjustments (interpolation, filter response, effect headroom, output-stage behavior) in separate commits

## Compatibility mode note

- `SF2_RENDER_TUNED` uses the SoundFont 2.04 spec-oriented resolver path as its current base.
- It is reserved for X-Ark's independent practical rendering balance and is not Sound Blaster/Audigy/Creative/EMU8000 hardware emulation.

## Validation

- Build target: `sf2_compliance`
- Runtime check: `sf2_compliance: all tests passed`
