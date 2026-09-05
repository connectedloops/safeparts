# Help docs

Owner: `web/help/`
Nearest contract: [`web/help/AGENTS.md`](../../../web/help/AGENTS.md)

## What belongs here

The help site is the user-facing Astro + Starlight documentation served under `/help/` in English and Arabic.

This developer guide is only for contributors working on that site.

## Change rules

- Apply the `humanizer` skill before finalizing user-facing prose.
- Keep English and Arabic route structures aligned.
- Preserve `/help/` base path behavior.
- Keep examples synthetic. Do not include real secrets or real share packets.
- Keep setup instructions linked to the bilingual saved-backup checkpoint in `it-devops-guide/break-glass.mdx`. Practice drills do not validate real saved Recovery shares; the checkpoint must precede working-copy retirement.
- Update `web/help/DOCS_MAP.md` for navigation or structure changes.
- Update `web/help/DOCS_STYLE.md` for style rules.

## Generated changelog

The English and Arabic changelog pages contain full main-branch and published-release history, also available in root `CHANGELOG.md`. Run `mise run changelog:generate` from the repository root to refresh all three. Do not edit the generated pages or translate commit subjects. Normal builds use the committed pages without Git or network access.

See [the generator guide](../../../scripts/dev/README.md#changelog-snapshots) for inputs, CI updates, and retry behavior.

## Useful checks

```bash
cd web/help
bun install --frozen-lockfile
bun run build
```

This standalone build replaces `web/dist/help/` with English and Arabic help but does not build the root app. A later app build deletes it. For a complete site, run `mise run web:build:site` or `bash web/scripts/build-site.sh` from the repository root. See [output semantics](../verification.md#output-semantics).

To execute the synthetic saved-backup rehearsal from both locale pages through the existing CLI, run from the repository root:

```bash
cargo build -p safeparts
PATH="${CARGO_TARGET_DIR:-$PWD/target}/debug:$PATH" python3 scripts/dev/test_backup_rehearsal.py
```

The check runs only synthetic data, requires Bash and standard Unix tools, and verifies fixed status output and temporary-file cleanup. It does not certify a production backup or its custody environment.

For route parity and accessibility coverage:

```bash
cd web
bun run test:e2e:full
```

## When help docs change

Update:

- both English and Arabic files when route or core guidance changes
- `DOCS_MAP.md` for page map changes
- `DOCS_STYLE.md` for style rule changes
- [`docs/dev/feature-matrix.md`](../feature-matrix.md) if feature coverage changes
