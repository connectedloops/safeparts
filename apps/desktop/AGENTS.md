# AGENTS.md — Experimental native desktop app

## Purpose

Owns the real Qt Widgets text round-trip slice for issue 140. This is development-host evidence, not a packaged or platform-qualified release.

## Ownership

- `src/`: single-window Qt UI, one serialized Rust worker, exact text editor, and bounded clipboard adapters.
- `tests/`: generated-CXX contract and Qt user-action/lifecycle checks.
- `scripts/`: deterministic generated-bridge command.
- `CMakeLists.txt`, `run.sh`: local build, test, and launch entry points.

## Local Contracts

- Use Qt Widgets and `crates/safeparts_desktop_bridge`; never copy core algorithms or use retired application sources.
- Keep one worker. Parsing, encoding, splitting, and recovery never run on Qt's event thread.
- Reject stale worker results by generation. Keep admitted source text visible after creation; a later create-field edit hides the generated-share controls, resets Rust state, and invalidates pending work. Mode changes, Start over, and close clear the source and generated state and always resolve the busy state.
- Keep authoritative packets, recovery input, and recovered bytes in Rust. Qt owns bounded presentation and intentional clipboard handoffs. Carry sensitive cross-thread bytes in `SecureByteBuffer`; its final shared owner wipes the sole `QByteArray` allocation.
- Support only V2 unprotected Words text in this slice. Do not misrepresent later encodings, passphrases, files, saving, or printing as implemented.
- Do not add autonomous networking, persistence, content logs, session restoration, or undo history.

## Work Guidance

- Follow the accepted visual direction at issue 140 comment `5747267572`: compact joined mode selector, system typography, rounded groups, restrained colors, clear status, and balanced spacing.
- Preserve valid UTF-8 input in the editor-owned model without trimming or Unicode normalization. Convert CRLF and lone CR to LF on text admission, then admit keyboard, IME, and paste replacements against the 1 MiB resulting UTF-8 size before changing the model. Render data as plain text and copy authoritative worker output rather than reading a widget back.
- The macOS clipboard adapter avoids Qt string materialization until after its byte check, but AppKit has already returned a complete `NSData`. Treat pre-materialization clipboard admission as blocked until a supported range/stream API exists. Other platform adapters remain unqualified.

## Verification

- Build: `mise run desktop:build`
- Public and Qt actions: `mise run desktop:test`
- Launch: `mise run desktop:run`
- Run `mise run verify` before review.

## Child DOX Index

- No child AGENTS.md files.
