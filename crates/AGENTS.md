# AGENTS.md — Rust Workspace Crates

## Purpose

Owns active Rust workspace members for core, CLI, TUI, and WASM, plus the dormant UniFFI reference source.

## Ownership

- `safeparts_core/`: security-sensitive algorithms, packet formats, encodings, optional encryption.
- `safeparts/`: script-friendly CLI binary and CLI integration tests.
- `safeparts_tui/`: terminal UI binary and interaction/domain state.
- `safeparts_wasm/`: wasm-bindgen facade consumed by `web/`.
- `safeparts_uniffi/`: retired native-app bridge retained as dormant reference, excluded from the active Cargo workspace.

## Local Contracts

- Keep shared secret-handling logic in `safeparts_core`; front-ends should adapt IO and presentation only.
- Treat share packets, passphrases, and reconstructed secrets as sensitive. Do not log or fixture real values.
- Workspace lints forbid `unsafe`; do not weaken lint policy.
- Supported builds, coverage, dependency policy, and releases use only core, CLI, TUI, and WASM crates. The UniFFI and Tauri manifests are explicitly excluded; restoring them may require manifest/workspace repair. No standalone buildability or parity is promised.

## Work Guidance

- Follow `docs/agents/conventions.md` for Rust style, errors, testing, CLI flags, and security-sensitive code.
- Prefer typed errors in core and user-facing error mapping in CLI/TUI/WASM boundaries.
- Add deterministic round-trip and negative tests for packet, encoding, crypto, and threshold behavior changes.

## Verification

- Format: `cargo fmt --all -- --check`
- Lint: `cargo clippy --all-targets --all-features -- -D warnings`
- Test: `cargo test --all-features`
- Targeted examples: `cargo test -p safeparts_core <pattern>`, `cargo test -p safeparts --test e2e <pattern>`

## Child DOX Index

- `safeparts_core/`: core library internals and public API.
- `safeparts/`: CLI binary and e2e tests.
- `safeparts_tui/`: terminal UI binary.
- `safeparts_wasm/`: browser/WASM binding layer.
- `safeparts_uniffi/`: dormant UniFFI reference source and its local retirement contract.
