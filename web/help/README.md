# Safeparts Help (Starlight)

This is the Safeparts help website, built with Astro Starlight.

- Source: `web/help/`
- Deployed paths: `/help/` (English), `/help/ar/` (Arabic)
- Build output: `web/dist/help/` (configured in `web/help/astro.config.mjs`)

## Local dev

Complete the [onboarding prerequisites](../../docs/dev/onboarding.md#2-install-tools) first. Run this in Bash from the repository root:

```bash
cd web/help
bun install --frozen-lockfile
bun run dev
```

Open:

- `http://localhost:4321/help/`
- `http://localhost:4321/help/ar/`

## Build

From a fresh shell at the repository root:

```bash
(cd web/help && bun run build)
```

This builds only the help site into `web/dist/help/`. For the app and bilingual help together, run `mise run web:build:site` from the root. Do not run the standalone app build afterward: it clears the help output. See [output semantics](../../docs/dev/verification.md#output-semantics).
