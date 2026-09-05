# AGENTS.md — safeparts_wasm

## Purpose

Owns the wasm-bindgen facade consumed by the browser web app.

## Ownership

- `src/lib.rs`: exported WASM functions, JavaScript-friendly value mapping, and error conversion.

## Local Contracts

- Delegate split/combine, packets, encodings, and crypto behavior to `safeparts_core`.
- Recovery exports (`combine_shares`, `combine_share_input`) throw plain objects with stable `code` values and allowlisted numeric fields; see `docs/dev/surfaces/wasm.md`. Never carry core prose or input text in these objects.
- Split and inspection exports retain their existing string-error compatibility contract.
- Preserve the API shape expected by `web/src/wasm.ts` unless the web app is updated in the same change.
- `web/src/wasm_pkg/` is generated from this crate and should not be hand-edited.

## Work Guidance

- Follow `docs/agents/conventions.md` and `docs/dev/surfaces/wasm.md`.
- Keep boundary tests focused on encoding names, error mapping, and exported behavior.

## Verification

- `cargo test -p safeparts_wasm`
- `cd web && bun run build:wasm`
- `cd web && bun run typecheck`
- `cargo fmt --all -- --check`

## Child DOX Index

- No child AGENTS.md files yet.
