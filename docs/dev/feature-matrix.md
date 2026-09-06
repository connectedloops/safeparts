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
| CLI/TUI private atomic file output | N/A | Yes | Yes | N/A | N/A | N/A | CLI e2e and TUI file tests | CLI and TUI guides |
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
| English + Arabic user docs | N/A | N/A | N/A | N/A | Links to help | Yes | Docs build, docs a11y route parity | Help docs guide |
| Generated main/release changelog | N/A | N/A | N/A | N/A | Localized footer link | Full English/Arabic pages and root Markdown | Isolated Git/release fixtures, writer policy, rendered docs, footer navigation/input retention and accessibility | Generator, CI handoff, tracked-output policy and web/help guides |
| Combined local static-site build | N/A | N/A | N/A | Rebuilt first | App built before help | English and Arabic in `web/dist/help/` | Task graph, destructive-writer rehearsal, final-route checks, actual combined builds | Web/help build contracts and verification guide |
| Release CLI/TUI archives | N/A | Yes | Yes | N/A | N/A | Yes | Release workflow, package script | Release guide; retain Linux/macOS/Windows host support |
| Release input and permission policy | N/A | N/A | N/A | N/A | N/A | N/A | Workflow policy unit and repository tests, actionlint | Release workflow, version sources, and release guide |
| QR export | No | No | No | No | Planned | Planned | None yet | Add proposal, UI tests, docs |
| Web `base58check` and `mnemo-bip39` exposure | Core | Yes | Yes | Yes | Planned | Planned | Add web boundary tests | Web, help docs |
| Compatibility import/export | Planned | Planned | Planned | Planned | Planned | Planned | None yet | Add spec and migration notes |

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
