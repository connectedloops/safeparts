# AGENTS.md — Agent Entry Point

Keep this file short. It contains only the essentials and links to the deeper, task-specific guidance.

<!-- Begin DOX -->

# DOX framework

- DOX is highly performant AGENTS.md hierarchy installed here
- Agent must follow DOX instructions across any edits

## Core Contract

- AGENTS.md files are binding work contracts for their subtrees
- Work products, source materials, instructions, records, assets, and durable docs must stay understandable from the nearest applicable AGENTS.md plus every parent AGENTS.md above it

## Read Before Editing

1. Read the root AGENTS.md
2. Identify every file or folder you expect to touch
3. Walk from the repository root to each target path
4. Read every AGENTS.md found along each route
5. If a parent AGENTS.md lists a child AGENTS.md whose scope contains the path, read that child and continue from there
6. Use the nearest AGENTS.md as the local contract and parent docs for repo-wide rules
7. If docs conflict, the closer doc controls local work details, but no child doc may weaken DOX

Do not rely on memory. Re-read the applicable DOX chain in the current session before editing.

## Update After Editing

Every meaningful change requires a DOX pass before the task is done.

Update the closest owning AGENTS.md when a change affects:

- purpose, scope, ownership, or responsibilities
- durable structure, contracts, workflows, or operating rules
- required inputs, outputs, permissions, constraints, side effects, or artifacts
- user preferences about behavior, communication, process, organization, or quality
- AGENTS.md creation, deletion, move, rename, or index contents

Update parent docs when parent-level structure, ownership, workflow, or child index changes. Update child docs when parent changes alter local rules. Remove stale or contradictory text immediately. Small edits that do not change behavior or contracts may leave docs unchanged, but the DOX pass still must happen.

## Hierarchy

- Root AGENTS.md is the DOX rail: project-wide instructions, global preferences, durable workflow rules, and the top-level Child DOX Index
- Child AGENTS.md files own domain-specific instructions and their own Child DOX Index
- Each parent explains what its direct children cover and what stays owned by the parent
- The closer a doc is to the work, the more specific and practical it must be

## Child Doc Shape

- Create a child AGENTS.md when a folder becomes a durable boundary with its own purpose, rules, responsibilities, workflow, materials, or quality standards
- Work Guidance must reflect the current standards of the project or user instructions; if there are no specific standards or instructions yet, leave it empty
- Verification must reflect an existing check; if no verification framework exists yet, leave it empty and update it when one exists

Default section order:
- Purpose
- Ownership
- Local Contracts
- Work Guidance
- Verification
- Child DOX Index

## Style

- Keep docs concise, current, and operational
- Document stable contracts, not diary entries
- Put broad rules in parent docs and concrete details in child docs
- Prefer direct bullets with explicit names
- Do not duplicate rules across many files unless each scope needs a local version
- Delete stale notes instead of explaining history
- Trim obvious statements, repeated rules, misplaced detail, and warnings for risks that no longer exist

## Closeout

1. Re-check changed paths against the DOX chain
2. Update nearest owning docs and any affected parents or children
3. Refresh every affected Child DOX Index
4. Remove stale or contradictory text
5. Run existing verification when relevant
6. Report any docs intentionally left unchanged and why

## User Preferences

- Prefer Stagehand-style local browser automation over Playwright defaults for developer env/task setup.

## Child DOX Index

- `.agents/`: repository-local agent skills and skill support files.
- `crates/`: Rust workspace crates for core algorithms, CLI, TUI, and WASM bindings.
- `web/`: Vite + React web app, WASM build integration, browser tests, and docs child site.
  - `web/help/`: Astro + Starlight help docs served under `/help/`.
- `desktop/`: retired Tauri app, retained as dormant source reference.
- `macos/`: retired SwiftUI app, retained as dormant source reference.
- `windows/`: retired WinUI app and native tooling, retained as dormant source reference.
- `docs/`: internal repository docs for agents and developers.
- `openspec/`: OpenSpec project context, active/archived changes, and accepted specs.
- `scripts/`: repository automation scripts, including release packaging.
- `explainer/`: explainer media sources and generated media outputs.
- `mobile/`: dormant prototype/native artifacts, not a supported product.

Top-level files not covered by a child AGENTS.md remain owned here, including `README.md`, `CHANGELOG.md`, `PRD.md`, `CONTEXT.md`, `Cargo.toml`, lockfiles, and CI config under `.github/`.
<!-- end DOX -->

## Agent skills

### Issue tracker

Track work in GitHub Issues for this repository. See `docs/agents/issue-tracker.md`.

### Triage labels

Use the five canonical triage roles mapped in `docs/agents/triage-labels.md`.

### Domain docs

Use the single-context domain model in `CONTEXT.md` and relevant records under `docs/adr/`. See `docs/agents/domain.md`.

## Repo snapshot

- Rust Cargo workspace (edition 2024):
  - `crates/safeparts_core/` (core algorithms + encodings + optional encryption)
  - `crates/safeparts/` (CLI wrapper; binary: `safeparts`)
  - `crates/safeparts_tui/` (interactive terminal UI; binary: `safeparts-tui`)
  - `crates/safeparts_wasm/` (wasm-bindgen exports for the web UI)
- Web app: `web/` (Vite + React) which expects a WASM build step.
- Help/docs: `web/help/` (Astro + Starlight), deployed under `/help/`.

## Supported product scope

- Support core, CLI, TUI, WASM, web, and help. CLI/TUI remain supported on Linux, macOS, and Windows.
- Tauri, native macOS, native Windows, and their dedicated `crates/safeparts_uniffi/` bridge are retired. Retain their source as dormant reference; exclude them from supported setup, workspace builds, tests, coverage, releases, and UI parity requirements.
- Do not port active web changes into retired apps or restore their automation. A replacement or reinstatement requires a separate explicit decision.
- Retiring applications does not invalidate existing Recovery shares. Keep compatibility guarantees and direct users to supported recovery tools. Historical release assets remain unchanged.

## Context

- Product intent + security model: `PRD.md`
- Local dev commands + repo layout: `README.md`
- Contributor onboarding and DX workflow: `CONTRIBUTING.md`, `docs/dev/README.md`
- Local dev toolchain/env/task shortcuts: `mise.toml`
- Repository stewardship and creator attribution: `README.md#stewardship`

## Research

- Use Brave search using the API key in `.env`

## Checks (mirrors CI)

- Build / lint / test commands: `docs/agents/checks.md`, `docs/dev/verification.md`
- CI workflows (source of truth): `.github/workflows/rust-ci.yml`, `.github/workflows/rustsec.yml`, `.github/workflows/web-ci.yml`, `.github/workflows/release.yml`
- Dependency audit policy: `rustsec-policy.toml`; run `mise run audit`, and record owner, exposure, upstream constraint, and review date for every exception.
- For browser-based Web UI checks, use the `browser` skill / `browse` CLI in local mode (`browse env local`) instead of Playwright. Do not use Playwright for local smoke testing unless the user explicitly asks for it.

## Standards (do not change)

- Coding conventions + testing guidance: `docs/agents/conventions.md`

## Security-sensitive work

- Prefer small, reviewable PRs; keep unrelated refactors out of feature patches.
- Do not commit secrets or sample shares derived from real secrets.
- Treat share packets and reconstructed secrets as sensitive: minimize logging.

## Docs writing

- When editing or creating user-facing docs (for example `README.md`, `web/help/**/*.mdx`, and other end-user/contributor docs), load and apply the `humanizer` skill before finalizing the text.
- Do not apply this to internal instruction/tooling files (for example `AGENTS.md`, `openspec/**`, `.opencode/**`, or similar).

## Developer experience

- Keep developer-only guidance in `docs/dev/` and agent instructions in `AGENTS.md` / `docs/agents/`.
- Update `docs/dev/feature-matrix.md` when feature behavior changes across core, CLI, TUI, WASM, web, help docs, or release packaging.
- Local DX diagnostics: `mise run doctor`, `mise run dx:verify`.
- Keep full root `CHANGELOG.md` and both help changelogs generated together from main and published releases. Read `scripts/dev/README.md#changelog-snapshots` when changing their generator or CI. Only the configured CI writer may publish automatically; agent pushes still require explicit permission.
- `mise run verify` uses `web:build:site` for the complete static site; keep standalone app/help writers out of its parallel dependencies. Build ordering and final-route checks are owned by `web/scripts/`.
- Release workflow policy and action lint: `mise run workflow:check`.

## Release packaging

- Local release-style archives and platform ownership: `scripts/release/README.md`
- Release CI packages CLI/TUI archives for Linux, Windows, and macOS. GUI/native installers are not published. Keep checksum verification and tag-only publication intact.

## OpenSpec tooling

- `openspec/` and `.opencode/` contain OpenSpec artifacts/tooling; avoid editing unless the task is explicitly about specs/tool config.

## Git workflow

- Always commit atomically and meaningfully
- Write commit messages using conventional commits
- Prefer rebase over merge
- For major features, create a feature branch (feat/this-feature). Use kebab case branch names
- Never push automatically. Only push when explicitly ordered to
