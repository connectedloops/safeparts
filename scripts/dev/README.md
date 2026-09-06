# Developer scripts

Run these from the repository root.

```bash
python3 scripts/dev/doctor.py
python3 scripts/dev/verify_dx.py
python3 scripts/dev/test_retirement.py
python3 scripts/dev/test_rust_coverage.py
python3 scripts/dev/rust_coverage.py
python3 scripts/dev/test_rustsec_audit.py
python3 scripts/dev/rustsec_audit.py
python3 scripts/dev/test_workflow_policy.py
python3 scripts/dev/workflow_policy.py
```

The saved-backup documentation rehearsal executes the English and Arabic Bash examples against a built CLI. It uses synthetic data only and checks both verification outcomes, correction, and cleanup:

```bash
cargo build -p safeparts
PATH="${CARGO_TARGET_DIR:-$PWD/target}/debug:$PATH" python3 scripts/dev/test_backup_rehearsal.py
```

Mise shortcuts:

```bash
mise run doctor
mise run dx:verify
mise run coverage
mise run audit
mise run workflow:check
```

## Changelog snapshots

Run `mise run changelog:generate` to refresh the full root `CHANGELOG.md` and the English and Arabic help changelogs. You need Git, Python 3, and authenticated GitHub CLI (`gh`) with permission to read public releases. The command fetches complete `origin/main` history and tags, captures all release API pages under `target/changelog/`, and writes only the three snapshots. Review and commit them together.

For repeatable generation without network access, supply a saved API response:

```bash
python3 scripts/dev/changelog.py --main-ref main --releases-json /path/to/releases.json
python3 scripts/dev/test_changelog.py
python3 scripts/dev/test_changelog_workflow.py
```

The JSON can be a release array or the page arrays from `gh api --paginate --slurp`. Use `--main-ref origin/main` for the remote-tracking branch. The generator rejects missing main, shallow history, and missing published-release tags; fetch complete history rather than substituting a feature branch. Normal help, offline, source-archive, and Docker builds use the committed pages and need neither Git nor GitHub access.

Each commit belongs to the earliest published eligible release that contains it. Releases appear newest publication first, with tag name breaking date ties. This also handles merged release branches, same-commit releases, and prereleases without duplicate entries. Later main commits appear under Unreleased. Dates come only from release metadata. Drafts, unpublished releases, and tags outside main history do not qualify. Original subjects remain unchanged, with punctuation escaped for Markdown and HTML.

`.github/workflows/changelog.yml` regenerates on main pushes, successful trusted `release` workflow completion, and scheduled/manual repair. It excludes only commits with the exact subject `chore(changelog): update generated history` whose changes are limited to the three generated paths. A concurrent main update rejects the non-force push; the next run regenerates from current main. After a successful generated push it dispatches `web-ci.yml` on main, which builds, tests, and deploys its existing verified artifact. Scheduled/manual runs also retry that dispatch if snapshots are unchanged. Only push or manual Web runs on main can deploy. The writer is restricted to `connectedloops/safeparts`; it never grants a job token to a fork or PR job.

The writer requests `contents: write` to commit snapshots and `actions: write` to dispatch the Web workflow, using `GITHUB_TOKEN` rather than a new secret. Repository policy must allow those permissions and bot pushes to main. If branch protection rejects the push, the workflow fails without bypassing it. A scheduled or manual changelog run retries generation and Web dispatch.

## Diagnostics and verification

The diagnostic scripts are read-only. The coverage runner writes LCOV, JSON, and HTML reports under `target/coverage/` and fails when a production-code floor is missed. The RustSec runner checks `Cargo.lock` against `rustsec-policy.toml` and rejects unreviewed or expired exceptions. The workflow policy check rejects mutable release actions, moving toolchains and runners, retired workloads and installers, and write permissions outside the publication job. `mise run workflow:check` also tests supported workspace/task/CI boundaries, release version inputs, checksum safety, and Docker Cargo inputs without requiring a Docker daemon.

Default setup and verification cover core, CLI, TUI, WASM, web, and help. The former desktop parity checker remains as dormant reference only; retired clients need not mirror supported features. Coverage retains the overall, core, CLI, and TUI floors and reports WASM separately.
