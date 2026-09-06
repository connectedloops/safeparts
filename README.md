<p align="center">
  <img src="web/src/assets/logo.svg" width="120" alt="Safeparts logo" />
</p>

# Safeparts

Safeparts is a threshold secret-sharing toolkit.
You split one secret into *n* recovery shares, then later recover it from any *k* of them.

- Web app: https://safeparts.netlify.app
- Docs: https://safeparts.netlify.app/help/ (English) and https://safeparts.netlify.app/help/ar/ (Arabic)
- Releases: https://github.com/connectedloops/safeparts/releases
- Changelog: [repository](CHANGELOG.md) or [docs page](https://safeparts.netlify.app/help/changelog/)

## What it's for

Safeparts is useful when you want recovery to require cooperation instead of one perfect backup.

Common examples:

- Password manager recovery keys / master keys
- 2FA backup codes
- API tokens, signing keys, "break-glass" credentials
- Family / executor planning (no single person holds full access)
- Team secrets where you want separation of duties

## Mental model

You pick a threshold (*k* of *n*):

- Fewer than *k* shares: reconstruction is impossible (and they don't reveal the secret).
- Any *k* shares: reconstruction succeeds.

Limits: `1 <= k <= n <= 255`. If you lose shares until fewer than *k* remain, recovery is impossible.

If you want reasonable defaults:

- Personal recovery: `k=2, n=3`
- Teams: `k=3, n=5`

Pick a plan people can execute under stress. If it's too clever, it won't get used.

## What it does (and what it doesn't)

**Included**

- Shamir-style secret sharing over `GF(256)` (byte-wise)
- Integrity check on combine (BLAKE3 tag)
- Optional passphrase protection (encrypt, then split): Argon2id -> ChaCha20-Poly1305
- Multiple encodings for the same share packet bytes:
  - `base64` (`base64url`, no padding)
  - `base58` (`base58check`)
  - `mnemo-words` (word-based + CRC16)
  - `mnemo-bip39` (BIP-39-valid phrases; a share may be multiple phrases separated by `/`)

The web UI currently offers `base64url` and `mnemo-words`. The CLI/TUI support all encodings.

Internally, Safeparts encrypts (optional), appends a BLAKE3 tag, then applies Shamir sharing byte-by-byte. On combine, it reconstructs, checks the tag, and only then decrypts.

**Not included**

- Storage. Safeparts won't manage where shares live.
- Protection against someone who legitimately holds *k* shares.
- Wallet/seed functionality. Mnemonic shares are an encoding, not wallet seeds.

## Safety rules (please read)

If you take one thing from this section: **Recovery shares are as sensitive as the Secret**.

- Don't paste real Secrets or Recovery shares into chat, tickets, issues, logs, or screenshots.
- Keep fewer Recovery shares than the Threshold in every account, device, location, administrator domain, and transport channel. Apply this rule to storage and delivery: one provider or administrator must never be able to collect enough Recovery shares to recover the Secret.
- Write down the runbook: who holds which share, and how to reach them.
- Do a practice run with a synthetic secret before you rely on a real recovery plan.
- After any "break-glass" recovery, assume the gathered shares were exposed. Rotate the underlying secret and re-split.

## Interfaces

Safeparts ships as a few different front-ends over the same core:

- **Web UI** (WASM, runs entirely in your browser; no backend): easiest for one-off workflows.
- **Desktop app** (Tauri, local webview): the released Linux app and the established Windows app during the native preview.
- **Native Windows preview** (WinUI 3): local Split and Recover workbench for Windows 11 on x64 and ARM64.
- **Native macOS app** (SwiftUI, macOS 14+): the downloadable macOS workbench, shipped as an unsigned universal DMG.
- **CLI** (`safeparts`): script-friendly; good for runbooks and automation.
- **TUI** (`safeparts-tui` or `safeparts tui`): interactive terminal workflow; nice for offline machines.
- **Rust crate** (`safeparts_core`): core algorithms and packet formats.

## Rust library

If you want to embed Safeparts in a Rust project, start with `safeparts_core`:

```rust
use safeparts_core::{combine_shares, split_secret, CoreResult};

fn main() -> CoreResult<()> {
    let packets = split_secret(b"secret", 2, 3, None)?;
    let recovered = combine_shares(&packets[..2], None)?;
    assert_eq!(recovered, b"secret");
    Ok(())
}
```

For text encodings, see `safeparts_core::ascii`, `safeparts_core::mnemo_words`, and `safeparts_core::mnemo_bip39`.

## Install

Download a release archive from GitHub Releases. Each release includes:

- `safeparts` (CLI)
- `safeparts-tui` (terminal UI)
- Tauri desktop installers for Linux and Windows
- Native Windows 11 preview archives for x64 and ARM64
- an unsigned universal native DMG for macOS 14+

Platform-specific steps (and build-from-source notes) live in the docs:

- https://safeparts.netlify.app/help/build-and-run/

## CLI quickstart

Split a secret into 3 shares, requiring any 2 to recover:

```bash
echo -n "my secret" | safeparts split -k 2 -n 3 -e base64
```

Combine (paste any *k* shares on stdin):

```bash
printf "%s\n%s\n" "<share1>" "<share2>" | safeparts combine
```

Write shares and recovered secret to files:

```bash
echo -n "my secret" | safeparts split -k 2 -n 3 -e base64 -o shares.txt
printf "%s\n%s\n" "<share1>" "<share2>" | safeparts combine -o secret.bin
```

Passphrases (optional):

- Prefer `--passphrase-file` (`-P`) over `--passphrase` (`-p`) in shells that keep history.

```bash
echo -n "my secret" | safeparts split -k 2 -n 3 -e base64 -P passphrase.txt
printf "%s\n%s\n" "<share1>" "<share2>" | safeparts combine -P passphrase.txt
```

Encodings:

- `split` supports: `base64`, `base58`, `mnemo-words`, `mnemo-bip39`
- `combine` can auto-detect the encoding if you omit `--encoding`

## TUI

Run the interactive terminal UI:

```bash
safeparts-tui
```

Or launch it via the CLI:

```bash
safeparts tui
```

For shortcuts and an offline workflow, see: https://safeparts.netlify.app/help/tui/

## Desktop app (local)

The desktop app is a Tauri + React version of the web UI that runs locally. It calls `safeparts_core` from the Tauri command layer and does not require a Safeparts server, CLI sidecar, or node process at runtime. Release CI packages this app for Linux and Windows. The native Windows app below is available as a preview.

```bash
cd desktop
bun install
bun run tauri:dev
```

Build the frontend or package the Tauri app:

```bash
cd desktop
bun run build
bun run tauri:build
```

## Native macOS app (local)

The SwiftUI app is a separate macOS 14+ interface with native file panels, menu commands, and clipboard controls. It calls `safeparts_core` through the Rust UniFFI bridge.

```bash
mise run macos:check
swift run --package-path macos SafepartsMac
```

Build the same unsigned universal DMG used by release CI:

```bash
RELEASE_VERSION=v0.3.1 mise run macos:package
```

The DMG supports Intel and Apple Silicon Macs. It is not signed or notarized, so downloaded copies may trigger a Gatekeeper warning.

## Native Windows preview

The WinUI 3 preview is available for Windows 11 on x64 and ARM64. It supports the same native Split and Recover workflows as macOS, including exact binary files, all Share encodings, Auto recovery, optional Passphrase protection, explicit clipboard actions, and batch export.

On Windows with .NET 10 and Rust installed:

```powershell
python windows/scripts/prepare.py --check
cargo build -p safeparts_uniffi
dotnet run --project windows/Safeparts.App/Safeparts.App.csproj -p:Platform=x64 -p:RuntimeIdentifier=win-x64
```

Release CI publishes separate self-contained preview archives for x64 and ARM64 beside the Tauri Windows installers. The archives are unsigned, so Windows may show a SmartScreen warning. Verify the archive against `SHA256SUMS.txt` before running it. The preview has a separate identity and does not replace an installed Tauri app.

## Web UI (local)

The web UI runs split/combine locally in your browser via WASM.
It does not upload anything unless you choose to copy/paste it elsewhere or deploy a modified build.

```bash
cd web
bun install
bun run build:wasm
bun run dev
```

Open http://localhost:5173.

## Web UI + docs (Docker)

This self-hosting route builds the static Web app and bilingual help site into an unprivileged Nginx image. It is separate from hosted-provider deployment and does not add a backend: Secrets and Recovery shares stay in the browser.

Run these commands from the repository root:

```bash
docker build --pull --tag safeparts-webui --file web/Dockerfile .
docker run --detach --name safeparts-web --publish 8080:8080 safeparts-webui
docker inspect --format '{{.State.Health.Status}}' safeparts-web
curl --fail --silent --show-error http://localhost:8080/healthz >/dev/null
```

Open http://localhost:8080. English help is at http://localhost:8080/help/, and Arabic help is at http://localhost:8080/help/ar/.

Run `bash web/tests/container-smoke.sh` for the same local route and header checks used by CI. See [the Docker self-hosting guide](docs/deployment/docker.md) for verification and cleanup commands.

Docs site (served under `/help/`):

```bash
cd web/help
bun install
bun run dev
```

Open http://localhost:4321/help/.

## Development

Rust (matches CI):

```bash
cargo fmt --all -- --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
```

Web a11y tests (Playwright + axe):

```bash
cd web
bun install
bun run test:a11y:install
bun run test:a11y
```

## Repo layout

- `crates/safeparts_core/`: core algorithms, packet format, encodings, crypto
- `crates/safeparts/`: CLI wrapper (binary: `safeparts`)
- `crates/safeparts_tui/`: terminal UI (binary: `safeparts-tui`)
- `crates/safeparts_wasm/`: wasm-bindgen exports used by the web UI
- `crates/safeparts_uniffi/`: UniFFI bridge shared by native Swift and C# bindings
- `web/`: Vite + React app
- `web/help/`: Astro + Starlight docs
- `desktop/`: Tauri + React desktop app
- `macos/`: native SwiftUI macOS app
- `windows/`: native WinUI 3 Windows app, model, generated binding, and tests

## Stewardship

Safeparts was created by [Mustafa Mohsen](https://github.com/mustafamohsen) and is maintained under the [Connected Loops](https://github.com/connectedloops) GitHub organization.

## Contributing

Contributions are welcome.
Start with an issue so we can agree on scope, direction, and acceptance criteria before writing code.

Workflow:

1. Open or pick an issue.
2. Fork the repo.
3. Create a dedicated branch (e.g. `feat/<short-slug>` or `fix/<short-slug>`).
4. Make changes and run checks:
   - `cargo fmt --all`
   - `cargo clippy --all-targets --all-features -- -D warnings`
   - `cargo test --all-features`
   - `cd web && bun run test:a11y` (if you touched the web/docs)
5. Open a PR and link the issue (e.g. "Fixes #123").

## License

MIT. See LICENSE.
