# AGENTS.md — Desktop CXX adapter

## Purpose

Owns the generated CXX seam between the experimental Qt desktop app and `safeparts_core`.

## Ownership

- `src/bridge.rs`: declarations-only CXX interface and the sole generated-glue lint exception.
- `src/operation.rs`: safe Rust operation state, admission, core calls, ownership, and typed status mapping.
- `tests/`: public Rust operation checks.
- `scripts/policy_guard.sh`: deterministic generated-glue exception guard.

## Local Contracts

- Pin the CXX family to `1.0.195`; do not add `build.rs` or handwritten unsafe code.
- Keep `src/bridge.rs` declarations-only and pinned by the policy guard. Keep operation logic under `#![forbid(unsafe_code)]`.
- Rust owns share packets, retained recovery input, and authoritative recovered bytes. CXX values are bounded copies with explicit owners. The generated boundary exposes explicit exact-byte recovery for compatibility tests; the Qt text workflow uses the UTF-8-gated recovery entry.
- Catch unwinds at every CXX operation entry and return content-free typed statuses. Never log secret or Recovery share content.
- Delegate packet grammar, version-preserving Auto/manual encoding detection, validation, interpolation, and integrity checks to `safeparts_core`; expose only bounded encoding/protection/count/status metadata.

## Work Guidance

- Add behavior through the existing operation interface. Do not expose packet layouts or create a second decoder.
- Run admission checks before allocation-heavy parsing and preserve accepted recovery batches after handled errors.
- Account with checked arithmetic for current operation-owned state and the next recovery phase: raw batches, decoded packets, incoming bridge/Qt copies, parser workspace, and fixed UI/runtime headroom. Preserve the published logical limits.

## Verification

- `mise exec -- cargo test -p safeparts_desktop_bridge`
- `crates/safeparts_desktop_bridge/scripts/policy_guard.sh`
- `mise exec -- cargo clippy -p safeparts_desktop_bridge --all-targets --all-features -- -D warnings`
- `ctest --test-dir target/desktop-build --output-on-failure -R desktop-bridge-contract`

## Child DOX Index

- No child AGENTS.md files.
