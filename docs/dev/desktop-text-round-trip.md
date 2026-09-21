# Experimental desktop text round trip

Issue 140 adds a reviewable native slice, not a desktop release. The accepted visual direction is [issue 140 comment 5747267572](https://github.com/connectedloops/safeparts/issues/140#issuecomment-5747267572). The source fixture remains available on `prototype/desktop-reference-ui` at `874c5e7`; production code does not import it.

## Run and test

Use synthetic text only.

```bash
mise run desktop:test
mise run desktop:run
```

The first command builds the Rust adapter and generated CXX source, compiles the Qt app with warnings denied, and runs the public CXX and Qt action suites. The second command builds and opens the same app.

Rust owns generated packets, recovery validation and state, and authoritative recovered bytes. Qt retains bounded presentation text visible in the Secret editor, generated Recovery share views, and Combine editors. Generated shares are encoded one at a time on the serialized worker and admitted all-or-none with checked four-times UTF-8 accounting under a 160 MiB presentation budget; Copy requests a fresh authoritative Rust output instead of reading the view. After a Combine edit, the UI sends the complete nonempty field set to the serialized worker; the worker resets and rebuilds Rust recovery state before returning one readiness result. Sensitive bytes crossing queued Qt signals use one shared owning buffer; its final owner overwrites the sole `QByteArray` allocation before release. Recovery admission uses checked arithmetic for current Rust-owned state and the next parser phase, including decoded packets, incoming bridge/Qt copies, parser workspace, and fixed UI/runtime headroom. The generated interface uses consuming `destroy_operation`; CXX vectors own returned copies. Every fallible operation entry catches Rust unwinds and returns a content-free status. The only `unsafe_code = "allow"` exception belongs to generated `cxx::bridge` expansion and is pinned by `crates/safeparts_desktop_bridge/scripts/policy_guard.sh`; handwritten operation code remains `forbid(unsafe_code)`.

## Implemented slice

- Split UTF-8 text into V2 unprotected Words shares, with 2-of-3 defaults and valid minimum/total-share customization. Text admission converts CRLF and lone CR to LF. It does not trim or apply Unicode normalization.
- Keep the admitted Secret visible above read-only, selectable generated Recovery share text. Encode the views sequentially on the worker; if every share cannot fit the presentation budget, clear the set and report a handled resource status instead of showing partial or truncated output. Editing the Secret or split settings clears and hides generated text immediately, rejects stale preview results, and resets the Rust operation before another Split.
- Copy any displayed Recovery share with its icon-only control; Copy requests a fresh authoritative encoding from Rust.
- Start Combine with two numbered, bounded Recovery share editors. Users can add or remove fields between the two-field minimum and 255-field maximum. A 250 ms debounce sends the complete nonempty field set to the worker; empty required fields remain visible and block Combine.
- Reject malformed, trailing, duplicate, mixed, unsupported, over-count, and over-limit input without filtering a subset.
- Reject stale success and error results by generation after edits, mode changes, Start over, and close.
- Preserve NUL, NBSP, U+2028, U+2029, supplementary-plane and composed/decomposed Unicode, whitespace, and leading/trailing newlines in the editor-owned model. The recovered widget is presentation only; Copy reads the authoritative Rust result.

Files, passphrases, other encodings, V1 input, save, print, packaging, and release-platform qualification remain outside this slice.

## Clipboard acquisition blocker

The development-host adapter calls `-[NSPasteboard dataForType:]`, which returns a complete `NSData`. `NSPasteboardItem` type discovery reports available types but exposes payload bytes through the same complete-data call; AppKit has no supported byte-length query, ranged read, or stream for a string pasteboard item. The fallback Qt interface, `QMimeData::data()`, likewise returns a complete `QByteArray`. The adapter can avoid creating a Qt `QString` until after checking `NSData.length`, but it cannot enforce the 16 MiB limit before AppKit materializes the payload. That check is intentionally documented as post-acquisition and does not satisfy the required pre-materialization clipboard gate.

## Development-host evidence

Evidence was collected on Apple-silicon macOS 15.5 (24F74), Qt 6.9.1 from `/opt/homebrew`, CMake 4.0.1, Apple Clang 17, Rust/Cargo 1.93.0 through `mise exec --`, and the owner-approved CXX 1.0.195 security update. RustSec reports no findings for the locked Cargo graph; no advisory exception was added. Qt 6.9.1 is only the installed development runtime; it does not replace the approved Qt 6.11 evaluation and packaging target.

Artifacts under `target/desktop-evidence/` were captured at commit `9851d0acfce8958cd698d51d3b42b087dddfce0f` after the Split/Combine layout and visible-share changes. The later complete-visible-set worker fix changes the observed executable and test workload, so recapture these artifacts at the final reviewed commit before acceptance.

- `versions.txt`: exact host and tool versions.
- `network.csv`: five one-second, PID-filtered `nettop` samples during the synthetic Qt create/copy/recover/reset/close suite. The observed process had no TCP/UDP rows or bytes in those samples.
- `actions.stdout`: the twelve passing Qt action/lifecycle phases used during the observation, including editable recovery fields, visible generated shares, and secure queued-buffer final-owner wiping.
- `closure.stderr` and `otool.txt`: loader and direct-link closure. The observed Qt images were Core, Gui, Widgets, DBus, the Cocoa platform plugin, the macOS style plugin, and Test for the test executable, all from Qt 6.9.1.
- `lsof-snapshots.txt`: supplemental open-file snapshots during the same flow.
- `fs-usage.stdout` and `fs-usage.stderr`: an owner-approved raw `fs_usage` trace containing several processes. `fs-usage.desktop-actions.txt` retains the 3,672 target-process rows, and `fs-usage-analysis.json` records the raw trace hash and operation counts. Review found no path-bearing file writes, renames, unlinks, directory creation, truncation, or sync calls. Test output went to stdout; one-byte writes used an unpathed runtime descriptor. The runtime made one local datagram connection to `/private/var/run/syslog` during startup; sampled `nettop` output contained no TCP/UDP target-process rows or bytes. Qt/macOS opened the OS internationalization cache read-write, but the trace shows no write to that path. This is development-host evidence, not a guarantee for other hosts or future runtime versions.

The public operation boundary test admits a 1 MiB Secret at 16 shares while exercising the 16 MiB paste, token, and 160 MiB retained-input bounds. The current Qt capacity action covers that Secret path, a 1 MiB visible Recovery share, configurable pre-mutation editor bounds, and reset. `/usr/bin/time -l` reported a 158,797,568-byte peak memory footprint and a 348,602,368-byte maximum resident-set metric for that action. This is not the exhaustive maximum editable-field workload certificate; issue 147 still owns reproducible maximum-workload RSS/private-footprint and allocation-failure certification. Disconnected repetition, Qt 6.11, and Windows/Linux platform rows also remain unverified.

## TDD trace

- Red: `mise exec -- cargo test -p safeparts_core --test desktop_admission` failed because `inspect_share_set` did not exist (`/tmp/desktop140-core-red.log`). Green: the same test passed after core-owned inspection and version-preserving parsing were added.
- Red: `mise exec -- cargo test -p safeparts_desktop_bridge --test operation` failed because the operation module did not exist (`/tmp/desktop140-bridge-red.log`). Green: public-interface tests now pass for exact Unicode, admission/memory envelopes, trailing content, mixed sets, unsupported encoding, and unsupported V1 input.
- Red: strict CMake compilation first exposed mixed Qt 6.8/6.9 headers, then the Qt suite exposed an incorrect second create action. Green: CMake now binds headers to the selected Qt package and `ctest --test-dir target/desktop-build --output-on-failure` passes both generated-CXX and Qt action suites.
- Red: the Qt action suite preserved CR and did not cover modified-key text, replacement-only IME edits, or editor paste/cut model parity. Green: text admission uses LF and all covered mutation paths keep displayed text and authoritative bytes equal.
- Red: the created-share state replaced and cleared the source form, lacked row separators, and inherited malformed global button styling. Green: the compact native layout retains source text, separates on-demand share rows, and invalidates the generated state after edits.
- Red: the native labels diverged from the web UI, recovery accepted only opaque clipboard batches, copy controls used text, and the selector painted a focus border. Green: Split/Combine terminology, icon-only copy controls, bounded editable Recovery share fields, atomic worker replacement, and borderless keyboard switching pass Qt action tests.
- Red: Split results showed only `Recovery share N` placeholders. Green: actual Words text is encoded sequentially on the worker, displayed read-only under all-or-none checked admission, copied from fresh Rust output, and cleared on edits or stale completion.
