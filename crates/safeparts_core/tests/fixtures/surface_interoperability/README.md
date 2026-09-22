# Surface interoperability fixtures

These synthetic V2, 2-of-3 share sets capture output from the supported CLI, TUI domain boundary, and web WASM boundary, plus the experimental Qt desktop bridge. Each producer emitted all four concrete encodings from its public split path at source commit `e085fb7ce2a68dfe3b555394854dcd970d3397b7`.

| Directory | Expected recovered UTF-8 bytes | Producer boundary |
| --- | --- | --- |
| `desktop/` | `synthetic desktop interoperability` | `safeparts_desktop_bridge::Operation` |
| `cli/` | `synthetic CLI interoperability` | `safeparts split` |
| `tui/` | `synthetic TUI interoperability` | `safeparts_tui::domain::split_secret` |
| `web/` | `synthetic web interoperability` | `safeparts_wasm::split_secret` through generated WASM |

Every directory contains one complete three-share set for `base64url`, `base58check`, `mnemo-words`, and `mnemo-bip39`. `SHA256SUMS` pins the captured bytes. Tests may consume these files across surfaces, but must not regenerate them in place. Add a new versioned fixture set when a producer contract changes.

The secrets are fabricated test strings. Do not replace them with user data or real recovery material.
