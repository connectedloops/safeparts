# Developer docs

Start here when you work on Safeparts as a contributor or maintainer.

## Quick path

1. Read the root [`AGENTS.md`](../../AGENTS.md) and the nearest child `AGENTS.md` for the files you will touch.
2. Follow [onboarding](onboarding.md#2-install-tools) for prerequisites and setup.
3. Run the checks for your change, then the [local aggregate gate](verification.md#one-command-checks) when practical. The verification guide also lists checks outside `mise run verify`.

## What to read

- [Onboarding](onboarding.md): first setup, common commands, and first PR flow.
- [Architecture](architecture.md): how core, CLI, TUI, WASM, web, help, and release packaging fit together.
- [Feature matrix](feature-matrix.md): the cross-surface map you update when behavior changes.
- [Workflows](workflows.md): repeatable steps for features, bugs, encodings, UI work, docs, and releases.
- [Developer manuals](manuals/README.md): longer guides for Rust library integration and CLI automation.
- [Change checklist](change-checklist.md): template for multi-surface feature work.
- [Verification](verification.md): local and CI check matrix.
- [Generated artifacts](generated-artifacts.md): what is generated, what is tracked, and how to refresh it.
- [Branch protection rollout](branch-protection.md): prepared merge-gate rule, rollout, rollback, and changelog-writer options.
- [Troubleshooting](troubleshooting.md): local setup and build fixes.

## Surface guides

- [Core library](surfaces/core.md)
- [CLI](surfaces/cli.md)
- [TUI](surfaces/tui.md)
- [WASM bindings](surfaces/wasm.md)
- [Web app](surfaces/web.md)
- [Help docs](surfaces/help-docs.md)
- [Release packaging](surfaces/release.md)

## Dormant reference

These sources are unsupported reference, outside active builds, tests, packaging, and parity. See the linked notices for boundaries and recovery guidance. CLI/TUI support on Linux, macOS, and Windows continues.

- [Retired Tauri application](surfaces/desktop.md)
- [Retired SwiftUI application](surfaces/macos.md)
- [Retired WinUI application](surfaces/windows.md)
- [Dedicated UniFFI bridge contract](../../crates/safeparts_uniffi/AGENTS.md)
- [Dormant mobile prototype](surfaces/mobile.md)

## DX maintenance rule

When a change affects how a developer builds, tests, runs, extends, or reviews Safeparts, update the closest developer doc and the nearest `AGENTS.md` contract. If a feature changes any product surface, update [feature-matrix.md](feature-matrix.md).
