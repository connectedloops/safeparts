# AGENTS.md — Developer Experience Docs

## Purpose

Owns contributor onboarding, local workflow docs, feature coverage maps, and surface-specific development guides.

## Ownership

- `README.md`: developer-docs entry point.
- `onboarding.md`: first local setup path.
- `architecture.md`: repo map and data flow.
- `feature-matrix.md`: cross-surface feature coverage and future-update checklist.
- `workflows.md`: repeatable development workflows.
- `change-checklist.md`: multi-surface feature checklist template.
- `verification.md`: check matrix and local command policy.
- `generated-artifacts.md`: generated/tracked artifact policy.
- `troubleshooting.md`: common local setup and verification fixes.
- `surfaces/`: focused guides for each product or tooling surface.
- `manuals/`: longer guides for library integration and CLI automation.

## Local Contracts

- These docs are for contributors, not end users. Do not replace help-site content here.
- Update `feature-matrix.md` whenever a change adds, removes, or changes a feature surface.
- Keep every surface guide aligned with the nearest source `AGENTS.md` contract.
- Active workflows cover core, CLI, TUI, WASM, web, help, and CLI/TUI releases for Linux/macOS/Windows. Retired application and UniFFI guides are concise dormant-reference notices; omit obsolete commands and parity obligations.
- Apply the `humanizer` skill before finalizing prose edits.

## Work Guidance

- Write in direct second person where useful: say what to run and where to look.
- Prefer tables and checklists for coverage rules.
- Link to repo files with relative paths.

## Verification

- Run `mise run dx:verify` after changing this subtree.
- Run command-specific checks when changing command examples.

## Child DOX Index

- `manuals/`: long-form developer manuals for Rust library integration and CLI automation.
- `surfaces/`: active guides for core, CLI, TUI, WASM, web, help docs, and release; dormant-reference notices for desktop, macOS, Windows, and mobile.
