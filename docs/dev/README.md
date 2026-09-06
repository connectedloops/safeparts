# Developer docs

Start here when you work on Safeparts as a contributor or maintainer.

## Quick path

1. Read the root [`AGENTS.md`](../../AGENTS.md) and the nearest child `AGENTS.md` for the files you will touch.
2. Install tools with `mise install` if you use mise.
3. Install project dependencies:

   ```bash
   mise run setup
   ```

4. Check your local environment:

   ```bash
   mise run doctor
   ```

5. Run the checks for your change. The full local gate is:

   ```bash
   mise run verify
   ```

## What to read

- [Onboarding](onboarding.md): first setup, common commands, and first PR flow.
- [Architecture](architecture.md): how core, CLI, TUI, WASM, web, help, and release packaging fit together.
- [Feature matrix](feature-matrix.md): the cross-surface map you update when behavior changes.
- [Workflows](workflows.md): repeatable steps for features, bugs, encodings, UI work, docs, and releases.
- [Developer manuals](manuals/README.md): longer guides for Rust library integration and CLI automation.
- [Change checklist](change-checklist.md): template for multi-surface feature work.
- [Verification](verification.md): local and CI check matrix.
- [Generated artifacts](generated-artifacts.md): what is generated, what is tracked, and how to refresh it.
- [Troubleshooting](troubleshooting.md): local setup and build fixes.

## Manuals

- [Rust library integration](manuals/rust-library.md)
- [CLI automation](manuals/cli-automation.md)

## Surface guides

- [Core library](surfaces/core.md)
- [CLI](surfaces/cli.md)
- [TUI](surfaces/tui.md)
- [WASM bindings](surfaces/wasm.md)
- [Web app](surfaces/web.md)
- [Help docs](surfaces/help-docs.md)
- [Release packaging](surfaces/release.md)

## Dormant reference

These sources are outside supported builds, tests, packaging, and parity maintenance. Existing native commands are not supported after workspace exclusion. There is no replacement plan or timeline; a replacement requires a new decision. CLI/TUI archives continue to support Linux, macOS, and Windows.

- [Retired Tauri application](surfaces/desktop.md)
- [Retired SwiftUI application](surfaces/macos.md)
- [Retired WinUI application](surfaces/windows.md)
- [Dedicated UniFFI bridge contract](../../crates/safeparts_uniffi/AGENTS.md)
- [Dormant mobile prototype](surfaces/mobile.md)

## DX maintenance rule

When a change affects how a developer builds, tests, runs, extends, or reviews Safeparts, update the closest developer doc and the nearest `AGENTS.md` contract. If a feature changes any product surface, update [feature-matrix.md](feature-matrix.md).
