# TUI

Owner: `crates/safeparts_tui/`
Nearest contract: [`crates/safeparts_tui/AGENTS.md`](../../../crates/safeparts_tui/AGENTS.md)

## What belongs here

The TUI is the keyboard-first terminal workflow over `safeparts_core`.

It owns:

- terminal application state
- split/combine interaction flow
- clipboard behavior and fallbacks
- terminal-focused validation and status messages

## Change rules

- Keep secret-sharing behavior in core. Mnemonic input uses `parse_share_packets_wrapped_mnemonics`: complete Recovery shares per line (including CLI output files), or wrapped Recovery shares separated by blank lines. Auto and explicit encoding accept LF and CRLF.
- Keep the CLI-to-TUI file-loading regression: produce real CLI mnemonic output, load both a Threshold-sized selection and the full file into the headless TUI, and compare recovered bytes.
- Keep keyboard operation reliable before adding mouse-only affordances.
- Route printable input, including `?`, to the focused editor. Multiline editors own unmodified arrows and other text-navigation keys; Up/Down still adjust focused settings or select a Recovery share.
- Use Alt+Left/Right to switch operations and F1 to open help from normal focus. Keep Enter submission, explicit Ctrl actions, and Esc modal cancellation intact. Check rendered shortcuts and both language versions of the TUI help page when changing keys.
- Treat clipboard contents as sensitive. Split clipboard actions copy only the selected Recovery share; they never gather multiple Recovery shares.
- Avoid writing share text or recovered secrets to logs.
- Save recovery shares and reconstructed secrets through atomic private-file output. On Unix, exported files must be owner-only.
- Results last until their operation's inputs change or another attempt starts. Editing Secret or Recovery-share text, Threshold, Share count, Share encoding, or passphrase clears the affected output and result metadata. Copy and save/export become available again only after success.
- Loading a Secret file clears Split output, including when reloading a path whose contents may have changed. Loading Recovery-share files clears recovered output when the input text changes.
- Preserve results when users move focus, select a Recovery share, cancel a file dialog, or make an edit that leaves the input unchanged. Cursor movement in an empty Secret editor must not switch away from file input.
- Add headless app-state tests for split/recovery workflows, recovery failures, focus wrapping, modal and status transitions, keyboard shortcuts, cyclic settings, and rendering. Result-lifetime tests must check clipboard writes and file outcomes, including adding Passphrase protection after an unprotected Split.
- Use manual terminal smoke tests for rendering, clipboard integration, and other host behavior.
- Keep terminal setup behind an RAII session guard so raw mode, alternate-screen state, and cursor visibility are restored on every exit path.

## Useful checks

```bash
cargo test -p safeparts_tui
cargo test --all-features
cargo clippy --all-targets --all-features -- -D warnings
```

Manual smoke:

```bash
cargo run -p safeparts_tui
```

## When TUI changes

Update:

- [`docs/dev/feature-matrix.md`](../feature-matrix.md)
- CLI/TUI docs if launch or shortcut behavior changes
- release notes when binary packaging changes
