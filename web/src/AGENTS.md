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
- Render each Recovery share and recovered Secret as one immediately complete, selectable text value with preserved Unicode and whitespace. Keep `EncryptedText` for decorative branding, outside security-bearing output.
- Copy buttons use result state, not presentation text. The Combine shortcut activates its `CopyButton` through `data-shortcut="copy-result"`; Split copies one Recovery share at a time and has no global copy action.
- Keep result text readable to assistive technology outside live regions; success announcements contain only status or Recovery share counts.
- Generated Recovery shares are valid only while the Secret, Threshold, Share count, Share encoding, and Passphrase protection match the completed Split operation; input changes must also reject late results.
- Display and copy recovered output only when it is valid UTF-8, and invalidate it whenever Recovery shares, Share encoding, or Passphrase protection changes.
- Keep changelog discovery unobtrusive in the footer. Use the existing help base/locale and a separate tab with opener isolation so in-progress input stays in place.
- Secret, Recovery-share (including dynamically added fields), and passphrase inputs explicitly disable spellchecking, autocorrection, and autocapitalization in both languages. Preserve entered text; browser settings are a mitigation, not a guarantee against extensions, provider features, or clipboard exposure.
- Recovery guidance uses validated WASM error codes and safe counts, never English prose matching or raw exception display/logging. Legacy string errors and unknown failures use a localized fallback.
- Each Recovery-share textarea is named by its localized visible field number; numbering follows current field order after add/remove.
- Recovery of non-UTF-8 output directs users to CLI/TUI file output in both languages. Dormant desktop mirrors are not parity targets.
- Coarse-pointer Threshold and Share count focus selects the current value after dispatch only while the original input remains connected and focused; fine-pointer caret behavior stays native.

## Work Guidance

- Follow `docs/agents/conventions.md` and `docs/dev/surfaces/web.md`.
- Keep WASM boundary types explicit and avoid `any` in new TypeScript code.
- Use project browser automation for manual smoke checks unless Playwright is explicitly requested.

## Verification

- `cd web && bun run build:wasm`
- `cd web && bun run typecheck`
- `cd web && bun run build`
- `cd web && bun run test:e2e:smoke`

## Child DOX Index

- No child AGENTS.md files yet.
