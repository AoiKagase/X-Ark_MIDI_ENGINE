---
created: 2026-05-24T00:00:00+09:00
title: Finish SF2 source curve coverage
area: general
files:
  - src/sf2/Sf2ModulatorResolver.cpp:1-220
  - src/sf2/Sf2ModulatorResolver.h:1-130
---

## Problem

The SF2 spec-compliant modulator resolver currently supports only the source shapes and source encodings that were needed for the existing default-modulator and refresh work.
The implemented set is intentionally conservative: CC, velocity, key number, poly pressure, channel pressure, pitch wheel, pitch wheel sensitivity, and link handling are covered, along with linear/concave/convex/switch curves.
Anything outside that subset is still reported as unsupported.

## Solution

Extend the resolver one source family at a time using the SF2 2.04 spec as the only reference.
Keep legacy behavior untouched, preserve unsupported reporting, and add focused tests for each new source curve or source encoding before broadening the accepted set.
