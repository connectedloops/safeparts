# AGENTS.md — Experimental native desktop app

## Purpose

Owns the experimental Qt Widgets text workflow through share-encoding and legacy-recovery issue 141. This is development-host evidence, not a packaged or platform-qualified release.

## Ownership

- `src/`: single-window Qt UI, one serialized Rust worker, exact text editor, and bounded clipboard adapters.
- `tests/`: generated-CXX contract and Qt user-action/lifecycle checks.
- `scripts/`: deterministic generated-bridge command.
- `CMakeLists.txt`, `run.sh`: local build, test, and launch entry points.

## Local Contracts

- Use Qt Widgets and `crates/safeparts_desktop_bridge`; never copy core algorithms or use retired application sources.
- Keep one worker. Parsing, encoding, splitting, and recovery never run on Qt's event thread.
- Reject stale worker results by generation. Keep the admitted Secret visible after Split; a later Secret or split-setting edit hides the generated-share controls, resets Rust state, and invalidates pending work. Recovery share edits replace the complete nonempty visible set on the worker. Mode changes, Start over, and close clear the visible and generated state and always resolve the busy state.
- Rust owns authoritative validation, packets, recovery state, and recovered bytes. Qt may retain only bounded presentation text shown in the Secret editor, generated Recovery share views, and Combine editors, plus intentional clipboard handoffs. Encode generated shares sequentially on the worker when the complete set fits the checked presentation budget. For larger valid sets, keep every share exportable through authoritative Copy requests and retain at most one revealed share. Replace the complete visible recovery set on the worker after edits; never parse it on the UI thread. Carry sensitive cross-thread bytes in `SecureByteBuffer`; its final shared owner wipes the sole `QByteArray` allocation.
- Split V2 unprotected text into all four core Share formats. Combine unprotected V1/V2 shares through core Auto or an explicit format. Recognize protected shares as passphrase-required, but do not implement passphrase entry before issue 142. Do not misrepresent files, saving, or printing as implemented.
- Do not add autonomous networking, persistence, content logs, session restoration, or undo history.

## Work Guidance

- Follow the accepted visual direction at issue 140 comment `5747267572`: compact joined Split/Combine selector without a focus border, system typography, rounded groups, restrained colors, clear status, and balanced spacing. Use the active web UI terminology and its overlapping-documents copy glyph.
- Preserve valid UTF-8 input in each editor-owned model without trimming or Unicode normalization. Convert CRLF and lone CR to LF on text admission, then admit keyboard, IME, and paste replacements against the configured resulting UTF-8 limit before changing the model: 1 MiB for the Secret and 8 MiB for one visible Recovery share. Render data as plain text. Generated Recovery share views are read-only/selectable, clear immediately on invalidation, and use checked four-times UTF-8 accounting within the 160 MiB presentation budget. When the complete set exceeds that budget, reveal only the latest copied share. Copy authoritative worker output rather than reading a widget back.
- The macOS clipboard adapter avoids Qt string materialization until after its byte check, but AppKit has already returned a complete `NSData`. Treat pre-materialization clipboard admission as blocked until a supported range/stream API exists. Other platform adapters remain unqualified.

## Verification

- Build: `mise run desktop:build`
- Public and Qt actions: `mise run desktop:test`
- Launch: `mise run desktop:run`
- Run `mise run verify` before review.

## Child DOX Index

- No child AGENTS.md files.
