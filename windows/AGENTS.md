# AGENTS.md — Dormant WinUI reference

## Purpose

Retains the retired native Windows application as dormant reference under approved issue #97.

## Ownership

- `Safeparts.App/`, `Safeparts.AppModel/`: retained WinUI application and UI-free state model.
- `Safeparts.Native/`, `Generated/`: retained adapter and generated C# binding.
- Test and interoperability projects, `scripts/`, and `docs/`: dormant validation and release reference.

## Local Contracts

- This application and its dedicated UniFFI bridge are unsupported: no active build, test, packaging, promotion, or parity obligation remains.
- Retain source and user-owned files. Existing native build commands are not supported after root workspace exclusion.
- A replacement or reactivation requires a new decision; no plan or timeline exists. There is no native preview promotion or Tauri cutover pending.
- Preserve core-owned cryptography and parsing, memory-only sensitive state, exact byte IO, explicit clipboard actions, stable share identities, and stale-result invalidation.
- Never log Secrets, Recovery shares, reconstructed bytes, or passphrases. Leave generated C# and build output untouched during supported-surface work.

## Work Guidance

- See `docs/dev/surfaces/windows.md` for reference status. CLI/TUI support on Windows continues.

## Verification

- Retained model, UI Automation, native interoperability, and package checks are dormant reference, not active gates or release requirements.

## Child DOX Index

- No child AGENTS.md files.
