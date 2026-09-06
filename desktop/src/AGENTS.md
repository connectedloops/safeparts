# AGENTS.md — Dormant desktop React source

## Purpose

Owns the dormant React source for the retired Tauri application.

## Ownership

- App shell, copied components, hooks, contexts, localization, and styling.
- `commands.ts` and `wasm.ts`: retained typed Tauri adapters.

## Local Contracts

- This source is unsupported and excluded from active builds, tests, packaging, and parity maintenance. The parent retirement contract applies.
- Preserve sensitive-data safeguards in the reference: memory-only state, explicit clipboard actions, stale-result invalidation, safe error messages, and fail-closed binary-output handling.
- Keep retained command payloads typed; do not weaken the repository's TypeScript or security standards.

## Work Guidance

- Refer to `docs/dev/surfaces/desktop.md`; supported browser changes belong in `web/`.

## Verification

- Retained adapter and rendered-workflow tests are dormant reference, not supported verification.

## Child DOX Index

- No child AGENTS.md files.
