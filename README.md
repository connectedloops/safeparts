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
- **CLI** (`safeparts`): script-friendly; good for runbooks and automation.
- **TUI** (`safeparts-tui` or `safeparts tui`): interactive terminal workflow; nice for offline machines.
- **Rust crate** (`safeparts_core`): core algorithms and packet formats.
- **WASM bindings** (`safeparts_wasm`): core APIs for the browser app.
- **Help site**: English and Arabic guidance under `/help/`.

The CLI and TUI support Linux, macOS, and Windows. All desktop applications (Tauri, SwiftUI macOS, and WinUI Windows) are retired. Their source and dedicated UniFFI bridge remain dormant reference, outside supported builds, tests, releases, and feature parity. There is no replacement plan or timeline; a future replacement needs a new decision.

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

Download a CLI/TUI archive for Linux, macOS, or Windows from GitHub Releases. Supported releases include `safeparts` (CLI) and `safeparts-tui` (terminal UI), not desktop installers. Historical releases remain available unchanged; their desktop assets are unsupported.

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

## Existing desktop users

Keep your saved recovery shares and any passphrase. Retirement does not change the Share packet format or require you to split the secret again. Supported tools retain decoding for released Safeparts V1 and V2 shares.

Use the CLI or TUI for all share encodings and exact binary-file recovery. The web app is suitable for text workflows. Gather at least the threshold number of shares from the same set and supply the original passphrase if protection was enabled. Keep originals until you have checked the recovered bytes in your trusted environment; never send shares or passphrases to an issue or support chat.

See the [build and recovery guidance](https://safeparts.netlify.app/help/build-and-run/) and its [Arabic version](https://safeparts.netlify.app/help/ar/build-and-run/). Dormant source is described in [developer reference notices](docs/dev/README.md#dormant-reference). Old native build commands are not supported after workspace exclusion.

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
- `crates/safeparts_uniffi/`: dormant UniFFI bridge for retired native apps
- `web/`: Vite + React app
- `web/help/`: Astro + Starlight docs
- `desktop/`: dormant Tauri + React reference source
- `macos/`: dormant SwiftUI reference source
- `windows/`: dormant WinUI reference source, model, bindings, and tests
- `mobile/`: dormant prototype artifacts

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
