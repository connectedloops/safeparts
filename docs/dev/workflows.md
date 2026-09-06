# Workflows

Use these checklists to keep future changes predictable.

## Add or change a feature

1. Read the nearest `AGENTS.md` files.
2. Identify the source of truth. Most behavior starts in `crates/safeparts_core/`.
3. For multi-surface work, copy [change-checklist.md](change-checklist.md) into your issue, PR, or task notes.
4. Update [feature-matrix.md](feature-matrix.md) with intended surface coverage.
5. Add the lowest-level tests first.
6. Update each exposed supported boundary: CLI, TUI, WASM, web, help docs, and release packaging.
7. Update the nearest `AGENTS.md` if contracts changed.
8. Update developer docs under `docs/dev/`.
9. Update user docs only when the change is user-visible and the task includes that scope.
10. Run relevant checks from [verification.md](verification.md).

## Fix a bug

1. Reproduce it with the smallest failing test or command.
2. Fix the source of truth, not only the front-end symptom.
3. Add a regression test near the failure boundary.
4. Check that errors do not echo share text, passphrases, or recovered secrets.
5. Run targeted checks, then the broader gate if practical.

## Integrate Safeparts from another Rust project

1. Start with [Rust library integration](manuals/rust-library.md).
2. Use `safeparts_core` instead of shelling out when you need typed errors, custom storage, or metadata inspection.
3. Keep storage and audit policy in the host application.
4. Add tests for successful recovery, insufficient shares, malformed input, and wrong passphrases if passphrase protection is enabled.
5. Do not log share text, passphrases, or recovered bytes.

## Automate the CLI

1. Start with [CLI automation](manuals/cli-automation.md).
2. Use synthetic secrets for CI drills.
3. Use `--passphrase-file` instead of `--passphrase` in automation.
4. Write recovered bytes to private files, not stdout.
5. Treat CI-based production recovery as a break-glass exception that needs a security review.

## Add a share encoding

1. Add the core encode/decode implementation and strict validation.
2. Add round-trip, malformed input, checksum, and auto-detection tests where relevant.
3. Add CLI/TUI support if the encoding is part of the Rust toolchain.
4. Add WASM support through the shared core encoding API.
5. Decide whether web exposes it.
6. Update `docs/dev/feature-matrix.md`, `docs/dev/surfaces/core.md`, and affected surface guides.
7. Update help docs only if the encoding becomes user-visible in a released surface.

## Change web UI behavior

1. Read `web/AGENTS.md` and `web/src/AGENTS.md`.
2. Keep split/combine local to the browser.
3. Build WASM before running the app.
4. Keep accessibility labels, live regions, keyboard behavior, and RTL support intact.
5. Update help guidance when user-visible behavior changes. Retired applications have no parity requirement.
6. Run web build, typecheck, and the relevant browser checks.

## Change Web deployment

1. Keep Rust, Bun, WASM tools, deployment CLIs, and GitHub actions pinned.
2. Install Web and help dependencies with `--frozen-lockfile`.
3. Build WASM, the application, and both help locales once.
4. Run the full checks against that output before preparing the artifact.
5. Run `python3 scripts/dev/test_web_deploy.py` and the credential-free package dry run in the [deployment guide](../deployment/web-artifact.md).
6. Keep Netlify and Cloudflare jobs independent, but make both download the same commit-named artifact.
7. Do not add provider build hooks, source rebuilds, or live installer commands.
8. Verify every configured provider serves the retained commit and content digests.

## Dormant application source

Tauri, SwiftUI, WinUI, and their dedicated UniFFI bridge are retired. Preserve their source and user-owned files as reference; supported feature work does not update or verify them. Existing native commands are not supported after workspace exclusion. A replacement requires a new decision; there is no plan or timeline.

## Change release packaging

1. Preserve CLI/TUI archives for Linux, macOS, and Windows.
2. Keep retired application installers and native bridge output outside future releases. Leave historical releases unchanged.
3. Run the CLI/TUI package checks and `mise run workflow:check`.
4. Verify archive layout, filenames, and the checksum manifest against the published asset set.
5. Preserve pinned inputs, read-only assembly, tag-only publication permissions, and artifact-only web deployment.
6. Update the release workflow, release guide, feature matrix, and public download guidance together.

## Change developer tooling

1. Keep scripts small and explicit about inputs and outputs.
2. Prefer read-only diagnostics for `doctor` and `dx:verify`.
3. Add commands to `mise.toml` when they are useful to most contributors.
4. Update `docs/dev/verification.md` and `docs/dev/troubleshooting.md` if the command changes how people work.

## Change docs or AGENTS

1. Keep public help docs and internal developer docs separate.
2. Apply the `humanizer` skill for contributor-facing prose.
3. Update child indexes when AGENTS files are added, removed, or moved.
4. Run `mise run dx:verify`.
