# TUI

Owner: `crates/safeparts_tui/`
Nearest contract: [`crates/safeparts_tui/AGENTS.md`](../../../crates/safeparts_tui/AGENTS.md)

The TUI owns terminal state, keyboard interaction, clipboard actions, file workflows, and operator guidance. Core owns sharing, encoding, and parsing.

## Input and result state

- Use the core wrapped-mnemonic parser. It accepts complete shares per line first, then wrapped shares separated by blank lines, with LF/CRLF and Auto or explicit encoding.
- Invalidate an operation's result when its inputs change or another attempt starts. Preserve results on focus movement, share selection, dialog cancellation, and unchanged edits.
- Load files and update state according to the owning contract, including re-reading a secret file at split time. Keep CLI-output-to-TUI recovery tests for both threshold-sized selections and full mnemonic files.

## Keyboard and clipboard

- Keep printable characters, including `?`, in focused text editors. Unmodified arrows navigate text; Alt+Left/Right switches operations and F1 opens help outside modals.
- Preserve explicit Ctrl actions, Enter submission, Esc cancellation, and the RAII terminal-session guard.
- Copy only the selected recovery share during split. Keep clipboard contents and recovered bytes out of logs.
- When keys change, update both locale TUI help pages and test keyboard events, focus, and rendering.

## File failure recovery

- Use atomic private-file output. Reject NUL destinations before touching output; Unix exports are owner-only.
- Keep previous input after failed loads and results after failed saves. Show sanitized operation context, failed-file ordinals, and completed export counts, not paths or IO chains.
- A batch export can leave completed files. Test failure and retry in the same app through keyboard events, including rendered guidance and footer space.

The [owning TUI contract](../../../crates/safeparts_tui/AGENTS.md) has the complete state, clipboard, file, and test requirements. Consult it before changing these flows rather than treating this orientation as a substitute.

## Verification

From the repository root:

```bash
cargo test -p safeparts_tui
cargo test --all-features
cargo clippy --all-targets --all-features -- -D warnings
```

For manual rendering and host clipboard checks in an interactive terminal:

```bash
cargo run -p safeparts_tui
```

## When TUI changes

Update [feature coverage](../feature-matrix.md), the owning contract, and relevant headless tests. Update CLI/TUI help for launch or shortcut changes and release guidance for packaging changes.
