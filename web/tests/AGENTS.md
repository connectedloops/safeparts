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
- Rendered Tauri tests mock the public command boundary, not React state or component internals.
- Accessibility tests should fail on serious, critical, and total axe violations unless a task explicitly changes the policy.

## Work Guidance

- Follow `docs/dev/surfaces/web.md` and `docs/agents/conventions.md`.
- Prefer stable role/label selectors over brittle DOM snapshots.
- Add tests for stable workflows, not temporary UI experiments.

## Verification

- `cd web && bun run test:e2e:smoke`
- `cd web && bun run test:e2e:full`
- `cd web && bun run test:container`
- `cd web && bun run typecheck`

## Child DOX Index

- No child AGENTS.md files yet.
