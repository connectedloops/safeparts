# Feature matrix

Update this file when a feature is added, removed, exposed on a new surface, or intentionally left out of a surface.

Status keys:

- `Yes`: implemented and exposed.
- `Core`: implemented in core but not exposed here.
- `No`: not implemented.
- `Planned`: expected future work.
- `N/A`: not relevant for this surface.

## Current coverage

| Feature | Core | CLI | TUI | WASM | Web | Desktop | Native macOS | Native Windows | Help docs | Tests | Update when changed |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Split/combine arbitrary bytes | Yes | Yes | Yes | Yes | Text input; recovered output requires valid UTF-8 | Text input; recovered output requires valid UTF-8 | Yes, with exact file output | Yes, with exact file output | Yes | Rust, headless TUI, WASM-target, native bridges, Windows model/launch smoke, CLI e2e, web recovery smoke, Tauri adapter and command tests | All surface guides and text-only recovery guidance |
| Split clipboard copy is one Recovery share at a time | N/A | N/A | Yes | N/A | Yes | Yes | Yes | Yes | Yes | Headless TUI clipboard writes, web browser clipboard writes, native app copy seams | TUI, web, desktop, native apps, and help docs |
| Released Safeparts V1/V2 Share packet decoding | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | N/A | Immutable core fixtures for every concrete Share encoding, metadata, exact bytes, and Passphrase protection | Core packet decoder, compatibility corpus, and core guide |
| Web immediately readable, exactly selectable results | N/A | N/A | N/A | Unchanged | Yes; state-backed Copy buttons and recovered-Secret shortcut | Excluded from #88; animated mirror unchanged | N/A | N/A | Unchanged | Browser Selection, clipboard writes, synthetic 4 KiB round trip, English/Arabic and motion preferences | Web output/copy contract and web guide; desktop parity is a known scope conflict |
| Split result provenance | N/A | N/A | N/A | N/A | Yes | Yes | Yes | Yes | N/A | Rendered Web and Tauri workflows, browser clipboard/status checks, and native application-model tests | Web, desktop, and native application models |
| CLI/TUI private atomic file output | N/A | Yes | Yes | N/A | N/A | N/A | N/A | N/A | N/A | CLI e2e and TUI file tests | CLI and TUI guides |
| TUI Split/Recover result lifetime | N/A | N/A | Input changes and new attempts clear output; focus and no-op edits preserve it | N/A | N/A | N/A | N/A | N/A | N/A | Headless key events, clipboard reads/writes, file load/export/save, and protected-export recovery | TUI state contract and guide |
| TUI terminal-state restoration | N/A | N/A | Yes | N/A | N/A | N/A | N/A | N/A | N/A | Injected setup, cleanup, and panic-path tests | TUI guide |
| Threshold range `1 <= k <= n <= 255` | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Core packet metadata and mutation properties, Windows model, and native-bridge negative tests | Core, CLI, TUI, WASM, web, desktop, native apps |
| BLAKE3 integrity tag | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Core | Yes | Core corruption and deterministic mutation properties | Core and technical docs |
| Recovery-share error redaction | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | N/A | CLI, TUI, WASM, desktop, and native-bridge sentinel tests | Core and surface error mappings |
| Passphrase protection | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Core, headless TUI, WASM-target, native bridges, Windows model, CLI e2e, web masking/confirmation/encrypted-round-trip smoke, desktop command tests | Security docs and every exposed UI |
| `base64url` share encoding | Yes | Yes, alias `base64` | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Core deterministic encoding properties, WASM-target, native bridges, Windows smoke, CLI e2e, desktop command tests | Encoding lists and UI choices |
| `base58check` share encoding | Yes | Yes, alias `base58` | Yes | Yes | Core | Core | Yes | Yes | Yes | Core deterministic encoding properties, WASM-target, native bridges, Windows smoke, CLI e2e, desktop command tests | Feature exposure notes if web/desktop add it |
| `mnemo-words` share encoding | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Core canonical framing and mutation properties, WASM-target, native bridges, Windows smoke, CLI e2e, web smoke, desktop command tests | Encoding docs and UI choices |
| `mnemo-bip39` share encoding | Yes | Yes | Yes | Yes | Core | Core | Yes | Yes | Yes | Core frame-order and mutation properties, WASM-target, native bridges, Windows smoke, CLI e2e, desktop command tests | Feature exposure notes if web/desktop add it |
| CLI mnemonic files loaded into TUI | Yes, strict line-or-paragraph parser | Produces one Recovery share per line | Yes, Auto and explicit; LF/CRLF; wrapped shares separated by blank lines | N/A | N/A | N/A | N/A | N/A | Yes, English/Arabic TUI guidance | Real CLI output through headless TUI file loading (Threshold and full file); core framing and TUI rejection tests | Core parser contract, TUI guide, help docs |
| Auto encoding parse | Yes | Yes for combine | Yes for combine | Yes | Yes for combine | Yes for combine | Yes | Yes | Yes | Core whitespace cases, headless TUI recovery, WASM-target inspection, native bridges, Windows smoke, CLI e2e, web smoke, desktop command tests | Encoding parser docs and UI guidance |
| Web local-only workflow | N/A | N/A | N/A | Yes | Yes | N/A | N/A | N/A | Yes | Web build and smoke tests | Web surface guide |
| Docker Web self-hosting | N/A | N/A | N/A | Yes | Yes | N/A | N/A | N/A | Yes | Clean image build, offline HTTP routes, headers, runtime user, and health failure smoke | Dockerfile, Nginx, deployment guide, and Web CI |
| Desktop local-only Tauri workflow | N/A | N/A | N/A | N/A | Mirrored UI source | Yes | N/A | N/A | Yes | Desktop build, Tauri command tests | Desktop surface guide |
| Native macOS local-only workflow | N/A | N/A | N/A | N/A | N/A | N/A | Yes | N/A | N/A | Rust bridge and Swift model tests | Native macOS surface guide |
| Native Windows C# interoperability | N/A | N/A | N/A | N/A | N/A | N/A | N/A | Yes | N/A | Rust public-API tests and real-DLL C# smoke | Native Windows bridge and surface guide |
| Native Windows local-only workflow | N/A | N/A | N/A | N/A | N/A | N/A | N/A | Yes | N/A | Windows model, keyboard UI Automation, launch, accessibility source, and real-DLL smoke | Native Windows surface guide |
| Metadata-guided native recovery inputs | N/A | N/A | N/A | N/A | N/A | N/A | Yes | Yes | N/A | Swift and Windows model tests plus real bridge smoke | Native app models and UI |
| Native batch-export filename prefix | N/A | N/A | N/A | N/A | N/A | N/A | Yes | Yes | N/A | Swift and Windows filename tests | Native app models and UI |
| English + Arabic user docs | N/A | N/A | N/A | N/A | Links to help | N/A | N/A | N/A | Yes | Docs build, docs a11y route parity | Help docs guide |
| Generated main/release changelog | N/A | N/A | N/A | N/A | Localized footer link | Mirrored footer link | N/A | N/A | Full English/Arabic pages and root Markdown | Isolated Git/release fixtures, writer policy, rendered docs, footer navigation/input retention and accessibility | Generator, CI handoff, tracked-output policy and web/help guides |
| Release CLI/TUI archives | N/A | Yes | Yes | N/A | N/A | N/A | N/A | N/A | Yes | Release workflow, package script | Release guide |
| Release input and permission policy | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A | Workflow policy unit and repository tests, actionlint | Release workflow, version sources, and release guide |
| Tauri desktop installers | N/A | N/A | N/A | N/A | N/A | Yes, Linux and Windows | N/A | N/A | Yes | Release workflow | Release guide and desktop guide |
| Native macOS universal DMG | N/A | N/A | N/A | N/A | N/A | N/A | Yes, unsigned | N/A | Yes | macOS CI, package validator | Release guide and native macOS guide |
| Native Windows preview packages | N/A | N/A | N/A | N/A | N/A | N/A | N/A | Yes, unsigned x64 and ARM64 | Yes | Windows CI, exact-package launch, and package validator | Release guide and native Windows guide |
| QR export | No | No | No | No | Planned | Planned | N/A | N/A | Planned | None yet | Add proposal, UI tests, docs |
| Web `base58check` and `mnemo-bip39` exposure | Core | Yes | Yes | Yes | Planned | Planned after web | N/A | N/A | Planned | Add web and desktop parity tests | Web, desktop, help docs |
| Compatibility import/export | Planned | Planned | Planned | Planned | Planned | Planned | N/A | Planned | Planned | None yet | Add spec and migration notes |

## Required update checklist

When a feature changes, answer these before closing the task:

- Which surface owns the source of truth?
- Should CLI, TUI, WASM, web, desktop, help docs, and release packaging expose it now, later, or never?
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
6. Decide whether web exposes it. If web exposes it, desktop should follow.
7. Update this matrix, surface guides, and relevant user docs.

### New UI workflow

1. Start in the web UI if it is a product feature.
2. Keep desktop UI parity unless there is a documented desktop-only reason.
3. Add browser accessibility coverage for stable flows.
4. Add Tauri command tests when the desktop command boundary changes.
5. Update this matrix and the web/desktop surface guides.

### New release artifact

1. Add the build or packaging path in scripts or release CI.
2. Document local verification in `verification.md` and `surfaces/release.md`.
3. Update checksums and artifact naming rules.
4. Update this matrix.
