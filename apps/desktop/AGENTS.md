# AGENTS.md — Desktop UI prototype

## Purpose

Owns the throwaway native desktop UI prototype used for the issue 140 visual checkpoint.

## Ownership

- `prototype/`: fixture-only Qt Widgets UI and its local build/run files.

## Local Contracts

- Keep the prototype visibly labeled as simulated and unsafe for real secrets.
- Use only fixed synthetic fixtures. Do not connect it to Rust, cryptography, persistence, networking, printing, or production packaging.
- Treat this code as a design artifact. Production desktop work must replace it and follow the separately approved runtime and core-adapter contracts.

## Work Guidance

- Keep the implementation direct and limited to the single-window Create/Recover flow.
- Keep generated builds and captures under `prototype/build/`.

## Verification

- Build and run capture mode with `mise run desktop:prototype -- --capture apps/desktop/prototype/build/captures`.
- Confirm all four PNG fixtures exist, then run `mise run dx:verify` and `git diff --check`.

## Child DOX Index

- No child AGENTS.md files.
