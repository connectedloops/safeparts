# Onboarding

This is the shortest path from a fresh clone to a useful local setup.

## 1. Read the local contract

Before editing, read:

1. [`AGENTS.md`](../../AGENTS.md)
2. The nearest `AGENTS.md` for the folder you plan to touch
3. The matching surface guide in [`surfaces/`](surfaces/)

The `AGENTS.md` files are the working contracts for this repo. If you add a durable workflow, boundary, or rule, update the nearest contract before you finish.

## 2. Install tools

Safeparts uses Rust, Bun, wasm-pack, and wasm-bindgen. The repo includes [`mise.toml`](../../mise.toml) with pinned local tool versions and shortcuts. Supported setup covers core, CLI, TUI, WASM, web, and help; it does not require retired application toolchains.

```bash
mise install
mise run setup
mise run doctor
```

If you do not use mise, install the same tools manually and use the commands in [verification.md](verification.md).

## 3. Run something useful

Rust checks:

```bash
mise run verify:rust
```

Web app:

```bash
mise run web:build
mise run web:dev
```

Help docs:

```bash
mise run docs:build
mise run docs:dev
```

## 4. Pick the right surface guide

- Core algorithm or encoding: [Core library](surfaces/core.md)
- CLI flags or stdin/stdout behavior: [CLI](surfaces/cli.md)
- Terminal workflow: [TUI](surfaces/tui.md)
- Browser bindings: [WASM bindings](surfaces/wasm.md)
- React browser UI: [Web app](surfaces/web.md)
- Help-site content: [Help docs](surfaces/help-docs.md)
- Release archives or GitHub release assets: [Release packaging](surfaces/release.md)

## 5. Before opening a PR

Run the smallest relevant checks first, then the full gate if practical:

```bash
mise run verify
```

Also check:

- Did you update [feature-matrix.md](feature-matrix.md) if behavior changed across surfaces?
- Did you update the nearest `AGENTS.md` if a contract changed?
- Did you avoid real secrets, real shares, and passphrases in tests, docs, logs, and screenshots?
- Did you leave generated artifacts in the documented state?
