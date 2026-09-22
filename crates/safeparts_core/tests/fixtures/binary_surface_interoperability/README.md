# Binary surface interoperability fixtures

These immutable synthetic V2 2-of-3 sets were captured from source commit `839d8fc8f3a739bb0d39ecba0693734437ccfe90`. Tests consume the fixed files and must never regenerate them in place.

`secret.bin` is the exact 11-byte fabricated secret `00 ff fe 0d 0a 42 69 6e 00 80 7f`. It includes NUL, invalid UTF-8, CRLF, and high bytes. No fixture contains user data.

| Directory | Protection | Producer boundary |
| --- | --- | --- |
| `desktop/` | Unprotected | `safeparts_desktop_bridge::Operation::create_with_passphrase` |
| `desktop-protected/` | V2 Argon2id/ChaCha20-Poly1305 | `safeparts_desktop_bridge::Operation::create_with_passphrase` |
| `cli/` | Unprotected | `safeparts split` with `secret.bin` as file input |
| `tui/` | Unprotected | `safeparts_tui::domain::split_secret` used by the TUI file path |
| `wasm/` | Unprotected | generated real-WASM `safeparts_wasm::split_secret` invoked with `Uint8Array` from Bun |

Every producer emitted Base64url, Base58check, Words, and BIP-39 through its public boundary. The protected desktop set uses the exact synthetic UTF-8 passphrase `issue-143 synthetic binary interoperability passphrase` and the core's stored creation defaults. Each text file contains the complete three-share set with one canonical encoded share per line.

The CLI capture used this command for each encoding:

```text
mise exec -- cargo run -q -p safeparts -- split -k 2 -n 3 -e <encoding> -i <secret.bin> -o <fixture>
```

Temporary, unretained Rust drivers invoked `Operation::create_with_passphrase` for desktop and `safeparts_tui::domain::split_secret` for TUI with the literal bytes above. The WASM capture ran `cd web && bun run build:wasm`, initialized `web/src/wasm_pkg/safeparts_wasm_bg.wasm` from a temporary Bun driver, and passed a `Uint8Array` to the generated `split_secret` export. `SHA256SUMS` pins every secret/share fixture byte.

All four active boundaries can create and recover arbitrary bytes, so no producer direction is inapplicable. Consumption evidence covers the core library, CLI process, TUI domain, desktop operation, and this generated real-WASM module in headless Chrome. This is boundary evidence only; it does not claim that the web UI exposes a binary-file workflow.
