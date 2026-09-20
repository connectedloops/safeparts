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
- For macOS checkpoints, use Cocoa window chrome, the system font, semantic palette colors, and the system accent. Use native text editing and keyboard behavior. Targeted Qt styling or painting is appropriate for modern grouped surfaces, compact selectors, editors, and actions; avoid universal widget stylesheets and forced cross-platform styles.
- Use the [Apple Passwords detail pane](https://support.apple.com/guide/passwords/the-passwords-app-mchl901b1b95/mac) as the visual reference for quiet rounded grouping, subtle separators, hierarchy, and inset spacing. The [accepted visual direction](https://github.com/connectedloops/safeparts/issues/140#issuecomment-5747267572) guides production UI work; fixture code must be replaced, not promoted. Visual approval does not waive runtime acceptance gates.
- Model duplicate/unknown-token blocking and immediate mode/reset clearing, including hidden input. These are fixture interactions, not real share validation.
- Keep generated builds and captures under `prototype/build/`.

## Verification

- Build and run capture mode with `mise run desktop:prototype -- --capture apps/desktop/prototype/build/captures`.
- Use `--appearance light` or `--appearance dark` for process-local scheme captures, and `--small` for the 620 by 480 minimum-size fixture.
- Confirm all four PNG fixtures exist and inspect the normal and minimum-size light and dark sets. Then run `mise run dx:verify` and `git diff --check`.

## Child DOX Index

- No child AGENTS.md files.
