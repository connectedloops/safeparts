# Product Requirements Document (PRD)

## Title

**Safeparts** - Threshold secret sharing toolkit (Rust core + CLI + TUI + Web UI + Help site)

---

## 1. Purpose & Scope

Safeparts is a cross-platform tool and library for splitting arbitrary secrets into
threshold-protected recovery shares (k-of-n). Shares are stored separately and can
be combined to reconstruct the original secret.

The project ships as a single repo with multiple deliverables:

- Rust core library: `crates/safeparts_core/`
- CLI: `crates/safeparts/` (`safeparts`)
- TUI: `crates/safeparts_tui/` (`safeparts-tui`) + a `safeparts tui` launcher
- WASM bindings for the browser: `crates/safeparts_wasm/`
- Web UI (Vite + React): `web/`
- Help website (Astro Starlight, i18n): `web/help/`

CLI/TUI archives support Linux, macOS, and Windows. Tauri, native SwiftUI macOS,
native WinUI Windows, and their dedicated UniFFI bridge are retired. Their source
remains dormant reference outside supported builds, tests, packaging, and feature
parity. A replacement requires a new decision; there is no plan or timeline.

Primary use cases:

- Backing up recovery keys, API tokens, secrets manager master keys, or any arbitrary
  bytes.
- Social recovery (distribute shares across trusted parties).
- Separation of duties (require multiple people to reconstruct).
- Geographic separation (reduce "single location" failure).

Non-goals:

- This is not a full secrets manager.
- This is not a wallet/seed product. Mnemonics are an encoding for share packets.

---

## 2. Functional Requirements

### 2.1 Core: Split / Combine

- Input: arbitrary bytes.
- Algorithm: Shamir-style secret sharing over GF(256).
- Parameters: `k` and `n` with `1 <= k <= n <= 255`.
- Output: `n` share packets with:
  - set ID (random)
  - `k`, `n`, share index `x`
  - payload bytes
  - optional crypto parameters
- Integrity: BLAKE3 tag ensures reconstruction fails for corrupted/mismatched shares.
- Optional encryption: encrypt-before-split using Argon2id -> ChaCha20-Poly1305.

### 2.2 Encodings

Safeparts defines a binary share packet format and supports multiple reversible
text encodings.

Implemented encodings in the Rust toolchain:

- `base64` / `base64url`: URL-safe, no padding
- `base58` / `base58check`: Base58 with checksum
- `mnemo-words`: single sentence using the BIP-39 word list as an alphabet + CRC16
- `mnemo-bip39`: valid BIP-39 mnemonics, may be multiple phrases per share

Web UI currently supports:

- `base64url`
- `mnemo-words`

(The CLI/TUI support the full encoding set.)

### 2.3 CLI

The CLI is non-interactive by default and script-friendly.

Commands:

```bash
safeparts split -k 2 -n 3 -e mnemo-words
safeparts combine --encoding base64
safeparts tui
```

Requirements:

- `split` reads from stdin by default, or `--in <file>`.
- `combine` reads shares from stdin by default, or `--in <file>`.
- `--out <file>` writes output to a file; otherwise stdout.
- Passphrase is provided via `--passphrase` or `--passphrase-file`.
- `combine` supports encoding auto-detection when `--encoding` is omitted.

### 2.4 TUI

The TUI provides an interactive workflow for:

- entering a secret
- selecting `k`, `n`, encoding, passphrase
- splitting and copying shares
- pasting shares and reconstructing

Requirements:

- Keyboard-first operation.
- Clipboard support (system clipboard when possible; OSC52 fallback where supported).
- Load/save secrets and shares from/to files.

### 2.5 Web UI

The Web UI is a static site built with Vite + React and uses WASM bindings for all
cryptographic operations.

Requirements:

- No backend required.
- Split and combine happen locally in the browser (WASM).
- Share encoding auto-detection when pasting shares in the combine UI.
- i18n: English + Arabic (RTL) UI.

### 2.6 Help Website

Safeparts includes a documentation site built with Astro Starlight.

Requirements:

- Served under `/help/` on the same domain as the main app.
- i18n: English + Arabic (RTL).
- Includes docs for the Web UI, CLI, TUI, build/run instructions, encodings, and
  security notes.

---

## 3. Non-Functional Requirements

Security:

- Threshold secrecy: fewer than `k` shares reveal nothing about the secret.
- Integrity: reconstruction fails when shares are corrupted or mismatched.
- Optional passphrase encryption uses modern KDF + AEAD.
- Zeroize secret material where practical (`zeroize`).
- Minimize sensitive logging. Treat shares and reconstructed secrets as secrets.

Local-first:

- Web UI runs entirely client-side.
- No cookies or analytics are required for core functionality.

Portability:

- Rust components support Linux, macOS, and Windows.
- Web and docs build into a single static bundle for easy self-hosting.

---

## 4. Repository & Build Model

Rust:

- CI runs `cargo fmt`, `cargo clippy`, and `cargo test`.

Web:

- Package manager: Bun.
- WASM build step is required before the Web UI works.

```bash
cd web
bun install
bun run build:wasm
bun run build
```

Help site:

```bash
cd web
bun run help:build
```

Deployment:

- Netlify and Cloudflare publish the same tested, retained static artifact without rebuilding source.
- The docs build output is placed in `web/dist/help/`.

---

## 5. Current Status vs Backlog

Implemented:

- Core split/combine + integrity + optional passphrase encryption.
- Base64url, Base58check, mnemo-words, mnemo-bip39 encodings in Rust.
- CLI + TUI.
- WASM bindings.
- Web UI (split/combine) with encoding auto-detection.
- Help website under `/help/` with English/Arabic.

Backlog / future ideas (not required for current release):

- Web UI support for Base58check and mnemo-bip39.
- QR code export.
- Optional import/export compatibility with other tools.
- Improved CLI/TUI archive distribution and release integrity.

## Git workflow

- Always commit atomically after each meaningful task 
- Write conventional commits style commit messages
- Prefer rebase over merge
- When starting a new feature, create a feature branch

