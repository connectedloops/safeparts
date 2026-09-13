# Branch protection rollout

Owners: repository owners
Nearest contracts: [docs AGENTS](../AGENTS.md), [developer docs AGENTS](AGENTS.md)

## Current boundary

The repository now has an always-reporting PR check named `verification gate`. It runs on every `pull_request`, even when path-filtered Rust or Web workflows do not start. The gate requires:

- `workflow policy and actionlint` from `.github/workflows/merge-gate.yml`
- `audit` from `.github/workflows/rustsec.yml`
- Rust jobs when Rust/workflow/support paths match the Rust CI filters: `rust`, `terminal behavior (windows)`, and `terminal behavior (macos-native)`
- Web jobs when Web/deployment/changelog paths match the Web CI filters: `container smoke` and `build, test, and package Web artifact`

Failed, cancelled, missing, pending-after-timeout, or unexpectedly skipped required jobs fail the gate. PR-only deployment jobs may skip without blocking because they publish only from main-capable runs.

This document prepares repository-rule changes only. Do not apply settings, install apps, add secrets, change bot trust, or create bypasses without explicit owner approval.

## Changelog writer compatibility

`.github/workflows/changelog.yml` publishes generated snapshots from trusted main context using `GITHUB_TOKEN`, a non-force push, and the existing Web workflow dispatch handoff. Token-generated pushes do not trigger ordinary push workflows, so the writer must keep its explicit Web handoff. If branch protection rejects the generated push, the writer fails and scheduled/manual repair can retry after owners adjust approved rules.

Owner-compatible options:

1. **Keep the existing writer and accept the shared bot boundary.** Require `verification gate` before PR merge and restrict direct human pushes, but leave the current `GITHUB_TOKEN` main write path available. GitHub Actions bot identity is shared across workflows, so do not treat a native actor bypass as workflow-scoped or file-scoped. This option preserves current changelog publication, but it does not prove that only `.github/workflows/changelog.yml` can update the three generated files.
2. **Route generated changelog snapshots through PRs.** Remove bot main pushes, have owners approve generated changelog PRs, and keep `verification gate` as the only required PR check. This is stricter but changes the approved writer model and needs separate owner acceptance.
3. **Use a separately approved, least-privilege GitHub App.** If owners need direct generated pushes plus narrower trust than the shared GitHub Actions bot, create a dedicated app or equivalent writer with explicit file, workflow, and validation controls. Do not create apps, secrets, bypasses, or repository rules from this ticket alone.

## Proposed required check

After approval, set the required status check to exactly:

- `verification gate`

Do not require path-filtered workflow jobs directly. Requiring `rust`, `container smoke`, or similar jobs as branch-protection checks can leave unrelated PRs pending forever when their workflows are intentionally filtered.

## Rollout

1. Confirm the latest default branch contains `.github/workflows/merge-gate.yml` and `scripts/dev/merge_gate.py`.
2. Run `mise run workflow:check` locally or review a passing run on the rollout commit.
3. Open a test PR with an irrelevant docs-only change and confirm `verification gate` succeeds without Rust/Web filtered jobs.
4. Open or update a test PR with Rust or Web paths and confirm `verification gate` waits for, then requires, the matching jobs.
5. With explicit owner approval, update repository branch protection or rulesets for `main` to require `verification gate` before merge.
6. Validate hosted enforcement with an owner-approved method: one PR with an applicable failing check must be blocked, and one valid PR must be mergeable.
7. Validate the approved changelog writer path still publishes snapshots and dispatches the Web handoff, or record the approved alternative.

## Rollback

1. Remove `verification gate` from required checks or disable the new ruleset requirement.
2. Do not delete workflows or scripts during rollback; leave evidence available for diagnosis.
3. Re-run the failed scenario and record whether rollback restored merges or changelog publication.
4. Re-enable only after the failing check-run evidence is understood and fixed.
