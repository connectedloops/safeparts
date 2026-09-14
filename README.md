<p align="center">
  <img src="web/src/assets/logo.svg" width="120" alt="Safeparts logo" />
</p>

# Safeparts

Safeparts splits a secret into recovery shares. With a 2-of-3 setup, any two distinct, valid shares from the same split can recover it. If you enable passphrase protection, you also need the original passphrase.

- Web app: https://safeparts.netlify.app
- Docs: https://safeparts.netlify.app/help/ (English) and https://safeparts.netlify.app/help/ar/ (Arabic)
- Releases: https://github.com/connectedloops/safeparts/releases
- Changelog: [repository](CHANGELOG.md) or [docs page](https://safeparts.netlify.app/help/changelog/)

## What it's for

Use Safeparts when recovery should require cooperation:

- Password manager recovery keys / master keys
- 2FA backup codes
- API tokens, signing keys, emergency-access credentials
- Family / executor planning where no single person holds full access
- Team secrets that require separation of duties

The threshold (*k*) is the number of shares needed to recover; the share count (*n*) is the number you create. Limits: `1 <= k <= n <= 255`. Fewer than *k* shares do not reveal the secret contents, but packets expose metadata such as threshold and payload length. Losing too many shares makes recovery impossible.

Common starting points are 2 of 3 for personal recovery and 3 of 5 for teams. Pick a plan people can execute under stress.

## Choose an interface

- **Web UI**: text workflows, processed locally in your browser via WASM.
- **CLI** (`safeparts`): scripts and operator-approved automation.
- **TUI** (`safeparts-tui` or `safeparts tui`): interactive terminal workflows.
- **Rust crate** (`safeparts_core`) and **WASM bindings** (`safeparts_wasm`): integration APIs.

The CLI and TUI support Linux, macOS, and Windows. Desktop applications are retired; see [existing desktop users](#existing-desktop-users) for supported recovery tools.

## Install

Download a CLI/TUI archive from [GitHub Releases](https://github.com/connectedloops/safeparts/releases). Follow the [installation and checksum steps](https://safeparts.netlify.app/help/build-and-run/) before running the binaries. Historical desktop installers remain available but are unsupported.

## CLI quickstart

Run these examples in Bash. **Synthetic practice data only:** substituting real secrets, shares, or passphrases in shell arguments can leave them in shell history.

```bash
printf '%s' 'example secret' | safeparts split -k 2 -n 3 -e base64url
printf '%s\n%s\n' '<share1>' '<share2>' | safeparts combine
```

For actual material, use controlled files inside an approved temporary setup, verification, or recovery session:

```bash
safeparts split --in secret.bin -k 2 -n 3 -e base64url --out shares.txt
safeparts combine --in selected-shares.txt --out recovered.bin
```

These commands are not a complete backup procedure. `shares.txt` contains every generated share; `selected-shares.txt` brings recovery shares together. Treat both as sensitive temporary material, not the backup. Distribute shares into separate custody and complete the [saved-backup checkpoint](https://safeparts.netlify.app/help/it-devops-guide/break-glass/#verify-saved-backup) before retiring the working copy and removing temporary aggregates. Keep real recovered bytes out of terminal output.

For optional passphrase protection, use `--passphrase-file` (`-P`) rather than a shell argument. It removes all trailing CR and LF bytes; use the same input convention for split and recovery. See the [CLI guide](https://safeparts.netlify.app/help/cli/) for byte-preserving recovery caveats.

`split` supports `base64url` (CLI alias: `base64`), `base58check` (alias: `base58`), `mnemo-words`, and `mnemo-bip39`. `combine` auto-detects the encoding when you omit `--encoding`.

For the interactive workflow, run `safeparts-tui` in a terminal. Press F1 for help and Ctrl+Q to quit. See the [TUI guide](https://safeparts.netlify.app/help/tui/) for shortcuts.

## Safety

Handle recovery shares as carefully as the secret.

- Don't paste real secrets or shares into chat, tickets, issues, logs, or screenshots.
- Keep fewer than the threshold number of shares in every account, device, location, administrator domain, and transport channel. Aggregate shares only during approved temporary setup, verification, or recovery sessions.
- Write down who holds each share and how to reach them.
- Practice with a synthetic secret, then verify the actual saved custody copies before relying on the backup. If verification fails or policy prevents it, keep the working copy and leave setup incomplete.
- After emergency recovery, assume the gathered shares were exposed. Rotate the underlying secret and re-split.

The web app processes secrets and recovery shares locally in your browser. Use a trusted browser and device; extensions, clipboard sync, and screen recording can still expose sensitive data. Loading the site also requires ordinary network requests. See the [security guide](https://safeparts.netlify.app/help/security/) for boundaries and cleanup limits.

Safeparts uses Shamir-style sharing over `GF(256)`. Optional passphrase protection uses Argon2id and ChaCha20-Poly1305 before splitting. A BLAKE3 digest detects corruption during recovery; it does not authenticate the sender or prevent replacement with another valid set. See [technical design](https://safeparts.netlify.app/help/technical-design/) for packet metadata and integrity limits.

Safeparts does not store your shares or prevent recovery by someone with enough valid shares and the passphrase, if used. Mnemonic shares are encodings, **not wallet seeds**. The web UI offers `base64url` and `mnemo-words`; CLI/TUI support all four encodings.

## Rust library

See the [Rust integration manual](docs/dev/manuals/rust-library.md) for dependency setup. A synthetic round trip:

```rust
use safeparts_core::{combine_shares, split_secret, CoreResult};

fn main() -> CoreResult<()> {
    let packets = split_secret(b"secret", 2, 3, None)?;
    let recovered = combine_shares(&packets[..2], None)?;
    assert_eq!(recovered, b"secret");
    Ok(())
}
```

For text conversion, use `safeparts_core::encoding::{encode_packet, parse_share_packets}`.

## Local development and self-hosting

Start with [onboarding](docs/dev/onboarding.md) for prerequisites, pinned tools, and installation side effects. Run the following Bash commands from the repository root after setup:

```bash
(cd web && bun run build:wasm && bun run dev)
```

Open http://localhost:5173.

### Help development

From the repository root, after installing help dependencies:

```bash
(cd web/help && bun run dev)
```

Open http://localhost:4321/help/. A standalone help build writes only `web/dist/help/`. For the app and bilingual help together, run `mise run web:build:site` from the root. Do not run the standalone app build afterward: it clears the help output.

### Docker self-hosting

The [Docker guide](docs/deployment/docker.md) covers building the static app and bilingual help into an unprivileged Nginx image, checking health and routes, and cleanup. Self-hosting does not add a secret-processing backend.

### Basic Rust checks

```bash
cargo fmt --all -- --check
cargo clippy --all-targets --all-features -- -D warnings
cargo test --all-features
```

These are not the full CI suite. See [verification](docs/dev/verification.md) for `mise run verify`, independent RustSec checks, browser test setup, and additional gates. The [developer index](docs/dev/README.md) maps supported surfaces and repository structure.

## Existing desktop users

Keep your saved recovery shares and any passphrase. Retirement does not change the share packet format or require re-splitting. Supported tools retain decoding for released Safeparts V1 and V2 shares.

Use CLI/TUI file output for all encodings and exact binary recovery; use the web app for text workflows. Keep originals until you have checked the recovered bytes in a trusted environment. Never send shares or passphrases to support.

See the [retired-desktop recovery notice](https://safeparts.netlify.app/help/desktop/) ([Arabic](https://safeparts.netlify.app/help/ar/desktop/)) and [dormant source references](docs/dev/README.md#dormant-reference). There is no replacement plan or timeline.

## Contributing

Start with an issue to agree on scope and acceptance criteria. See [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow and checks.

## Stewardship

Safeparts was created by [Mustafa Mohsen](https://github.com/mustafamohsen) and is maintained under the [Connected Loops](https://github.com/connectedloops) GitHub organization.

## License

MIT. See [LICENSE](LICENSE).
