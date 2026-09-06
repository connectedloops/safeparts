# AGENTS.md — Dormant Tauri reference

## Purpose

Retains the retired Tauri application as dormant reference under approved issue #97.

## Ownership

- Root package, build configuration, and retained assets.
- `src/`: copied React UI and command adapters.
- `src-tauri/`: Rust commands, permissions, and packaging source.

## Local Contracts

- This application is unsupported: no active build, test, packaging, promotion, or web parity obligation remains.
- Retain source and user-owned files. Existing build commands are not supported after exclusion from the root Cargo workspace.
- A replacement or reactivation requires a new decision; no plan or timeline exists.
- Preserve local-only secret handling: no backend, telemetry, sidecar, or automatic persistence of Secrets, Recovery shares, or passphrases. Never log sensitive values.

## Work Guidance

- Supported product work belongs in core, CLI, TUI, WASM, web, and help. Do not copy web changes here to maintain parity.
- See `docs/dev/surfaces/desktop.md` for reference status and recovery pointers.

## Verification

- Default verification excludes this source. Retained tests are reference, not an active gate.

## Child DOX Index

- `src/`: dormant React UI and typed command adapters.
- `src-tauri/`: dormant Tauri command layer, configuration, and permissions.
