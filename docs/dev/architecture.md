# Architecture

Safeparts is a local-first threshold secret-sharing toolkit. Supported interfaces use the same core logic.

## Repo map

```text
crates/safeparts_core/   Core split/combine, packets, encodings, optional encryption
crates/safeparts/        CLI binary: safeparts
crates/safeparts_tui/    Terminal UI binary: safeparts-tui
crates/safeparts_wasm/   wasm-bindgen facade for the browser app
web/                    Vite + React browser UI
web/help/               Astro + Starlight help site under /help/
scripts/                Repository automation
```

`desktop/` (Tauri), `macos/` (SwiftUI), `windows/` (WinUI), and `crates/safeparts_uniffi/` are dormant reference for retired applications. `mobile/` retains dormant prototype artifacts. These are outside supported builds, tests, coverage, packaging, and parity requirements; the retired Rust crates are excluded from the active workspace. Existing native commands are not supported. A replacement needs a new decision; there is no plan or timeline.

## Data flow

```text
secret bytes
  -> optional passphrase protection in safeparts_core
  -> BLAKE3 integrity tag
  -> Shamir-style split over GF(256)
  -> SharePacket values
  -> selected share encoding
  -> CLI, TUI, WASM, or web presentation
```

Combine reverses that path:

```text
share text
  -> decode into SharePacket values
  -> validate metadata and threshold
  -> reconstruct tagged bytes
  -> verify integrity tag
  -> optional decrypt
  -> recovered secret bytes
```

## Source of truth

- Cryptography, packet parsing, validation, and encodings live in `crates/safeparts_core/`.
- CLI, TUI, WASM, and web adapt IO and presentation rather than forking secret-sharing rules.
- Release CI packages CLI/TUI archives for Linux, macOS, and Windows. Host names do not imply GUI application support.
- The help site is user-facing documentation. Developer workflow docs live under `docs/dev/`.

## Boundary rules

- Do not add a backend for split/combine. Browser workflows stay local.
- Do not log share text, passphrases, or recovered secrets.
- Keep generated WASM output out of source edits unless the task is explicitly about generated artifacts.
- Leave dormant native bindings and schemas unchanged during supported-surface work.
- Keep release packaging scripts deterministic and explicit about inputs and outputs.

## Where to make changes

| Change | Primary owner | Usually also update |
| --- | --- | --- |
| New split/combine rule | `safeparts_core` | CLI, TUI, WASM, web tests, feature matrix |
| New share encoding | `safeparts_core::encoding` | CLI/TUI choices, WASM API, UI choices if exposed, docs, tests |
| CLI flag | `crates/safeparts` | CLI tests, help docs if user-visible, feature matrix |
| Web workflow | `web/src` | Web tests, WASM boundary if changed, help guidance |
| Build or release behavior | `scripts/`, `.github/`, `mise.toml` | `docs/dev/verification.md`, release guide |
