# AGENTS.md — Repository Docs

## Purpose

Owns internal repository documentation for agents and developers.

## Ownership

- `agents/`: coding-agent checks, conventions, and task notes.
- `dev/`: human developer onboarding, workflows, surface guides, and DX maintenance docs.
- `deployment/`: operational guides for supported deployments.

## Local Contracts

- Keep developer docs separate from end-user product docs in `README.md` and `web/help/`.
- Supported scope is core, CLI, TUI, WASM, web, and help; CLI/TUI archives support Linux, macOS, and Windows. Update `dev/feature-matrix.md` when supported behavior or release packaging changes.
- Retired Tauri, SwiftUI, WinUI, and dedicated UniFFI sources remain dormant reference. Their guides and old Tauri plans are notices, not build, test, packaging, parity, or promotion instructions. Preserve historical records and existing issues.
- Apply the `humanizer` skill before finalizing contributor-facing prose in `dev/`.

## Work Guidance

- Prefer short operational docs with commands, owners, and update rules.
- Link to source-of-truth files instead of copying long command lists.
- Do not document secrets, real share packets, or real passphrases.

## Verification

- Run `mise run dx:verify` when docs structure, AGENTS indexes, or developer workflow docs change.
- Run the nearest build/test command when a doc change describes a command or script.

## Child DOX Index

- `agents/`: agent-oriented checks, conventions, and durable task notes.
- `dev/`: contributor onboarding, workflows, feature coverage, and surface guides.
- `deployment/`: supported deployment setup and validation guides.
