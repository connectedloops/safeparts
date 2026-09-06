# Troubleshooting

## `mise run doctor` reports missing tools

Run:

```bash
mise install
mise run setup
```

If you do not use mise, install the versions listed in [`mise.toml`](../../mise.toml).

## WASM build cannot find `wasm-pack`

From `web/`:

```bash
bun run build:wasm
```

The build script installs the pinned `wasm-pack` and `wasm-bindgen` versions when needed. If that fails, check that Rust is installed and `~/.cargo/bin` is on `PATH`.

## Web app loads but split/combine does not work

Regenerate the WASM package:

```bash
cd web
bun run build:wasm
bun run dev
```

`web/src/wasm_pkg/` is generated and ignored by git, so a fresh clone needs this step before the browser UI can call the Rust bindings.

## Old native build commands fail

Tauri, SwiftUI, WinUI, and their dedicated UniFFI bridge are retired and excluded from supported setup. Their source is dormant reference; existing native build commands are not supported after workspace exclusion. Use the [supported surface guides](README.md#surface-guides). CLI/TUI host support on Linux, macOS, and Windows continues.

## `dx:verify` reports a stale AGENTS child path

Open the nearest parent `AGENTS.md` and fix its Child DOX Index. Either create the missing child path, correct the path, or remove a stale entry.

## `dx:verify` reports package-manager ambiguity

Use Bun for web and help. Review accidental npm, pnpm, or yarn lockfiles against the package-manager policy. Preserve user-owned untracked files and dormant dependency state.

## A check is too expensive locally

Run the targeted check for the surface you changed and record the skipped check with a reason in your PR or handoff. Do not mark a failing check as skipped.
