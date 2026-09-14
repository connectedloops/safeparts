# Onboarding

This is the shortest path from a fresh clone to a useful local setup.

## 1. Read the local contract

Before editing, read:

1. [`AGENTS.md`](../../AGENTS.md)
2. The nearest `AGENTS.md` for the folder you plan to touch
3. The matching surface guide in [`surfaces/`](surfaces/)

The `AGENTS.md` files are the working contracts for this repo. If you add a durable workflow, boundary, or rule, update the nearest contract before you finish.

## 2. Install tools

Run the examples in **Bash from the repository root** on Linux/macOS, or in a separately installed Unix-like shell on Windows. These blocks are not PowerShell commands. Supported setup covers core, CLI, TUI, WASM, web, and help; retired application toolchains are not required.

Install these prerequisites separately:

- Git, [mise](https://mise.jdx.dev/getting-started.html), and Bash. `mise.toml` does not provision Bash.
- Python **3.11 or newer**, available as `python3`, for doctor, DX/workflow checks, and dependency scans. Mise does not provision Python.
- Rust/rustup access and the platform's native compiler/linker tools. See [Rust installation](https://www.rust-lang.org/tools/install); Windows Rust builds require the appropriate Visual Studio C++ build tools.

[`mise.toml`](../../mise.toml) pins Rust (including its WASM target and components), Bun, Node, WASM tools, and verification tools. Node is required by Astro and deployment tooling even though Bun manages packages. Use the checked-in versions, not versions from another branch.

`mise install` downloads tools and may build Cargo-installed tools. `mise run setup` installs both web and help dependencies from frozen lockfiles; package install scripts may run. `web/scripts/build-wasm.sh` adds the WASM target and can install or replace the pinned `wasm-pack` and `wasm-bindgen-cli` with `cargo install --locked --force`. These steps need network access on a fresh machine and can change tools in your Cargo bin directory.

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

Run the smallest relevant checks first, then the [local aggregate gate](verification.md#one-command-checks) if practical. It does not include every CI check:

```bash
mise run verify
```

Also check:

- Did you update [feature-matrix.md](feature-matrix.md) if behavior changed across surfaces?
- Did you update the nearest `AGENTS.md` if a contract changed?
- Did you avoid real secrets, real shares, and passphrases in tests, docs, logs, and screenshots?
- Did you leave generated artifacts in the documented state?
