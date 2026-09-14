# Verification

Use the smallest check that proves your change, then run the broader gate before a PR when practical. Supported verification covers core, CLI, TUI, WASM, web, and help, plus CLI/TUI archives for Linux, macOS, and Windows. Retired applications and their dedicated UniFFI bridge have no build, test, coverage, dependency, packaging, or parity gate.

## One-command checks

```bash
mise run doctor          # local environment diagnostics
mise run dx:verify       # docs, AGENTS, lockfile, and generated-artifact checks
mise run workflow:check  # workflow policy/support tests and actionlint
mise run verify          # local aggregate gate (scope below)
```

`mise run verify` runs exactly these task groups from `mise.toml`:

| Included group | Checks |
| --- | --- |
| `verify:rust` | Rust format, Clippy, and all-feature workspace tests |
| `web:build:site` | WASM build, app typecheck, app build, bilingual help build, required output routes |
| `dx:verify` | Documentation/AGENTS and developer-experience consistency checks |
| `workflow:check` | Offline workflow-policy/support fixtures and actionlint |

It is not a claim of full CI equivalence. Run additional checks for the affected surface:

| Not included in `verify` | Entry point |
| --- | --- |
| Rust coverage and floors | `mise run coverage` |
| Independent live RustSec audit | `mise run audit` |
| Live supported dependency scan | `mise run security:scan` (see [identity limitations](dependency-scans.md#known-bun-identity-limitation)) |
| WASM browser bindings | `(cd web && bun run test:wasm)` |
| Browser end-to-end and accessibility | [Built-site recipe](#built-site-browser-suite) below |
| Docker image/runtime smoke | `bash web/tests/container-smoke.sh` |
| Native Windows/macOS CLI/TUI behavior | Host jobs in `.github/workflows/rust-ci.yml` |

Complete [onboarding](onboarding.md#2-install-tools) before these commands. Browser suites need both web/help dependencies, generated WASM, and the browser install step below.

`mise run workflow:check` runs the lightweight workflow-policy, coverage-filter, RustSec-classifier, changelog, and actionlint checks. Rust CI publishes the matching stable check as `workflow policy and actionlint`; branch-protection rules are managed outside this repository. Relevant Rust, dependency, and workflow pull requests also run `cargo test -p safeparts --all-features` and `cargo test -p safeparts_tui --all-features` on pinned Windows and one native macOS runner. GitHub records the hosted job timings; documentation-only changes outside the Rust workflow path filters do not start those host jobs.

## Dependency reports

Run `mise run security:scan` for the supported Cargo, web and help lockfiles, including development dependencies. It writes JSON and a summary under `target/security/supported/`; vulnerabilities exit 1, while incomplete scans and operational failures exit 2. Keep `mise run audit` for the independent RustSec policy.

See [dependency scans](dependency-scans.md) for tool pins, database freshness, exact scope, artifacts and the separately invoked retired-source report. The offline orchestration fixtures run with `python3 scripts/dev/test_dependency_scan.py` and are included in `mise run workflow:policy`. Live scans are explicit local commands, not part of `mise run verify`.

## Rust

```bash
mise run fmt-check
mise run lint
mise run test
```

Direct commands:

```bash
cargo fmt --all -- --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
```

Coverage:

```bash
mise run coverage
```

This is the same production-only line metric used by Rust CI. It stops counting a source file at its first `#[cfg(test)]` section and excludes trivial launch shims. The gate requires 70% overall, with floors of 90% for core, 75% for CLI, and 50% for TUI. WASM line coverage remains informational because its browser tests do not currently produce a stable LLVM profile.

Reports are written to `target/coverage/`: `production-summary.json` is the floor result, `rust.lcov` is machine-readable coverage, and `html/index.html` is the browsable report. CI uploads the directory as the `rust-coverage` artifact and runs the headless Chrome WASM suite separately.

Dependency audit:

```bash
mise run audit
```

The audit fails on vulnerabilities, unexpected unsound or unmaintained advisories, stale exceptions, and expired reviews. `rustsec-policy.toml` is the exception source of truth. Each entry names the dependency path, practical exposure, upstream constraint, owner, and review date. The scheduled `rustsec audit` workflow also runs on every pull request, so a new advisory cannot wait for a lockfile change.

Targeted examples:

```bash
cargo test -p safeparts_core encoding::
cargo test -p safeparts_core --test share_compatibility
cargo test -p safeparts --test e2e explicit_dash_paths_use_stdin_and_stdout
cargo test -p safeparts_tui app::tests
cargo test -p safeparts_wasm
(cd web && bun run test:wasm)
```

### Released Share packet fixtures

The compatibility test decodes immutable synthetic V1 and V2 Recovery share sets and reconstructs exact Secret bytes. Its [fixture README](../../crates/safeparts_core/tests/fixtures/share_compatibility/README.md) records provenance and the extension checklist.

When reviewing a new packet version, require a new fixture directory for every released concrete Share encoding. Compare the literals against the tagged release source, check `SHA256SUMS`, and confirm the test uses public decode and combine APIs. Do not approve regenerated or reformatted fixtures for an older version unless an explicit migration decision requires that change.

Run:

```bash
cargo test -p safeparts_core --test share_compatibility
(
  cd crates/safeparts_core/tests/fixtures/share_compatibility
  shasum -a 256 -c SHA256SUMS
)
```

Then run the core security properties and the full Rust gate.

## Web app

Build the complete static site from the repository root:

```bash
mise run web:build:site
# Same command without the task runner, using installed tools:
bash web/scripts/build-site.sh
```

This builds WASM, type-checks the app, builds Vite, then builds English and Arabic help. It fails if any build fails or if `web/dist/index.html`, `web/dist/help/index.html`, or `web/dist/help/ar/index.html` is missing or empty. `mise run verify` uses this combined task rather than running the app and help builds as siblings.

### Output semantics

| Command | Output |
| --- | --- |
| `mise run web:build` | Rebuilds WASM, type-checks, and replaces `web/dist/` with the app only. Deletes previously built help. |
| `(cd web && bun run build)` | Same app-only output; expects generated WASM to exist and does not type-check. |
| `mise run docs:build` or `(cd web/help && bun run build)` | Replaces only `web/dist/help/` with English and Arabic help. Does not build or verify the root app. `bun run help:build` from `web/` is an alias. |
| `mise run web:build:site` | Replaces `web/dist/` with the complete app and help, then checks the required routes. |

Use the combined command for publishable local output, including after an app-only rebuild. Do not run standalone app/help builds concurrently with it: they share the same output directory. This command does not publish, create deployment evidence, or run browser tests.

Additional focused checks from `web/`:

```bash
bun install --frozen-lockfile
bun run build:wasm
bun run typecheck
bun run build
bun run test:wasm
python3 ../scripts/dev/test_web_deploy.py
```

### Development-server automated suite

After onboarding and a WASM build, `(cd web && bun run test:e2e:full)` starts Vite/Astro development servers unless `PLAYWRIGHT_BASE_URL` is set. A preceding build does not make that command test the built output. Install Chromium once with `(cd web && bun run test:a11y:install)`.

### Built-site browser suite

CI tests the combined `web/dist` artifact. To test the same kind of output locally, run from the repository root:

```bash
mise run web:build:site
(cd web && bun run test:a11y:install)
```

In a separate terminal at the root, keep a loopback-only static server running:

```bash
python3 -m http.server 4173 --bind 127.0.0.1 --directory web/dist
```

Then, from the root:

```bash
(cd web && PLAYWRIGHT_BASE_URL=http://127.0.0.1:4173 bun run test:e2e:full)
```

Stop the server with Ctrl+C afterward. Use a plain static server: Vite preview inherits the `/help` development proxy. Do not start retired desktop servers.

Use the [Web artifact deployment guide](../deployment/web-artifact.md) to prepare and dry-run the same credential-free provider package.

Use local browser automation through the project browser skill or `browse` CLI for manual web smoke checks. Playwright remains the CI test runner and should not be the default manual browser tool unless a task asks for it.

For Docker self-hosting, run the clean build and offline runtime smoke from the repository root:

```bash
bash web/tests/container-smoke.sh
```

This checks the root app, SPA fallback, English and Arabic help, help 404 behavior, a hashed asset, security and cache headers, the unprivileged runtime, and both healthy and unhealthy container states.

## Help docs

From `web/help/`:

```bash
bun install --frozen-lockfile
bun run build
```

For route parity and accessibility coverage, run the web test suites from `web/`.

## Dormant application reference

Tauri, SwiftUI, WinUI, and the dedicated UniFFI bridge retain source and tests as reference. Existing native commands are not supported after workspace exclusion. See the [reference notices](README.md#dormant-reference); supported changes do not require native binding refresh, copied-UI synchronization, or preview promotion.

## Release packaging

From the repo root:

```bash
mise run workflow:check
cargo test --all-features
cargo build --release -p safeparts -p safeparts_tui
python3 scripts/release/package.py --version 0.3.1
```

Release CI packages CLI/TUI archives for Linux, macOS, and Windows. Future releases exclude retired installers and native bridge output; historical releases remain unchanged. The assembly job generates one checksum manifest that lists only published assets by their release-page filenames. On `workflow_dispatch`, it uploads the complete result as a short-lived dry-run artifact instead of creating a GitHub Release. With explicit authorization for a remote dry run, run the full platform matrix with `gh workflow run release.yml --ref <branch> -f version=v0.3.1`.

The release workflow pins every third-party action to a reviewed commit SHA and records the action version in a comment. Rust follows `mise.toml`; ordinary Rust CI uses the same compiler pin, and release jobs use fixed Blacksmith runner labels. Bun/Node belong to web build and deployment tooling, not the Rust archive jobs. Repository permissions default to `contents: read`; only the tag-only `publish` job has `contents: write`. Follow the pin-review procedure in [`surfaces/release.md`](surfaces/release.md) before updating these inputs.

## DX checks

`mise run dx:verify` checks:

- AGENTS child indexes point to real paths.
- `docs/dev/feature-matrix.md` has required surface columns.
- Required surface guides and developer manuals are present.
- Bun package lock policy is not mixed with npm lockfiles.
- Generated artifact policy catches common drift.
- The Web release artifact and workflow follow the tested, immutable deployment policy.

## When to skip a check

If you skip a relevant check, record why in the PR or final handoff. Good reasons include missing host dependencies, a check that is unrelated to the changed surface, or a command that is too expensive for the current task. Do not hide failures.
