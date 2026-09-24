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
- Rust owns authoritative validation, packets, recovery state, and recovered bytes. Qt may retain only bounded editor-owned text, one selected file Secret, passphrases, generated Recovery share presentation, and Combine presentation state, plus intentional clipboard and file handoffs. Passphrase controls admit at most 1 MiB of valid UTF-8, render only masking glyphs, expose no copy action or undo history, and clear their owned state on mode change, Start over, and close; Qt and OS copies remain subject to the documented forensic-erasure limitation. Encode generated shares sequentially on the worker when the complete set fits the checked presentation budget. For larger valid sets, keep every share exportable through authoritative Copy or Save requests and retain only the latest copied share in `SegmentedShareView`, backed by the sole validated ASCII byte allocation rather than a whole `QString` or text document. Replace the complete visible recovery set on the worker after edits; never parse it on the UI thread. Carry sensitive cross-thread and selected-file bytes in `SecureByteBuffer`; its final shared owner wipes the sole `QByteArray` allocation.
- Split V2 unprotected or passphrase-protected text or one exact-byte file into all four core Share formats. Protection is off by default and requires a byte-exact nonempty confirmation. Combine unprotected V1/V2 and protected V2 shares loaded from text fields or files through core Auto or an explicit format; require a passphrase before protected recovery and keep uncertain decryption failures content-free. Valid UTF-8 recovery has inert display and authoritative Copy; every successful recovery, including binary and empty output, has authoritative exact-byte Save.
- File acquisition uses bounded streaming actual reads. Exports write directly to only the selected destination with no staging, rename, auto-open, rollback, or source alteration. Paths never cross CXX. Cancellation is a no-op; write failure preserves the operation and may leave a partial selected destination.
- Qt allocation-failure tests inject a narrow constructor-owned `DesktopAllocationPolicy`, shared with `SegmentedShareView`. Production uses the always-allow default; there is no environment, configuration, UI, or global mutable activation. Test policies fail one named boundary once, report content-free errors, preserve accepted state, suppress the affected handoff, and permit retry. Check accessibility immediately before its `QString` text-range handoff and explicit selection Copy immediately before clipboard writing; empty-selection Copy remains an authoritative worker request.
- Do not add autonomous networking, persistence, content logs, session restoration, or undo history.

## Work Guidance

- Follow the accepted visual direction at issue 140 comment `5747267572`: compact joined Split/Combine selector without a focus border, system typography, rounded groups, restrained colors, clear status, and balanced spacing. Use the active web UI terminology and its overlapping-documents copy glyph.
- Preserve valid UTF-8 input, including passphrases, in each editor-owned model without trimming or Unicode normalization. For Secret text only, convert CRLF and lone CR to LF on admission; loaded Recovery share files preserve supported line endings and framing. Preserve passphrase line endings byte-for-byte. Admit keyboard, IME, paste, and file replacements against the configured resulting inclusive UTF-8 limit before changing the model: 1 MiB for the Secret or one passphrase and 16 MiB for one pasted or imported Recovery batch. Generated Recovery shares remain bounded to 8 MiB. Render data as plain text without line wrapping so maximum admitted text does not trigger unbounded Qt layout work. Generated Recovery share views are read-only/selectable, clear immediately on invalidation, and use checked four-times UTF-8 accounting within the 160 MiB presentation budget. When the complete set exceeds that budget, clear and wipe the previous reveal before retaining the latest copied share. The virtualized reveal uses fixed 64-byte rows, global selectable offsets, bounded visible-row conversion, horizontal scrolling, keyboard and assistive-text access, and no retained whole-share `QString` or text document during normal paint/view operation. An explicit accessibility full-range query may create one transient Latin-1-to-`QString` handoff of at most 8 MiB; release it after the caller returns and treat its allocator/forensic erasure like other presentation handoffs. Reveal, replacement, and clear report exact text insertion/removal in at-most-64-KiB transient accessibility-event chunks after the model reaches the corresponding final state; external assistive technology may retain its own copies. Clipboard handoff may add an OS-owned copy. Toolbar Copy and Save request authoritative worker output rather than reading a widget back; copying an explicit viewer selection is a presentation-only text handoff.
- The macOS clipboard adapter avoids Qt string materialization until after its byte check, but AppKit has already returned a complete `NSData`. Treat pre-materialization clipboard admission as blocked until a supported range/stream API exists. Other platform adapters remain unqualified.

## Verification

- Build: `mise run desktop:build`
- Public and Qt actions: `mise run desktop:test`
- Launch: `mise run desktop:run`
- Capacity evidence: run `mise run desktop:evidence:capacity` with a named case from `apps/desktop/scripts/run_capacity_evidence.sh`; every case resolves and hashes its exact native executable, then runs `/usr/bin/time -l` directly around a PID-preserving `exec`, so direct-test RSS and peak private footprint are the independent strict gates. `maximum-retained` additionally samples that stable PID, requires every named phase with bounded gaps and no sampling errors, and derives its ownership ledger from emitted test facts. Treat a timeout, ambiguous identity, incomplete samples, or either metric at 1 GiB as a blocker.
- Run `mise run verify` before review.

## Child DOX Index

- No child AGENTS.md files.
