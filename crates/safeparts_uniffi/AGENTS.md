# AGENTS.md — Dormant UniFFI bridge

## Purpose

Retains the dedicated UniFFI bridge for retired SwiftUI and WinUI applications as dormant reference under approved issue #97.

## Ownership

- Retained Rust facade, binding-generator configuration, and bridge tests in this directory.

## Local Contracts

- This bridge is excluded from the active Cargo workspace and supported builds, tests, coverage, dependency policy, and release packaging.
- Retain source and user-owned files. Existing native build and binding-generation commands are not supported after workspace exclusion.
- There is no binding refresh or native parity obligation for supported core changes.
- A replacement or reactivation requires a new decision; no plan or timeline exists.
- Preserve core-owned cryptography, sanitized boundary errors, and sensitive-data safeguards. Never log or fixture real Secrets, Recovery shares, or passphrases.

## Work Guidance

- Supported browser bindings live in `crates/safeparts_wasm/`; supported algorithms live in `crates/safeparts_core/`.
- Follow `docs/agents/conventions.md`; retirement does not weaken repository safety standards.

## Verification

- Retained bridge tests are dormant reference, not active verification gates.

## Child DOX Index

- No child AGENTS.md files.
