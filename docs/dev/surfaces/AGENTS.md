# AGENTS.md — Developer Surface Guides

## Purpose

Owns focused contributor guides for each Safeparts surface.

## Ownership

- `core.md`: Rust core library and packet/encoding ownership.
- `cli.md`: CLI flags, IO behavior, and e2e tests.
- `tui.md`: terminal UI workflow and interaction rules.
- `wasm.md`: wasm-bindgen boundary consumed by the web app.
- `web.md`: Vite/React app, WASM package boundary, and browser checks.
- `desktop.md`: retired Tauri source notice and recovery pointers.
- `help-docs.md`: Astro/Starlight help-site contributor notes.
- `release.md`: release packaging and workflow notes.
- `mobile.md`: dormant mobile prototype expectations.
- `macos.md`: retired SwiftUI and dedicated UniFFI reference notice.
- `windows.md`: retired WinUI and dedicated UniFFI reference notice.

## Local Contracts

- Keep these guides contributor-facing and implementation-focused.
- Do not duplicate full API docs or user help pages.
- Update a guide when the matching source subtree contract changes.
- Dormant guides must not prescribe builds, tests, binding refresh, packaging, parity, or promotion. Preserve links for wayfinding; supported CLI/TUI host platforms still include Linux, macOS, and Windows.

## Work Guidance

- Keep each guide short enough to scan before making a change.
- Link to the owning source path and nearest `AGENTS.md`.

## Verification

- Run `mise run dx:verify` after changing guide names or links.

## Child DOX Index

- No child AGENTS.md boundaries. The surface guides listed under Ownership remain directly owned here.
