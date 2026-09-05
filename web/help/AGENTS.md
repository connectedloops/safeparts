# AGENTS.md — Help Site

## Purpose

Owns the Astro + Starlight documentation site served under `/help/` in English and Arabic.

## Ownership

- `src/content/docs/`: user-facing documentation pages.
- `src/content/docs/ar/`: Arabic documentation pages.
- `src/components/`, `src/styles/`, `src/assets/`: documentation presentation support.
- `DOCS_MAP.md` and `DOCS_STYLE.md`: local documentation structure and style references.

## Local Contracts

- Apply the `humanizer` skill before finalizing edits to user-facing docs.
- Keep English and Arabic documentation structures aligned when changing navigation or core user guidance.
- Preserve the `/help/` base path behavior.
- English/Arabic `changelog.md` pages are tracked generated Markdown, not hand-maintained MDX. Regenerate them with root `CHANGELOG.md` through `mise run changelog:generate`; see `scripts/dev/README.md#changelog-snapshots`. Preserve full history in both locales and original commit wording; localize only fixed presentation text.
- English/Arabic security guidance describes browser writing-assistance restrictions as a limited mitigation, not control over extensions, browser-provider features, or clipboard safety; do not imply an observed disclosure.
- Standalone builds replace `web/dist/help/`, not the root app. Use the combined build owned by `../scripts/AGENTS.md` for publishable output; a later app build deletes help.

## Work Guidance

- Prefer direct safety guidance over promotional language.
- Keep examples synthetic; never include real secrets or real share packets.
- Coordinate content structure changes with `DOCS_MAP.md` and style changes with `DOCS_STYLE.md`.

## Verification

- Install: `bun install`
- Dev server: `bun run dev`
- Build: `bun run build`

## Child DOX Index

- No child AGENTS.md files yet.
