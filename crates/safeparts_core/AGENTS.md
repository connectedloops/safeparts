# AGENTS.md — safeparts_core

## Purpose

Owns the core library for secret splitting, combining, packets, encodings, integrity checks, and optional passphrase protection.

## Ownership

- `src/sss.rs`, `src/gf256.rs`: threshold sharing math.
- `src/packet.rs`: versioned share packet parsing and serialization.
- `src/encoding.rs`, `src/ascii.rs`, `src/base58check.rs`, `src/mnemo_*`: share encoding API and implementations. Base58Check arithmetic stays private behind `ascii.rs`.
- `src/crypto.rs`: passphrase protection using KDF and AEAD.
- `src/error.rs`: typed core errors.
- `src/lib.rs`: public API.
- `tests/fixtures/share_compatibility/`: immutable released-version compatibility evidence.
- `tests/fixtures/surface_interoperability/`: immutable synthetic UTF-8 output captured from active and experimental boundaries for cross-surface checks.
- `tests/fixtures/binary_surface_interoperability/`: immutable synthetic arbitrary-byte output captured from CLI, TUI, WASM, and desktop boundaries.

## Local Contracts

- Keep cryptographic and encoding rules here; front-ends adapt IO and presentation only.
- Preserve exact Bitcoin Base58Check framing, alphabet, leading zeroes, SHA-256d checksum, and strict all-content decoding. Pin `ibig` to `0.3.6` with default features disabled; keep `bs58 0.5.1` only as a development differential oracle. The dependency contains reviewed internal unsafe code, while project-owned Rust remains unsafe-forbidden. Fallible reservations cover project-owned attacker-sized vectors and strings; `ibig` limb allocations remain outside recoverable allocation guarantees. The payload-plus-checksum byte buffer is zeroized on normal `Result` exits, but `ibig` limbs, output strings, allocator copies, panic/abort, and process OOM are not guaranteed erased.
- Keep reusable pre-recovery metadata inspection and strict version-preserving parsing for every share encoding here so front ends can enforce admission policy without decoding packets independently or interpolating early.
- Do not log or fixture real secrets, share packets, passphrases, or reconstructed secrets.
- Preserve strict validation and typed errors for malformed input, including empty BIP-39 frames.
- `parse_share_packets_wrapped_mnemonics` accepts complete mnemonic packets per line only when every nonempty line strictly decodes; otherwise it decodes blank-line-separated wrapped packets. Consume all input in either framing.
- Retain decoding for every released Share packet version unless an explicit migration decision changes the policy.
- Treat `tests/fixtures/share_compatibility/` as immutable released evidence: add new versioned fixtures without regenerating expected data.
- Treat `tests/fixtures/surface_interoperability/` and `tests/fixtures/binary_surface_interoperability/` as captured boundary evidence. Verify their hashes and provenance; add a new fixture set instead of regenerating files in place.
- Workspace lint policy forbids `unsafe`; do not weaken it.

## Work Guidance

- Follow `docs/agents/conventions.md` and `docs/dev/surfaces/core.md`.
- Add deterministic round-trip tests and negative tests for behavior changes.
- Keep public API changes explicit and update downstream surfaces when needed.

## Verification

- `cargo test -p safeparts_core`
- `cargo test -p safeparts_core --test share_compatibility`
- `cargo test --all-features`
- `cargo fmt --all -- --check`
- `cargo clippy --all-targets --all-features -- -D warnings`

## Child DOX Index

- No child AGENTS.md files yet.
