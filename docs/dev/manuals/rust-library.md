# Rust library integration manual

Use `safeparts_core` when another Rust program needs to create or recover Safeparts recovery shares without shelling out to the CLI.

The core crate works on bytes. It zeroizes temporary secret, reconstruction, and Shamir coefficient buffers when they leave scope. Your application still owns storage, operator identity, audit policy, UI wording, and the lifetime of returned secret and recovery-share bytes.

## Add the crate

Inside this repository, depend on the workspace crate by path:

```toml
[dependencies]
safeparts_core = { path = "../safeparts/crates/safeparts_core" }
```

For another repository, use a Git dependency until a published crate is available:

```toml
[dependencies]
safeparts_core = { git = "https://github.com/connectedloops/safeparts", package = "safeparts_core" }
```

If you want zeroizing wrappers in your application code, add `zeroize` too:

```toml
[dependencies]
zeroize = "1"
```

## Minimal split and recover

```rust
use safeparts_core::{combine_shares, split_secret, CoreResult};

fn main() -> CoreResult<()> {
    let shares = split_secret(b"synthetic integration secret", 2, 3, None)?;
    let recovered = combine_shares(&shares[..2], None)?;

    assert_eq!(recovered, b"synthetic integration secret");
    Ok(())
}
```

Core enforces `1 <= k <= n <= 255`. Decoded and directly constructed share packets must also use an index in `1..=n`; recovery rejects reserved packet flags and inputs larger than the declared share count.

## Encode shares for storage

Most applications should store or display encoded share text, not raw `SharePacket` structs.

```rust
use safeparts_core::encoding::{self, Encoding};
use safeparts_core::{split_secret, CoreResult};

fn split_for_storage() -> CoreResult<Vec<String>> {
    let packets = split_secret(b"synthetic storage secret", 2, 3, None)?;

    packets
        .iter()
        .map(|packet| encoding::encode_packet(packet, Encoding::Base64url))
        .collect()
}
```

Keep each encoded share in a separate failure domain. Storing enough shares together defeats the threshold design.

## Parse pasted shares and recover

Use `Encoding::Auto` when an operator pastes shares and the UI does not know the encoding. The parser returns the concrete encoding it used.

```rust
use safeparts_core::encoding::{self, Encoding};
use safeparts_core::{combine_shares, CoreResult};

fn recover_from_paste(input: &str) -> CoreResult<Vec<u8>> {
    let parsed = encoding::parse_share_packets(input, Encoding::Auto)?;
    combine_shares(&parsed.packets, None)
}
```

For UIs that allow wrapped mnemonic lines, use `parse_share_packets_wrapped_mnemonics`. It first tries to strictly decode every nonempty line as a complete Recovery share, so CLI mnemonic output can be loaded unchanged. If that fails, it treats each blank-line-separated paragraph as one wrapped Recovery share. Both attempts consume all nonempty input, including every word and BIP-39 frame; empty BIP-39 frames are errors. It accepts LF and CRLF line endings with Auto encoding or an explicit mnemonic encoding. Compact encodings still use whitespace separators.

```rust
use safeparts_core::encoding::{self, Encoding};
use safeparts_core::CoreResult;

fn parse_wrapped_mnemonic(input: &str) -> CoreResult<usize> {
    let parsed = encoding::parse_share_packets_wrapped_mnemonics(input, Encoding::Auto)?;
    Ok(parsed.packets.len())
}
```

## Add passphrase protection

Passphrase protection encrypts the secret before splitting. Recovery then requires both enough shares and the same passphrase. Core rejects share packets whose Argon2 settings fall outside the supported policy: 8,192 to 262,144 KiB of memory, 1 to 10 iterations, and parallelism from 1 to 4.

```rust
use safeparts_core::{combine_shares, split_secret, CoreResult};
use zeroize::Zeroizing;

fn encrypted_round_trip() -> CoreResult<()> {
    let passphrase = Zeroizing::new(b"synthetic passphrase for tests".to_vec());

    let shares = split_secret(
        b"synthetic protected secret",
        2,
        3,
        Some(passphrase.as_slice()),
    )?;

    let recovered = combine_shares(&shares[..2], Some(passphrase.as_slice()))?;
    assert_eq!(recovered, b"synthetic protected secret");
    Ok(())
}
```

Do not accept passphrases through logs, URLs, panic messages, analytics, or request metadata that your platform records by default.

## Handle errors by class

Core errors are typed. Match the cases that affect user guidance and log only the failure class. For a generic boundary, `CoreError::user_message()` returns a redacted message that does not repeat recovery-share input.

```rust
use safeparts_core::encoding::{self, Encoding};
use safeparts_core::{combine_shares, CoreError};

fn recover_with_message(input: &str) -> Result<Vec<u8>, String> {
    let parsed = encoding::parse_share_packets(input, Encoding::Auto).map_err(|err| match err {
        CoreError::EmptyShareInput => "paste at least one recovery share".to_string(),
        CoreError::CouldNotDetectEncoding => "choose the share encoding and try again".to_string(),
        CoreError::Encoding(_) | CoreError::InvalidPacket(_) => {
            "one or more recovery shares could not be decoded".to_string()
        }
        other => other.to_string(),
    })?;

    combine_shares(&parsed.packets, None).map_err(|err| match err {
        CoreError::NotEnoughShares { k, m } => format!("need {k} shares, got {m}"),
        CoreError::DuplicateX { .. } => "the same recovery share was provided twice".to_string(),
        CoreError::InconsistentMetadata => "recovery shares come from different sets".to_string(),
        CoreError::PassphraseRequired => "these recovery shares need a passphrase".to_string(),
        CoreError::DecryptFailed => "wrong passphrase or tampered encrypted data".to_string(),
        CoreError::IntegrityCheckFailed => "recovery shares failed the integrity check".to_string(),
        other => other.to_string(),
    })
}
```

Avoid adding the pasted share text to error context. The text itself is sensitive.

## Public API reference

### Top-level crate API

| Item | Signature or shape | Use |
| --- | --- | --- |
| `split_secret` | `fn split_secret(secret: &[u8], k: u8, n: u8, passphrase: Option<&[u8]>) -> CoreResult<Vec<SharePacket>>` | Main API for creating share packets. |
| `combine_shares` | `fn combine_shares(packets: &[SharePacket], passphrase: Option<&[u8]>) -> CoreResult<Vec<u8>>` | Main API for recovering secret bytes. |
| `tag_and_split` | `fn tag_and_split(secret: &[u8], k: u8, n: u8) -> CoreResult<Vec<SharePacket>>` | Compatibility wrapper for unprotected splits. Prefer `split_secret`. |
| `combine_and_verify` | `fn combine_and_verify(packets: &[SharePacket]) -> CoreResult<Vec<u8>>` | Compatibility wrapper for unprotected combine. Prefer `combine_shares`. |
| `CoreError` | enum | Typed error cases. Match this at UI or service boundaries; use `user_message()` for redacted generic output. |
| `CoreResult<T>` | `Result<T, CoreError>` | Convenience result alias. |
| `INTEGRITY_TAG_LEN` | `usize` | Length of the internal BLAKE3 tag. Most integrations do not need it. |

### `encoding` module

Use this module for text storage, copy/paste, and operator input.

| Item | Use |
| --- | --- |
| `Encoding` | Share encoding enum: `Auto`, `Base64url`, `Base58check`, `MnemoWords`, `MnemoBip39`. |
| `Encoding::CONCRETE` | Concrete encodings valid for output. |
| `Encoding::WITH_AUTO` | Encodings plus `Auto`, useful for combine UI options. |
| `Encoding::label()` | Canonical label such as `base64url` or `mnemo-words`. |
| `Encoding::parse_name(name)` | Parse canonical names and CLI aliases such as `base64` and `base58`. |
| `Encoding::is_auto()` | Check whether the value is `Auto`. |
| `ParsedSharePackets` | Parsed packet list plus detected concrete encoding. |
| `encode_packet(packet, encoding)` | Encode one packet as share text. `Auto` is rejected for output. |
| `decode_packet(text, encoding)` | Decode one share packet. `Auto` requires exactly one packet in the input. |
| `parse_share_packets(input, encoding)` | Parse one or more shares. Whitespace separates compact encodings. Mnemonic shares use lines or blank-line blocks. |
| `parse_share_packets_wrapped_mnemonics(input, encoding)` | Parse UI input where a single mnemonic share may wrap across lines. |
| `detect_encoding(input)` | Return the likely concrete encoding without returning packets. |

### `packet` module

Most integrations only need packet fields for inspection. Do not mutate packets unless you are writing tests or a storage adapter.

| Item | Use |
| --- | --- |
| `SharePacket` | Packet struct with `set_id`, `k`, `n`, `x`, `payload`, and optional `crypto_params`. |
| `SharePacket::from_raw_share(share)` | Build a packet from a lower-level `RawShare`. Mainly for internals and tests. |
| `SharePacket::with_crypto_params(params)` | Attach crypto parameters to a packet. Usually handled by `split_secret`. |
| `SharePacket::is_encrypted()` | True when passphrase protection was used. |
| `SharePacket::to_raw_share()` | Convert back to a lower-level `RawShare`. Used by combine internals. |
| `SharePacket::encode_binary()` | Serialize to Safeparts binary packet format. Prefer text encodings unless you control binary storage. |
| `SharePacket::decode_binary(bytes)` | Parse the binary packet format. |
| `packet::binary_total_len(bytes)` | Compute the full binary packet length from a byte prefix. Mainly useful for framing. |

### Lower-level modules

These modules are public because the crate is still small, but most applications should prefer the top-level and `encoding` APIs.

| Module | Public items | When to use |
| --- | --- | --- |
| `ascii` | `Encoding`, `encode_packet`, `decode_packet` | Direct base64url or base58check handling. Prefer `encoding` for new code. |
| `mnemo_words` | `encode_packet`, `decode_packet` | Direct word-list encoding with strict word-count and padding checks. Prefer `encoding` for new code. |
| `mnemo_bip39` | `encode_packet`, `decode_packet` | Direct BIP-39 phrase encoding. Prefer `encoding` for new code. |
| `crypto` | `CryptoParams`, `CryptoParams::random_default`, `encrypt`, `decrypt` | Low-level encrypt/decrypt. Prefer passphrase arguments on `split_secret` and `combine_shares`. |
| `sss` | `SetId`, `SetId::random`, `RawShare`, `split`, `combine` | Low-level Shamir shares without packet, encoding, integrity, or passphrase policy. Use only for focused tests or internals. |
| `gf256` | `Gf256`, `Gf256::inv`, `Gf256::checked_div` | Core math internals. Avoid in application integrations. |

## Common `CoreError` cases

| Error | Meaning for integrations |
| --- | --- |
| `InvalidKAndN` | Split parameters are outside `1 <= k <= n <= 255`. |
| `NotEnoughShares` | Combine input has fewer than the threshold. |
| `InconsistentMetadata` | Shares do not belong to the same set or have incompatible metadata. |
| `DuplicateX` | The same share coordinate was supplied more than once. |
| `InvalidX` / `InvalidShareIndex` | A share index is zero or exceeds the declared share count. |
| `TooManyShares` | Recovery received more packets than the declared share count. |
| `UnsupportedPacketFlags` | A packet uses flag bits that its version does not define. |
| `InvalidPacket` | Binary packet parsing failed. |
| `Encoding` | Text decoding failed for the selected encoding. |
| `UnknownEncoding` | A provided encoding name is not recognized. |
| `EmptyShareInput` | Pasted share input had no non-empty shares. |
| `CouldNotDetectEncoding` | Auto encoding could not choose a concrete encoding. |
| `AutoEncodingForOutput` | Caller tried to encode output with `Encoding::Auto`. |
| `IntegrityCheckFailed` | Reconstructed bytes did not match the internal BLAKE3 tag. |
| `PassphraseRequired` | Encrypted shares were combined without a passphrase. |
| `DecryptFailed` | Wrong passphrase or tampered encrypted data. |
| `UnsupportedCryptoParams` | Passphrase-protection work factors fall outside the supported resource policy. |
| `CryptoParamsMismatch` | Encrypted packet metadata does not match across shares. |

## Integration checklist

Before using Safeparts in another product, decide:

- Where each recovery share will be stored.
- Whether passphrase protection is required or allowed.
- Which share encoding operators can handle under stress.
- How your app will avoid logging share text and recovered bytes.
- How users will practice recovery with a synthetic secret.
- Which errors get user-facing messages and which become audit events.

A safe audit event says recovery was attempted or completed. It should not include share text, passphrases, or recovered secret bytes.
