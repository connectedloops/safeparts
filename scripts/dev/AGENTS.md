# AGENTS.md — Developer Automation Scripts

## Purpose

Owns local developer-experience diagnostics and verification helpers.

## Ownership

- `doctor.py`: read-only local environment diagnostics.
- `verify_dx.py`: repository DX consistency checks.
- `check_desktop_parity.py`: dormant historical reference; not invoked by active tasks, doctor, DX checks, or CI. Divergence of retired clients is expected.
- `rust_coverage.py`: Rust coverage runner, production-code filter, report writer, and floor gate.
- `test_rust_coverage.py`: unit tests for coverage filtering and floor diagnostics.
- `rustsec_audit.py`: Cargo audit runner and exact policy-exception gate.
- `test_rustsec_audit.py`: unit tests for RustSec finding classification.
- `dependency_scan.py`, `test_dependency_scan.py`: explicit supported/retired locked-graph reports and controlled public-CLI fixtures.
- `test_web_deploy.py`: local build graph, destructive-writer rehearsal, final-route checks, deployment artifact behavior, and immutable Web workflow policy tests.
- `test_backup_rehearsal.py`: executes English/Arabic synthetic saved-backup examples through the CLI; checks success, failed verification, working-copy retention, re-verification, and cleanup.
- `workflow_policy.py`: release workflow input, tag-only publication, and permission policy gate.
- `install_actionlint.sh`: installs the reviewed Linux x86_64 actionlint release for CI policy jobs from the official release asset and checksum.
- `test_workflow_policy.py`: public-behavior tests for release pins, permissions, tag-only publication, and retired-workload rejection.
- `merge_gate.py`: stable PR gate evaluator for applicable check-run results and path-filtered workloads.
- `test_merge_gate.py`: fixture tests for relevant, irrelevant, failed, cancelled, missing, pending, and expected-skip gate outcomes.
- `test_ci_triggers.py`: Rust/Web/RustSec workflow trigger policy tests for PR-based feature-branch verification, main push verification, Windows/native macOS CLI/TUI behavior jobs, and recovery schedules/manual dispatch.
- `test_retirement.py`: active Cargo/task/CI boundaries, CLI/TUI release hosts and checksum safety, supported version manifests, and staged Docker Cargo inputs.
- `changelog.py`: main-history/release collector and renderer for the three committed changelog snapshots.
- `test_changelog.py`, `test_changelog_workflow.py`: isolated Git/release fixtures and main-only writer/artifact handoff guards; require supported WASM/web/help gates without retired desktop builds.
- `README.md`: local script usage notes, including changelog regeneration and CI permissions.

## Local Contracts

- Scripts must be deterministic and explicit about failures; generated reports belong under `target/`.
- Changelogs are intentional tracked outputs: root `CHANGELOG.md` and both help `changelog.md` pages. Generate them together; normal site builds consume the snapshots without Git or network access.
- Changelog history uses explicit main refs and published-release ancestry, never feature HEAD. Preserve original subjects as safely escaped text. Ignore only the fixed generated-only commit contract; generation must remain byte-identical after those commits.
- The changelog writer is restricted to this repository's main branch and the three explicit outputs. Use non-force pushes and the existing main-only Web dispatch; preserve its complete artifact verification/deployment gates.
- Do not print secrets, share text, passphrases, or reconstructed secrets.
- Prefer actionable messages that name the command or file to fix.
- DX checks require supported surface guides and the Feature/Core/CLI/TUI/WASM/Web/Help docs/Tests/Update when changed matrix. Retired guides remain notices, not supported implementation requirements.
- Coverage counts core, CLI, TUI, and WASM production source only; preserve overall/core/CLI/TUI floors. Active RustSec policy exceptions must match dependencies still in Cargo.lock.
- Dependency scans stage only named lockfiles after manifest/workspace validation. Keep Trivy/Bun versions aligned with `mise.toml`, include development dependencies and every severity, validate current database metadata, and fail closed on missing inputs or incomplete scanner results. Preserve the independent RustSec policy.
- Retired scan mode is explicit reporting only: inventory unresolved Tauri/native/UniFFI surfaces as incomplete coverage without restore/build steps. Coverage gaps alone do not fail this mode; vulnerabilities exit 1 and operational failures exit 2. Scope, artifacts and freshness policy are documented in `../../docs/dev/dependency-scans.md`.

## Work Guidance

- Keep scripts dependency-free unless a task explicitly approves a new runtime dependency.
- Avoid network calls in diagnostics. Explicit dependency scans may update the advisory database and validate supported Cargo resolution over the network.
- Make checks pass from the repository root.

## Verification

- Run changed scripts directly with `python3`.
- Coverage automation: `python3 scripts/dev/test_rust_coverage.py` and `mise run coverage`.
- RustSec automation: `python3 scripts/dev/test_rustsec_audit.py` and `mise run audit`.
- Dependency scan orchestration: `python3 scripts/dev/test_dependency_scan.py` (also in `mise run workflow:policy`). Use `mise run security:scan` for live supported findings; a finding exit is distinct from a fixture failure.
- Web deployment policy: `python3 scripts/dev/test_web_deploy.py`.
- Backup rehearsal: build `safeparts` with Cargo and put the built binary on `PATH`, then run `python3 scripts/dev/test_backup_rehearsal.py`. Requires Bash and standard Unix tools; accepts no production data.
- Release/workload policy: `mise run workflow:check` (includes retirement boundary, coverage-filter, RustSec-classifier, merge-gate, changelog fixture, changelog workflow, and actionlint tests). Docker input tests stage the Dockerfile's Cargo COPY inputs and run locked offline metadata; they do not replace the full image smoke test.
- Changelog regression tests: `python3 scripts/dev/test_changelog.py` and `python3 scripts/dev/test_changelog_workflow.py`.
- Run `mise run dx:verify` when changing DX checks.

## Child DOX Index

- No child AGENTS.md files yet.
