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
- golden audio regression tests

## Remaining work

- none (checkpoint scope complete)

## Validation

- Build target: `sf2_compliance`
- Runtime check: `sf2_compliance: all tests passed`
