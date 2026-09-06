# Web app

Owner: `web/`
Nearest contracts: [`web/AGENTS.md`](../../../web/AGENTS.md), [`web/src/AGENTS.md`](../../../web/src/AGENTS.md)

## What belongs here

The web app is a Vite + React browser UI. Split and combine run locally through WASM. There is no backend for secrets.

It owns:

- React UI and browser interaction
- i18n and RTL behavior
- accessibility behavior
- WASM package integration
- browser smoke and accessibility tests

## Change rules

- Run `bun run build:wasm` before expecting split/combine to work locally.
- Keep Recovery share and Secret handling in memory. Do not add server calls for Split or Combine.
- Keep the changelog link in the footer rather than primary navigation. It follows the help URL and opens a separate, opener-isolated tab without discarding form input.
- Remove generated Recovery shares as soon as the Secret, Threshold, Share count, Share encoding, or Passphrase protection changes. A pending Split must not restore an invalid result.
- Preserve keyboard access, live-region feedback, and labels when changing forms. Split success announcements may report the Recovery share count, but must not include Secret or Recovery share text.
- Derive cheap values during render. Use memoization only when computation cost or reference identity requires it.
- Keep generated modules and application boundaries typed instead of using file-wide type-check suppressions or `any` casts.
- Use local browser automation through the project browser tooling for manual checks. Playwright remains the CI runner.
- If a product UI change should exist in desktop, update desktop parity or record why not.
- Treat the tested `web/dist` plus help output as one release unit. Netlify and Cloudflare must consume the retained artifact instead of rebuilding source.

## Useful checks

```bash
cd web
bun install --frozen-lockfile
bun run build:wasm
bun run typecheck
bun run build
bun run test:wasm
bun run test:e2e:full
python3 ../scripts/dev/test_web_deploy.py
```

Use the credential-free package and dry-run commands in the [Web artifact deployment guide](../../deployment/web-artifact.md) before changing provider configuration.

## When web changes

Update:

- [`docs/dev/feature-matrix.md`](../feature-matrix.md)
- desktop copied UI files when parity applies
- `web/tests/` for stable workflow changes
- help docs only when user-facing guidance changes and the task includes that scope
