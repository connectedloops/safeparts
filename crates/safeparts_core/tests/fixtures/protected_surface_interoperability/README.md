# Protected surface interoperability fixtures

These immutable synthetic V2 protected 2-of-3 sets were captured at source commit `0da9288681d15aba188415deb8b8cb2cf7469724`. Every producer emitted all four concrete Share encodings through its public boundary.

| Directory | Exact UTF-8 secret | Producer boundary |
| --- | --- | --- |
| `desktop/` | `synthetic protected desktop interoperability` | `safeparts_desktop_bridge::Operation::create_with_passphrase` |
| `cli/` | `synthetic protected CLI interoperability` | `safeparts split` |
| `tui/` | `synthetic protected TUI interoperability` | `safeparts_tui::domain::split_secret` |
| `wasm/` | `synthetic protected WASM interoperability` | generated real-WASM `safeparts_wasm::split_secret` in Node |

Every set uses the exact synthetic UTF-8 passphrase `issue-142 synthetic interoperability passphrase`. No fixture contains user data.

Generation used the checked-out source at the commit above:

```text
printf %s '<secret>' > /tmp/secret
printf %s 'issue-142 synthetic interoperability passphrase' > /tmp/pass
mise exec -- cargo run -p safeparts -- split -k 2 -n 3 -e <encoding> -i /tmp/secret -P /tmp/pass -o <fixture>
```

The desktop and TUI captures invoked their named public Rust functions from temporary test/example drivers with the same literal arguments. The WASM capture ran `cd web && bun run build:wasm`, initialized the generated `.wasm` module in Node, and invoked exported `split_secret` with the same arguments. Those drivers were capture tools only and are not retained. Tests consume these fixed files and must never regenerate them in place. `SHA256SUMS` pins every byte; verify it with `shasum -a 256 -c SHA256SUMS` from this directory. Add a new corpus rather than replacing these files.
