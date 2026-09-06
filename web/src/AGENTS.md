# AGENTS.md — Web Source

## Purpose

Owns the Vite + React browser application source and generated WASM package boundary.

## Ownership

- `App.tsx`, `main.tsx`: app shell and startup.
- `components/`: split/combine UI, shared controls, and visual components.
- `hooks/`, `context/`, `lib/`: browser UI support code.
- `i18n.ts`: English/Arabic UI strings and direction handling.
- `styles.css`, `assets/`: web styling and assets.
- `wasm.ts`: generated WASM package loader.
- `wasm_pkg/`: generated output from `bun run build:wasm`.

## Local Contracts

- Split/combine stays local to the browser through WASM.
- Do not hand-edit `wasm_pkg/` unless the task explicitly targets generated artifacts.
- Preserve accessibility, keyboard behavior, live-region feedback, and RTL support when changing UI.
- Split clipboard actions copy one Recovery share at a time; only Combine may copy its recovered Secret through the global result shortcut.
- Generated Recovery shares are valid only while the Secret, Threshold, Share count, Share encoding, and Passphrase protection match the completed Split operation; input changes must also reject late results.
- Display and copy recovered output only when it is valid UTF-8, and invalidate it whenever Recovery shares, Share encoding, or Passphrase protection changes.
- Keep changelog discovery unobtrusive in the footer. Use the existing help base/locale and a separate tab with opener isolation so in-progress input stays in place.
- Desktop UI parity depends on copied files from this subtree.

## Work Guidance

- Follow `docs/agents/conventions.md` and `docs/dev/surfaces/web.md`.
- Keep WASM boundary types explicit and avoid `any` in new TypeScript code.
- Use project browser automation for manual smoke checks unless Playwright is explicitly requested.

## Verification

- `cd web && bun run build:wasm`
- `cd web && bun run typecheck`
- `cd web && bun run build`
- `cd web && bun run test:e2e:smoke`
- `mise run desktop:parity` when copied UI files change

## Child DOX Index

- No child AGENTS.md files yet.
