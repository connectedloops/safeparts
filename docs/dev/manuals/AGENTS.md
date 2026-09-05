# AGENTS.md — Developer Manuals

## Purpose

Owns longer contributor manuals for integrating and automating Safeparts.

## Ownership

- `README.md`: manual index.
- `rust-library.md`: using `safeparts_core` from Rust projects and public API reference.
- `cli-automation.md`: safe CLI automation patterns, including CI scenarios.

## Local Contracts

- Manuals are contributor-facing. Do not replace end-user help docs.
- Keep examples synthetic and safe to copy into tests or local drills.
- CLI automation guidance links to the help site's actual saved-backup checkpoint; synthetic round trips do not certify custody copies. Keep working-copy retention on failed verification and temporary aggregation distinct from ongoing custody.
- Do not include real secrets, real recovery shares, passphrases, or recovered secret output.
- Apply the `humanizer` skill before finalizing prose.

## Work Guidance

- Check source code before documenting function signatures or command flags.
- Prefer complete examples over broad claims.
- Explain security boundaries near the examples that need them.

## Verification

- Run `mise run dx:verify` after changing this subtree.
- Run command-specific checks when changing executable examples.

## Child DOX Index

- No child AGENTS.md files yet.
