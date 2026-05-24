---
created: 2026-05-24T00:00:00+09:00
title: Complete linked modulator edge cases
area: general
files:
  - src/sf2/Sf2ModulatorResolver.cpp:220-520
  - src/sf2/Sf2File.cpp:1840-1868
---

## Problem

Linked modulator handling is implemented for the safe, spec-aligned cases we already needed, including cycle detection and invalid-link suppression.
The remaining linked-modulator space still needs a deliberate pass so nested linked inputs, unsupported link shapes, and suppression semantics stay consistent with the default-modulator hierarchy.

## Solution

Audit linked modulator resolution against the SF2 2.04 rules and add the missing cases incrementally.
Keep cycle detection, invalid-link rejection, and default-suppression behavior stable while filling in only the remaining supported link patterns.
