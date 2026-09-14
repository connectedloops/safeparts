# Dependency scans

From the repository root, install the pinned tools with `mise install`, then run:

```bash
mise run security:scan
```

You need Python 3.11 or newer, Trivy 0.74.0, Bun 1.3.11 and the repository's Rust toolchain. The command checks the current locked dependency graphs against Trivy's vulnerability database. It does not install JavaScript dependencies or build applications. Cargo's `metadata --locked --all-features` check can fetch registry metadata and crate sources; it neither upgrades the lockfile nor executes build scripts.

## Supported scope

| Graph | Products covered |
| --- | --- |
| `Cargo.lock` | Core, CLI, TUI and WASM, including platform and development dependencies |
| `web/bun.lock` | Web app, build tools, deployment tools and test dependencies |
| `web/help/bun.lock` | English and Arabic help, including build and development dependencies |

The script validates the supported Cargo workspace and its locked resolution. It checks each Bun lockfile's root dependency declarations against its `package.json`, including development dependencies. Manifest drift, missing files, symlinked inputs, unexpected workspaces and incomplete scanner inventories fail the command. Changes to workspace or override policy require an explicit scan-contract review.

Trivy receives a temporary tree containing only the three named lockfiles. It cannot discover `node_modules` examples, generated WASM packages, build outputs, or another application's dependencies. Tauri, native macOS, native Windows, the UniFFI bridge and the mobile prototype are outside this supported result. Agent tooling such as `.opencode`, explainer media and container base images are also outside this product dependency report. This is not a whole-repository, image, secret or source-code scan.

All severities, including unknown severity and findings without fixes, count. Development dependencies are included rather than suppressed: build and deployment tools can affect what we ship. The summary labels findings as `dev` only when Trivy marks the corresponding package that way; `other/unknown` is not a runtime-exposure assessment. Counts are package/advisory occurrences within each graph, not globally deduplicated advisory totals.

The script clears `TRIVY_*` environment settings and supplies an empty configuration and ignore file from its temporary working directory. Repository or personal Trivy filters, VEX settings and advisory ignore files do not change this gate. It has no advisory suppression list or severity threshold.

## Database and artifacts

Each invocation runs Trivy's database update check using a scope-specific cache. Trivy can reuse a database until its published `NextUpdate`; this is not a forced download on every invocation. The script requires database schema 2, `UpdatedAt` at or before the current UTC time, and `NextUpdate` after it. Missing, expired or future-dated metadata and update failures exit 2. Keep your system clock correct. After that check, the scan uses the same database with updates disabled.

Reports live under `target/security/supported/`:

- `report.json`: schema-versioned report with exact input and manifest SHA-256 hashes, tool version, database timestamps and SHA-256, start/end times, command outcomes, package inventories, findings and totals.
- `scanner.json`: Trivy's unmodified JSON response, when the scanner returned one. Consult `report.json` for the validated completion status.
- `summary.txt`: the same human summary printed to the terminal.
- `cache/db/`: the database and its metadata used for the scan.

Run one invocation per scope at a time. Each run replaces that scope's reports and invalidates previous success before calling tools. Archive the directory before rerunning if you need comparison evidence. The pinned scanner and input hashes make the scan procedure reproducible; fresh advisory data can change findings for unchanged lockfiles. Preserve the database with the report when you need the exact advisory snapshot. Generated reports and databases stay untracked under `target/`.

| Exit | Meaning |
| --- | --- |
| `0` | Every selected graph scanned successfully with no vulnerability findings. Retired coverage gaps still apply. |
| `1` | The scan completed and found one or more vulnerabilities at any severity. |
| `2` | The scan did not complete reliably: tool/version, input, resolution, database, timeout, scanner output or report-writing failure. |

An error report has `status: "error"`, `coverage_complete: false` and a null finding count, not zero. An interrupted process can leave the initial incomplete report; require the command's successful completion as well as the JSON status. If the report directory is unwritable, the command prints an error and exits 2. Treat an older report as stale.

A finding exit is not a fixture-test failure. Review the affected package/version and advisory in the JSON; do not change the scope or add ignores to make it green.

## Retired-source report

Invoke this separately; it is not part of default setup, verification or the supported vulnerability gate:

```bash
mise exec -- python3 scripts/dev/dependency_scan.py --scope retired
```

The report goes to `target/security/retired/` with the same artifact names and exit codes. It scans exactly `desktop/bun.lock` and `mobile/src-native/Cargo.lock`. It inventories retained manifests for `desktop/src-tauri`, `macos`, `windows` and `crates/safeparts_uniffi` as coverage gaps: these surfaces have no usable locked graph in the scan contract. Their generated bindings, installed dependencies and build outputs are not substitutes for a locked graph.

The report always labels retired coverage incomplete. Gaps alone do not fail this explicit report; findings exit 1 and operational failures exit 2. An exit 0 means only that the two scanned graphs had no findings, never that all retired sources are clean. The command does not resolve, restore, build, upgrade or reinstate retired applications. A new retained lockfile needs a reviewed inventory change before it enters the report.

## RustSec and verification

Keep running `mise run audit`. Its existing RustSec vulnerability, unsound/unmaintained warning and reviewed-exception policy remains independent and unchanged. A Trivy report does not replace it or import its exceptions.

Run the controlled CLI fixtures with:

```bash
python3 scripts/dev/test_dependency_scan.py
```

These tests exercise the real orchestration with temporary source trees and external-tool fixtures. They retain supported development findings, inject retired/example/generated poison inputs, and check invalid inputs, manifest/workspace drift, scanner failures, database freshness, report completeness and ambient suppression resistance. `mise run workflow:policy` includes these offline fixtures; live vulnerability scans remain explicit local commands.
