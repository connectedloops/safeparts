# Web app

Owner: `web/`
Nearest contracts: [`web/AGENTS.md`](../../../web/AGENTS.md), [`web/src/AGENTS.md`](../../../web/src/AGENTS.md)

The Vite + React app owns browser interaction, English/Arabic presentation, accessibility, and WASM integration. Split and combine run locally through core/WASM; there is no backend for secrets.

## Forms and results

- Build WASM before expecting split/combine to work. Keep generated modules and application boundaries typed.
- Keep sensitive state in memory. Invalidate stale results when inputs change; pending operations must not restore invalidated output.
- Render complete, selectable results immediately, preserving Unicode and whitespace. Copy from result state, not presentation markup. Split copies only individual shares.
- Preserve keyboard access, localized labels, and safe announcements without sensitive contents.
- Keep browser writing-assistance restrictions on all sensitive fields. They cannot guarantee trustworthy extensions, provider features, or clipboard handling.

The [source contract](../../../web/src/AGENTS.md) owns detailed state and input rules. Its linked tests cover exact Selection/clipboard output, synthetic 4 KiB input, both locales and motion preferences, and coarse-pointer numeric focus.

## Recovery errors

Use the [WASM error contract](wasm.md#recovery-error-contract), not exception prose. Keep distinct localized field names and safe fallback guidance. Invalid UTF-8 requires separate exact-file recovery guidance; never display lossy text as the recovered secret.

## Dependency updates

Read [the web dependency review](web-dependencies.md) before changing build or provider tools. Use the pinned Node runtime for provider commands and keep local and active CI pins synchronized. Preserve the separate help dependency graph and Vite's browser targets.

## Navigation and packaging

Keep the changelog in the footer. Help/changelog links preserve the app session in an opener-isolated tab. Supported web changes do not require retired-app parity; see [dormant references](../README.md#dormant-reference).

Treat the tested `web/dist` app/help output as one release unit. Netlify and Cloudflare consume the retained artifact without rebuilding source.

## Verification

Complete [onboarding](../onboarding.md#2-install-tools) first. From the repository root, `mise run web:build:site` builds the app before help and checks required output routes. Standalone app builds clear existing help; see [output semantics](../verification.md#output-semantics).

Focused checks from `web/`:

```bash
bun run build:wasm
bun run typecheck
bun run test:wasm
python3 ../scripts/dev/test_web_deploy.py
```

The **development-server automated suite**, `bun run test:e2e:full`, starts Vite/Astro unless `PLAYWRIGHT_BASE_URL` is set. Install Chromium once with `bun run test:a11y:install`. Use the [built-site recipe](../verification.md#built-site-browser-suite) to test the combined artifact instead.

Use local browser tooling for manual smoke checks; Playwright remains the automated CI runner. Before changing provider configuration, use the credential-free package checks in the [deployment guide](../../deployment/web-artifact.md).

## When web changes

Update [feature coverage](../feature-matrix.md), owning contracts, and stable browser tests. Update help when user guidance changes and the task includes that scope.
