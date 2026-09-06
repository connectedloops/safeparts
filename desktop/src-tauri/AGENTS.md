# AGENTS.md — Dormant Tauri command layer

## Purpose

Owns the dormant Rust command layer and packaging configuration for the retired Tauri application.

## Ownership

- `src/`: retained commands and tests.
- `capabilities/`, `tauri.conf.json`: permissions and application configuration.
- `gen/schemas/`, `icons/`: retained generated schemas and assets.

## Local Contracts

- This crate is excluded from the active Cargo workspace and supported builds, tests, and packages. Existing native build commands are not supported.
- Preserve the reference's use of core public APIs, sanitized parse errors, and explicit valid-UTF-8 metadata with lossless optional text.
- Preserve local-only operation without backend, telemetry, sidecars, or automatic persistence of Secrets, Recovery shares, reconstructed bytes, or passphrases.
- Treat schemas as generated reference; leave them unchanged during supported-surface work.

## Work Guidance

- See `docs/dev/surfaces/desktop.md` and `docs/agents/conventions.md`.

## Verification

- Retained command and packaging tests are dormant reference, not active gates.

## Child DOX Index

- No child AGENTS.md files.
