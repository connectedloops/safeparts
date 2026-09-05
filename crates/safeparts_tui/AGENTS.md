# AGENTS.md — safeparts_tui

## Purpose

Owns the `safeparts-tui` interactive terminal UI.

## Ownership

- `src/app.rs`: terminal application state, rendering, events, keyboard flow.
- `src/domain.rs`: TUI-facing domain adapters over `safeparts_core`.
- `src/clipboard.rs`: system clipboard and terminal clipboard behavior.
- `src/main.rs`: binary entry point.

## Local Contracts

- Keep split/combine and encoding rules in `safeparts_core`.
- File loading and pasted mnemonic input use the core wrapped parser: accept CLI files with one complete Recovery share per line as well as blank-line-separated wrapped shares, with Auto or explicit encoding.
- Keep the TUI keyboard-first. Focused editors own printable text (including `?`) and multiline text-navigation keys. Reserve Alt+Left/Right for operation switching and F1 for help from normal focus; keep Enter submission and explicit Ctrl action shortcuts.
- Up/Down adjust numeric settings, Share encoding, or Recovery-share selection only when those controls have focus. Navigating the Secret editor preserves its loaded-file source; editing text switches to text input. Keep on-screen shortcuts and English/Arabic TUI help aligned with routing.
- Treat clipboard contents, shares, passphrases, and recovered secrets as sensitive.
- Split clipboard actions must copy only the selected Recovery share, never a multi-share payload.
- Results belong to the inputs that produced them. Changes to Secret/Recovery-share text, Threshold, Share count, Share encoding, or passphrase clear that operation's output and result metadata before copy/export/save can use it. Successful Secret file loading also clears Split output; Recovery file loading clears output when its text changes.
- Clear an operation's previous result before attempting it, including early rejection and IO failure. Only success makes output available again.
- Preserve valid results for focus movement, Recovery-share selection, cancelled loading, and edits that leave input values unchanged. Keep file input selected until a text edit actually changes the editor.
- Do not add logging that includes secret material.

## Work Guidance

- Follow `docs/agents/conventions.md` and `docs/dev/surfaces/tui.md`.
- Add domain/state tests when changing behavior that can be tested without terminal rendering. Test result lifetime through headless key events, recording clipboard reads/writes, and temporary-file load/save/export outcomes with synthetic data.
- Keep terminal messages actionable and short.

## Verification

- `cargo test -p safeparts_tui` (the CLI-to-TUI file regression invokes Cargo to run the local `safeparts` producer)
- `cargo run -p safeparts_tui` for manual smoke when UI behavior changes
- `cargo fmt --all -- --check`
- `cargo clippy --all-targets --all-features -- -D warnings`

## Child DOX Index

- No child AGENTS.md files yet.
