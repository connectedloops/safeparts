# AGENTS.md — Experimental native desktop app

## Purpose

Owns the experimental Qt Widgets text and exact-byte file workflow through capacity and fault handling issue 147. This is development-host evidence, not a packaged or platform-qualified release.

## Ownership

- `src/`: single-window Qt UI, one serialized Rust worker, exact text editor, bounded clipboard adapters, and direct file I/O.
- `tests/`: generated-CXX contract and Qt user-action/lifecycle checks.
- `scripts/`: deterministic generated-bridge command.
- `CMakeLists.txt`, `run.sh`: local build, test, and launch entry points.

## Local Contracts

- Use Qt Widgets and `crates/safeparts_desktop_bridge`; never copy core algorithms or use retired application sources.
- Keep one worker. Parsing, encoding, splitting, and recovery never run on Qt's event thread.
- Reject stale worker results by generation. Keep the admitted Secret visible after Split; a later Secret or split-setting edit hides the generated-share controls, resets Rust state, and invalidates pending work. Recovery share edits replace the complete nonempty visible set on the worker. Mode changes, Start over, and close clear the visible and generated state and always resolve the busy state.
- Rust owns authoritative validation, packets, recovery state, and recovered bytes. Qt may retain only bounded editor-owned text, one selected file Secret, passphrases, generated Recovery share presentation, and Combine presentation state, plus intentional clipboard and file handoffs. Passphrase controls admit at most 1 MiB of valid UTF-8, render only masking glyphs, expose no copy action or undo history, and clear their owned state on mode change, Start over, and close; Qt and OS copies remain subject to the documented forensic-erasure limitation. Encode generated shares sequentially on the worker when the complete set fits the checked presentation budget. For larger valid sets, keep every share exportable through authoritative Copy or Save requests without retaining encoded text in a Qt document. Replace the complete visible recovery set on the worker after edits; never parse it on the UI thread. Carry sensitive cross-thread and selected-file bytes in `SecureByteBuffer`; its final shared owner wipes the sole `QByteArray` allocation.
- Split V2 unprotected or passphrase-protected text or one exact-byte file into all four core Share formats. Protection is off by default and requires a byte-exact nonempty confirmation. Combine unprotected V1/V2 and protected V2 shares loaded from text fields or files through core Auto or an explicit format; require a passphrase before protected recovery and keep uncertain decryption failures content-free. Valid UTF-8 recovery has inert display and authoritative Copy; every successful recovery, including binary and empty output, has authoritative exact-byte Save.
- File acquisition uses bounded streaming actual reads. Exports write directly to only the selected destination with no staging, rename, auto-open, rollback, or source alteration. Paths never cross CXX. Cancellation is a no-op; write failure preserves the operation and may leave a partial selected destination.
- Do not add autonomous networking, persistence, content logs, session restoration, or undo history.

## Work Guidance

- Follow the accepted visual direction at issue 140 comment `5747267572`: compact joined Split/Combine selector without a focus border, system typography, rounded groups, restrained colors, clear status, and balanced spacing. Use the active web UI terminology and its overlapping-documents copy glyph.
- Preserve valid UTF-8 input, including passphrases, in each editor-owned model without trimming or Unicode normalization. For Secret text only, convert CRLF and lone CR to LF on admission; loaded Recovery share files preserve supported line endings and framing. Preserve passphrase line endings byte-for-byte. Admit keyboard, IME, paste, and file replacements against the configured resulting inclusive UTF-8 limit before changing the model: 1 MiB for the Secret or one passphrase and 16 MiB for one pasted or imported Recovery batch. Generated Recovery shares remain bounded to 8 MiB. Render data as plain text without line wrapping so maximum admitted text does not trigger unbounded Qt layout work. Generated Recovery share views are read-only/selectable, clear immediately on invalidation, and use checked four-times UTF-8 accounting within the 160 MiB presentation budget. When the complete set exceeds that budget, leave views empty and export each share on demand. Copy and Save request authoritative worker output rather than reading a widget back.
- The macOS clipboard adapter avoids Qt string materialization until after its byte check, but AppKit has already returned a complete `NSData`. Treat pre-materialization clipboard admission as blocked until a supported range/stream API exists. Other platform adapters remain unqualified.

## Verification

- Build: `mise run desktop:build`
- Public and Qt actions: `mise run desktop:test`
- Launch: `mise run desktop:run`
- Capacity evidence: `CAPACITY_CASE=maximum-valid mise run desktop:evidence:capacity` or `CAPACITY_CASE=maximum-words mise run desktop:evidence:capacity`
- Run `mise run verify` before review.

## Child DOX Index

- No child AGENTS.md files.
