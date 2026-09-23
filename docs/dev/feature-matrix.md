# Feature matrix

Update this file when a feature is added, removed, exposed on a new surface, or intentionally left out of a surface.

Status keys: `Yes` means implemented and exposed; `Core` means implemented in core but not exposed here; `No` means not implemented; `Planned` means expected future work; `N/A` means not relevant.

## Current coverage

Supported surfaces are core, CLI, TUI, WASM, web, and help. Retired applications and their dedicated UniFFI bridge are dormant reference, outside feature coverage and parity requirements. CLI/TUI archives support Linux, macOS, and Windows.

| Feature | Core | CLI | TUI | WASM | Web | Help docs | Tests | Update when changed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Split/combine arbitrary bytes | Yes | Yes | Yes | Yes | Text input; recovered output requires valid UTF-8 | Yes | Rust, headless TUI, WASM-target, CLI e2e, web recovery smoke | Supported surface guides and text-only recovery guidance |
| Split clipboard copy is one Recovery share at a time | N/A | N/A | Yes | N/A | Yes | Yes | Headless TUI clipboard writes, web browser clipboard writes | TUI, web, and help docs |
| Released Safeparts V1/V2 Share packet decoding | Yes | Yes | Yes | Yes | Yes | N/A | Immutable core fixtures for every concrete Share encoding, metadata, exact bytes, and Passphrase protection | Core packet decoder, compatibility corpus, and core guide |
| Web immediately readable, exactly selectable results | N/A | N/A | N/A | Unchanged | Yes; state-backed Copy buttons and recovered-Secret shortcut | Unchanged | Browser Selection, clipboard writes, synthetic 4 KiB round trip, English/Arabic and motion preferences | Web output/copy contract and web guide |
| Split result provenance | N/A | N/A | N/A | N/A | Yes | N/A | Rendered Web workflows and browser clipboard/status checks | Web result state and tests |
| CLI/TUI private atomic file output | N/A | Yes; rejects NUL-containing destinations before writing | Yes; rejects NUL-containing destinations before writing | N/A | N/A | N/A | CLI e2e and TUI file tests, including NUL-path rejection and Windows plus one native macOS Rust CI host job on relevant PRs | CLI and TUI guides |
| TUI Split/Recover result lifetime | N/A | N/A | Input changes and new attempts clear output; focus and no-op edits preserve it | N/A | N/A | N/A | Headless key events, clipboard reads/writes, file load/export/save, and protected-export recovery | TUI state contract and guide |
| TUI editor-owned text and navigation | N/A | N/A | Yes; F1 help, Alt+Left/Right operations | N/A | N/A | English + Arabic shortcuts | Headless key events, exact Secret/passphrase values, cursor navigation, focused controls, rendered shortcuts | TUI interaction contract, surface guide, and bilingual TUI help |
| TUI file-error recovery without session loss | N/A | N/A | Yes; failed loads retain input, failed saves retain output, same-session retry | N/A | N/A | Yes | Headless keyboard/file-outcome and rendered status tests | TUI guide and help |
| TUI terminal-state restoration | N/A | N/A | Yes | N/A | N/A | N/A | Injected setup, cleanup, and panic-path tests | TUI guide |
| Threshold range `1 <= k <= n <= 255` | Yes | Yes | Yes | Yes | Yes | Yes | Core packet metadata and mutation properties, boundary negative tests | Core, CLI, TUI, WASM, web |
| BLAKE3 integrity tag | Yes | Yes | Yes | Yes | Yes | Yes | Core corruption and deterministic mutation properties | Core and technical docs |
| Recovery-share error redaction | Yes | Yes | Yes | Yes | Yes | N/A | CLI, TUI, and WASM sentinel tests | Core and surface error mappings |
| Localized coded recovery guidance and numbered accessible inputs | N/A | N/A | N/A | Stable recovery codes and safe numeric fields | English + Arabic | English + Arabic troubleshooting | Public WASM error/redaction tests; real-WASM browser recovery and axe checks in both locales | WASM contract, web error mapping, recovery labels, help guidance |
| Passphrase protection | Yes | Yes | Yes | Yes | Yes | Yes | Core, headless TUI, WASM-target, CLI e2e, web masking/confirmation/encrypted-round-trip smoke | Security docs and every exposed UI |
| `base64url` share encoding | Yes | Yes, alias `base64` | Yes | Yes | Yes | Yes | Core deterministic encoding properties, WASM-target, CLI e2e | Encoding lists and UI choices |
| `base58check` share encoding | Yes | Yes, alias `base58` | Yes | Yes | Core | Yes | Core deterministic encoding properties, WASM-target, CLI e2e | Feature exposure notes if web adds it |
| `mnemo-words` share encoding | Yes | Yes | Yes | Yes | Yes | Yes | Core canonical framing and mutation properties, WASM-target, CLI e2e, web smoke | Encoding docs and UI choices |
| `mnemo-bip39` share encoding | Yes | Yes | Yes | Yes | Core | Yes | Core frame-order and mutation properties, WASM-target, CLI e2e | Feature exposure notes if web adds it |
| CLI mnemonic files loaded into TUI | Yes, strict line-or-paragraph parser | Produces one Recovery share per line | Yes, Auto and explicit; LF/CRLF; wrapped shares separated by blank lines | N/A | N/A | Yes, English/Arabic TUI guidance | Real CLI output through headless TUI file loading (Threshold and full file); core framing and TUI rejection tests | Core parser contract, TUI guide, help docs |
| Auto encoding parse | Yes | Yes for combine | Yes for combine | Yes | Yes for combine | Yes | Core whitespace cases, headless TUI recovery, WASM-target inspection, CLI e2e, web smoke | Encoding parser docs and UI guidance |
| Browser writing-assistance restrictions on sensitive inputs | N/A | N/A | N/A | N/A | Yes, Secret, Recovery shares, and passphrases | English/Arabic mitigation limits | Rendered browser attributes, dynamic fields, exact Unicode/clipboard input, and encoding detection | Web inputs, security help, and web surface guide |
| Safe deferred Threshold/Share count touch-focus selection | N/A | N/A | N/A | N/A | Yes | N/A | Coarse-pointer browser replacement, blur/unmount, bounds/steppers, result invalidation, fine-pointer caret, English/Arabic | Web focus handler and browser tests |
| Web local-only workflow | N/A | N/A | N/A | Yes | Yes | Yes | Web build and smoke tests | Web surface guide |
| Docker Web self-hosting | N/A | N/A | N/A | Yes | Yes | Yes | Clean image build, offline HTTP routes, headers, runtime user, and health failure smoke | Dockerfile, Nginx, deployment guide, and Web CI |
| Saved-backup setup checkpoint (guidance, not a product feature) | N/A | Existing commands | N/A | N/A | N/A | Yes, English + Arabic | Synthetic documented CLI rehearsal: saved files, every share, Passphrase failure, byte mismatch, correction, cleanup | Help setup/security pages and CLI automation manual |
| Predictable help navigation | N/A | N/A | N/A | N/A | App session behavior unchanged | Same-tab references; localized, announced new-tab app links | Bilingual link attributes, accessible names, popup opener isolation, help-page retention | Help components, app-entry labels, docs browser tests |
| English + Arabic user docs | N/A | N/A | N/A | N/A | Links to help | Yes | Docs build, docs a11y route parity | Help docs guide |
| Generated main/release changelog | N/A | N/A | N/A | N/A | Localized footer link | Full English/Arabic pages and root Markdown | Isolated Git/release fixtures, writer policy, rendered docs, footer navigation/input retention and accessibility | Generator, CI handoff, tracked-output policy and web/help guides |
| Combined local static-site build | N/A | N/A | N/A | Rebuilt first | App built before help | English and Arabic in `web/dist/help/` | Task graph, destructive-writer rehearsal, final-route checks, actual combined builds | Web/help build contracts and verification guide |
| Release CLI/TUI archives | N/A | Yes | Yes | N/A | N/A | Yes | Release workflow, package script, and relevant-PR Windows/native macOS CLI/TUI behavior jobs | Release guide; retain Linux/macOS/Windows host support |
| Release input and permission policy | N/A | N/A | N/A | N/A | N/A | N/A | Workflow policy unit and repository tests, actionlint | Release workflow, version sources, and release guide |
| QR export | No | No | No | No | Planned | Planned | None yet | Add proposal, UI tests, docs |
| Web `base58check` and `mnemo-bip39` exposure | Core | Yes | Yes | Yes | Planned | Planned | Add web boundary tests | Web, help docs |
| Compatibility import/export | Planned | Planned | Planned | Planned | Planned | Planned | None yet | Add spec and migration notes |

## Experimental desktop coverage

The issue 140 app is a reviewable native development slice, not a supported release surface. Its accepted visual source is [issue 140 comment 5747267572](https://github.com/connectedloops/safeparts/issues/140#issuecomment-5747267572); the historical throwaway fixture remains on `prototype/desktop-reference-ui` at `874c5e7` and is not production source.

| Capability | Experimental Qt desktop | Evidence | Deferred or unverified |
| --- | --- | --- | --- |
| Text and file Split/Combine | V2 output in Base64url, Base58check, Words, or BIP-39, optionally protected by a confirmed passphrase; text uses UTF-8/LF and one selected file uses exact bytes; Auto/manual V1/V2 recovery accepts pasted or loaded share text | Public Rust and generated CXX cover protected/unprotected binary, valid empty recovery, and exact UTF-8 byte forms; Qt actions cover protected file Split/Combine and exact Save; immutable binary fixtures cover desktop, CLI, TUI, core, and generated real WASM in every encoding; issue 143 adds development-host network, storage, and memory observations | Issue 147 capacity certification, issues 148 through 151 platform qualification, and any web-UI binary/protected-file workflow |
| Desktop direct file export | Per-share Save requests fresh encoding of retained packets; every recovery has Rust-authoritative exact-byte Save; valid UTF-8 additionally has inert display and Copy | Bounded adapter tests cover stream growth and open/read/write/flush/close failures; Qt covers cancellation, transactional import, stable repeated Copy/Save, failed-write retry, and stale-export rejection; the owner-approved issue 143 trace covers cancelled, failed, share, and recovered-file destinations | Direct writes deliberately provide no atomic rollback |
| Recovery share entry | Two to 255 numbered, bounded editable fields; Add/Remove and multi-file Load controls; core Auto detection or manual encoding; detected minimum expands fields; core metadata determines readiness; protected shares require a passphrase | Qt actions cover all four encodings, Auto/manual selection, threshold endpoints, reordered and multi-share fields, complete loaded batches, empty/invalid/unreadable file rejection, malformed/duplicate/mixed/unsupported input, correction, invalidation, reset, secure queued-buffer release, and protected passphrase preservation | Full platform qualification |
| Processing lifecycle | One worker, explicit Combine, immediate inspection, generation-based stale-result rejection, Start over/mode/close discard; cancelled file dialogs preserve the operation | Qt actions cover stale protected/unprotected success and error, stale file-export rejection before write, passphrase invalidation and clearing, file-dialog cancellation, failed-export retry, reset, and close/reopen | Forced-termination host storage observation remains pending |
| Admission | 1 MiB Secret and passphrase, 16 MiB pasted/imported batch, 8 MiB generated share, 160 MiB retained input, token/share-count/logical-volume bounds, bounded actual file reads, conservative pre-parse decoded-storage accounting, and a checked 160 MiB four-times UTF-8 presentation budget; larger valid generated sets export on demand while retaining only the latest copied share in a virtualized exact selectable 64-byte-row view | Rust covers dense compact-input rejection, maximum counts, declared-255 subsets, compile-isolated one-shot resource failures, real maximum-policy Argon2 recovery, and separate maximum Base64url, Base58check, Words, and BIP-39 modes; file-adapter tests cover below/at/above and growing actual reads; Qt covers empty/oversized files, inclusive Secret/passphrase and Recovery limits, exact passphrase line endings, masking, over-limit mutation rejection, and named fresh-process memory gates | Full 160 MiB aggregate recovery evidence, allocator time-series reconciliation, Qt-side allocation failpoints, and the remaining exhaustive matrix are incomplete |
| Clipboard fidelity | Valid Unicode text survives the tested macOS action path; recovered Copy is a text handoff and may omit a UTF-8 BOM, while exact-byte fidelity belongs to Save. Qt owns bounded visible presentation text while Rust remains authoritative for validation, packets, generated Copy output, and recovered bytes | Qt action tests cover exact Unicode, generated-display versus fresh authoritative Copy equality, and bounded virtualized maximum-share selection, keyboard Copy, accessibility, palette, wiping, and heartbeat behavior | Pre-materialization admission is blocked because AppKit returns complete `NSData`; Windows/Linux acquisition qualification |
| Distribution | None | Qt 6.9.1 development-host build only | Qt 6.11 evaluation, packaging, licensing bundle, signing, and every release-platform row |

## Required update checklist

When a feature changes, answer these before closing the task:

- Which surface owns the source of truth?
- Should CLI, TUI, WASM, web, help docs, and release packaging expose it now, later, or never?
- Which tests prove the core behavior and each exposed boundary?
- Does any `AGENTS.md` contract need a new rule?
- Does a developer guide need a new workflow or warning?
- Does user-facing documentation need an approved update?
- Does the change affect generated artifacts or release assets?

## Common future feature paths

### New share encoding

1. Implement encode/decode in `safeparts_core`.
2. Add parser and detection rules if auto encoding should support it.
3. Add core round-trip and negative tests.
4. Expose through CLI and TUI if the encoding is stable.
5. Add WASM bindings only through the core encoding API.
6. Decide whether web exposes it.
7. Update this matrix, surface guides, and relevant user docs.

### New UI workflow

1. Start in the web UI if it is a browser product feature.
2. Add browser accessibility coverage for stable flows.
3. Test the WASM boundary when it changes.
4. Update this matrix and the web surface guide.

### New release artifact

1. Obtain a scope decision before adding a supported artifact.
2. Add the build or packaging path in scripts or release CI.
3. Document local verification in `verification.md` and `surfaces/release.md`.
4. Update checksums and artifact naming rules.
5. Update this matrix.
