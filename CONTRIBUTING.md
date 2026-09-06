# Contributing

Thanks for helping with Safeparts.

Start with the developer docs: [`docs/dev/README.md`](docs/dev/README.md).

Supported work covers core, CLI, TUI, WASM, web, and help. CLI/TUI archives support Linux, macOS, and Windows. Tauri, SwiftUI, WinUI, and their dedicated UniFFI bridge are retired, retained as dormant reference without build, test, packaging, or parity requirements. A replacement needs a new decision; there is no plan or timeline.

## Before you edit

1. Read [`AGENTS.md`](AGENTS.md).
2. Read the nearest child `AGENTS.md` for the files you will touch.
3. Check the matching guide under [`docs/dev/surfaces/`](docs/dev/surfaces/).
4. If your change affects feature coverage, update [`docs/dev/feature-matrix.md`](docs/dev/feature-matrix.md).

## Local setup

If you use mise:

```bash
mise install
mise run setup
mise run doctor
```

If you do not use mise, install the tools listed in [`mise.toml`](mise.toml) and use the commands in [`docs/dev/verification.md`](docs/dev/verification.md).

## Checks

Run the checks for the area you changed. The full local gate is:

```bash
mise run verify
```

Targeted commands live in [`docs/dev/verification.md`](docs/dev/verification.md).

## Security-sensitive work

Do not put real secrets, real recovery shares, passphrases, or reconstructed secrets in issues, logs, tests, screenshots, or docs. Use synthetic examples only.

## Git workflow

- Create a feature branch for meaningful work.
- Keep commits small and scoped.
- Use conventional commit messages.
- Prefer rebase over merge.
- Do not push unless you intend to publish your branch.

## Pull request checklist

Before opening a PR, check:

- Relevant tests pass, or skipped checks are explained.
- The nearest `AGENTS.md` still matches the code.
- Developer docs are updated for workflow or surface changes.
- User-facing docs are updated only when the task calls for it.
- Generated artifacts and lockfiles match the repo policy.
