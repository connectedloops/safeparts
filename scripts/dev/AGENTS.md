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
- `test_web_deploy.py`: local build graph, destructive-writer rehearsal, final-route checks, deployment artifact behavior, and immutable Web workflow policy tests.
- `test_backup_rehearsal.py`: executes English/Arabic synthetic saved-backup examples through the CLI; checks success, failed verification, working-copy retention, re-verification, and cleanup.
- `workflow_policy.py`: release workflow input, tag-only publication, and permission policy gate.
- `test_workflow_policy.py`: public-behavior tests for release pins, permissions, tag-only publication, and retired-workload rejection.
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

## Work Guidance

- Keep scripts dependency-free unless a task explicitly approves a new runtime dependency.
- Avoid network calls in diagnostics.
- Make checks pass from the repository root.

## Verification

- Run changed scripts directly with `python3`.
- Coverage automation: `python3 scripts/dev/test_rust_coverage.py` and `mise run coverage`.
- RustSec automation: `python3 scripts/dev/test_rustsec_audit.py` and `mise run audit`.
- Web deployment policy: `python3 scripts/dev/test_web_deploy.py`.
- Backup rehearsal: build `safeparts` with Cargo and put the built binary on `PATH`, then run `python3 scripts/dev/test_backup_rehearsal.py`. Requires Bash and standard Unix tools; accepts no production data.
- Release/workload policy: `mise run workflow:check` (includes retirement boundary, coverage-filter, changelog fixture, and changelog workflow tests). Docker input tests stage the Dockerfile's Cargo COPY inputs and run locked offline metadata; they do not replace the full image smoke test.
- Changelog regression tests: `python3 scripts/dev/test_changelog.py` and `python3 scripts/dev/test_changelog_workflow.py`.
- Run `mise run dx:verify` when changing DX checks.

## Child DOX Index

- No child AGENTS.md files yet.
