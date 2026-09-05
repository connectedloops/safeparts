# AGENTS.md — Web Scripts

## Purpose

Owns web-specific helper scripts.

## Ownership

- `build-site.sh`: builds WASM, type-checks, builds the app, then help; fails unless `index.html`, `help/index.html`, and `help/ar/index.html` under `web/dist/` are nonempty files.
- `build-wasm.sh`: builds `crates/safeparts_wasm` into the generated browser package under `web/src/wasm_pkg/`.
- `test-wasm.sh`: runs the WASM binding suite in headless Chrome; set `CHROMEDRIVER` when automatic driver selection does not match the installed browser.
- `deploy-artifact.py`: prepares and verifies commit, tool-version, manifest, and served-byte evidence for the shared provider artifact.

## Local Contracts

- Keep scripts deterministic and explicit about installed tool versions.
- Do not commit generated `web/src/wasm_pkg/` output unless a task explicitly changes generated artifact policy.
- Do not add scripts that upload secrets or share material.

## Work Guidance

- Follow `docs/dev/generated-artifacts.md` and `docs/dev/surfaces/wasm.md`.
- Keep local and CI WASM build behavior aligned.
- Keep app and help writes sequential: Vite clears the parent output directory. Verify required routes only after both builds finish; propagate every build failure.
- Keep artifact verification provider-neutral; deployment credentials and publishing remain in CI.

## Verification

- From the repository root: `bash web/scripts/build-site.sh`
- `cd web && bun run build:wasm`
- `cd web && bun run test:wasm`
- `cd web && bun run typecheck`
- From `web/`: `python3 ../scripts/dev/test_web_deploy.py`
- `mise run dx:verify` when generated artifact policy changes

## Child DOX Index

- No child AGENTS.md files yet.
