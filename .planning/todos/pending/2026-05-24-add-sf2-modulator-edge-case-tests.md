---
created: 2026-05-24T00:00:00+09:00
title: Add SF2 modulator edge case tests
area: testing
files:
  - tests/sf2_compliance.cpp:849-5363
---

## Problem

The current SF2 compliance suite covers the default-modulator hierarchy, the resolver refresh paths, and the supported source families that were already implemented.
The remaining unsupported inputs and future source-curve expansions need regression coverage so spec-mode changes do not reopen old compatibility issues.

## Solution

Add targeted tests for unsupported source encodings, unsupported transforms, linked-modulator corner cases, and any new source families as they are implemented.
Keep each test narrowly scoped so legacy mode, spec mode, and unsupported reporting can be checked independently.
