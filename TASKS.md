# SF2 Spec Follow-up Tasks

- [x] Implement true SoundFont 2.04 `sm24` playback by combining `smpl` + `sm24` into a 24-bit sample path.
- [x] Implement ROM sample playback instead of skipping ROM-backed zones.
- [ ] Validate `INFO` string chunks more strictly: terminators, basic ASCII fidelity, and ROM metadata handling.
- [ ] Validate `sdta` trailing zero padding and loop-neighborhood data requirements from the spec.
