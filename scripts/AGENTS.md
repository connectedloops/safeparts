# AGENTS.md — Scripts

## Purpose

Owns repository automation scripts.

## Ownership

- `dev/`: local developer diagnostics and DX verification helpers.
- `release/`: local CLI/TUI archive packaging and documentation of platform release ownership.

## Local Contracts

- Scripts must be deterministic, explicit about inputs/outputs, and avoid embedding secrets.
- Release packaging behavior must match `scripts/release/README.md`: CLI/TUI archives for Linux, Windows, and both macOS architectures. Retired GUI applications and their UniFFI bridge are outside active setup, verification, dependency policy, and future releases; preserve their implementation source as dormant reference.

## Work Guidance

- Prefer small, auditable scripts with clear error messages.
- Preserve executable behavior and documented command examples when editing packaging scripts.

## Verification

- Run the script-specific documented command when practical.
- Run `mise run dx:verify` after changing developer automation checks.

## Child DOX Index

- `dev/`: developer diagnostics and DX verification scripts.
- `release/`: release archive packaging script and README.
