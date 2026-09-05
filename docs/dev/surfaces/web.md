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
- Render Recovery shares and recovered Secrets as complete text on the first result render. Preserve Unicode and whitespace, and keep each value selectable exactly once. Reserve character animation for branding.
- Copy from result state. The recovered-Secret shortcut activates the same `CopyButton` as a click through `data-shortcut="copy-result"`; it does not read presentation markup. Split has only individual Recovery-share Copy buttons.
- Preserve keyboard access and labels when changing forms. Result text stays accessible outside live regions. Announce success or the Recovery share count without Secret or Recovery share contents.
- Test actual browser Selection and clipboard writes, including a synthetic 4 KiB Secret, both languages, and both motion preferences in `web/tests/readable-output.e2e.spec.ts`.
- Explicitly disable spellchecking, autocorrection, and autocapitalization on Secret, Recovery-share, and passphrase inputs in both languages, including dynamically added fields. Preserve entered text. These browser requests cannot guarantee safe extensions, provider features, or clipboard handling; see the English/Arabic security help.
- Derive cheap values during render. Use memoization only when computation cost or reference identity requires it.
- Keep generated modules and application boundaries typed instead of using file-wide type-check suppressions or `any` casts.
- Use local browser automation through the project browser tooling for manual checks. Playwright remains the CI runner.
- Recovery errors use the [WASM error contract](wasm.md#recovery-error-contract) and English/Arabic guidance. Recovery-share fields have distinct localized names that follow visible numbering. Unknown failures use a safe localized fallback; invalid UTF-8 keeps its separate file-recovery guidance.
- If a product UI change should exist in desktop, update desktop parity or record why not. The approved web-only review fixes leave the excluded desktop mirror unchanged. Report the resulting parity failure; keep the gate intact.
- Threshold and Share count select their current value on coarse-pointer focus only while that input remains connected and focused. Fine-pointer focus keeps native caret behavior. `web/tests/split-touch-focus.e2e.spec.ts` covers replacement, blur/unmount, steppers, bounds, result invalidation, and English/Arabic layouts.
- Treat the tested `web/dist` plus help output as one release unit. Netlify and Cloudflare must consume the retained artifact instead of rebuilding source.

## Useful checks

For a complete static site, run `mise run web:build:site` or `bash web/scripts/build-site.sh` from the repository root. It builds the app before help and checks the final routes. The standalone app build below clears `web/dist/`, including any help output. See [output semantics](../verification.md#output-semantics).

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
