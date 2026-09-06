# AGENTS.md — Dormant SwiftUI reference

## Purpose

Retains the retired native macOS application as dormant reference under approved issue #97.

## Ownership

- `Sources/`, `Tests/`: SwiftUI application, model, bridge integration, and tests.
- `Generated/`: retained Swift, header, and module-map outputs.
- `Release/`, `scripts/`: retained bundle metadata, preparation, and packaging scripts.

## Local Contracts

- This application and its dedicated UniFFI bridge are unsupported: no active build, test, packaging, promotion, or parity obligation remains.
- Retain source and user-owned files. Existing native build commands are not supported after root workspace exclusion.
- A replacement or reactivation requires a new decision; no plan or timeline exists.
- Preserve secret-handling safeguards: core-owned cryptography, memory-only state, byte-accurate file IO, explicit clipboard actions, stale-result invalidation, and errors without sensitive contents.
- Leave generated bindings and compiled output untouched during supported-surface work; never commit libraries, app bundles, or DMGs.

## Work Guidance

- See `README.md` and `docs/dev/surfaces/macos.md` for reference status. CLI/TUI support on macOS continues.

## Verification

- Retained Swift, bridge, and package tests are dormant reference, not active gates.

## Child DOX Index

- No child AGENTS.md files.
