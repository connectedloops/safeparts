# AGENTS.md — Web Tests

## Purpose

Owns browser smoke, end-to-end, docs route, accessibility, and mirrored Tauri UI tests for the web app, desktop app, and help site.

## Ownership

- `*.spec.ts`: Playwright and axe coverage used by CI, including rendered desktop tests backed by a synthetic Tauri command boundary.
- `docs.smoke.spec.ts` and `docs.a11y.spec.ts`: bilingual help route/accessibility coverage, including generated changelog navigation, literal history text, and LTR isolation.
- `container-smoke.sh`: clean image build and offline runtime HTTP checks used by CI.
- `a11y-utils.ts`: shared accessibility and WASM-ready helpers.
- `tsconfig.json`: test TypeScript settings.

## Local Contracts

- Tests may use Playwright because CI owns these suites.
- For manual browser smoke work, prefer the project browser skill or `browse` CLI unless the user asks for Playwright.
- Keep test fixtures synthetic. Do not paste real secrets or real shares into tests.
- Sensitive-input coverage inspects live effective spellchecking and writing-assistance attributes in English and Arabic, including added Recovery-share fields, and checks exact input/paste content and Share encoding detection with synthetic data.
- Rendered Tauri tests mock the public command boundary, not React state or component internals.
- Generated WASM module mocks must match both `/src/wasm_pkg/safeparts_wasm.js` in dev and `/assets/safeparts_wasm-<hash>.js` in built artifacts, allow query strings, and assert interception. Match the public module URL without hardcoding hashes or minifier internals.
- Accessibility tests should fail on serious, critical, and total axe violations unless a task explicitly changes the policy.

## Work Guidance

- Follow `docs/dev/surfaces/web.md` and `docs/agents/conventions.md`.
- Prefer stable role/label selectors over brittle DOM snapshots.
- Add tests for stable workflows, not temporary UI experiments.
- Verify output with browser Selection and clipboard writes, not hidden DOM copies. `readable-output.e2e.spec.ts` covers exact selection, state-backed copy, Unicode/whitespace, English/Arabic, motion preferences, and bounded DOM size for a synthetic 4 KiB Secret.
- `split-touch-focus.e2e.spec.ts` covers coarse-pointer numeric focus. Wait for deferred focus work and prove selection by typing a replacement; clicking or filling alone can mask focus regressions. Check page errors, blur/unmount safety, bounds, result invalidation, and English/Arabic layouts.

## Verification

- `cd web && bun run test:e2e:smoke`
- `cd web && bun run test:e2e:full`
- `cd web && bun run test:container`
- `cd web && bun run typecheck`

## Child DOX Index

- No child AGENTS.md files yet.
