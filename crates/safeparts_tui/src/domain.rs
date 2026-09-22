use anyhow::{Context, Result, anyhow};
use safeparts_core::encoding as core_encoding;
use safeparts_core::packet::SharePacket;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Encoding {
    Auto,
    Base64url,
    Base58check,
    MnemoWords,
    MnemoBip39,
}

impl Encoding {
    pub const ALL: &'static [Encoding] = &[
        Encoding::Auto,
        Encoding::Base64url,
        Encoding::Base58check,
        Encoding::MnemoWords,
        Encoding::MnemoBip39,
    ];

    pub const SPLIT: &'static [Encoding] = &[
        Encoding::Base64url,
        Encoding::Base58check,
        Encoding::MnemoWords,
        Encoding::MnemoBip39,
    ];

    pub fn label(self) -> &'static str {
        self.core().label()
    }

    fn core(self) -> core_encoding::Encoding {
        match self {
            Encoding::Auto => core_encoding::Encoding::Auto,
            Encoding::Base64url => core_encoding::Encoding::Base64url,
            Encoding::Base58check => core_encoding::Encoding::Base58check,
            Encoding::MnemoWords => core_encoding::Encoding::MnemoWords,
            Encoding::MnemoBip39 => core_encoding::Encoding::MnemoBip39,
        }
    }

    fn from_core(value: core_encoding::Encoding) -> Self {
        match value {
            core_encoding::Encoding::Auto => Encoding::Auto,
            core_encoding::Encoding::Base64url => Encoding::Base64url,
            core_encoding::Encoding::Base58check => Encoding::Base58check,
            core_encoding::Encoding::MnemoWords => Encoding::MnemoWords,
            core_encoding::Encoding::MnemoBip39 => Encoding::MnemoBip39,
            _ => Encoding::Auto,
        }
    }
}

pub fn split_secret(
    secret: &[u8],
    k: u8,
    n: u8,
    encoding: Encoding,
    passphrase: Option<&[u8]>,
) -> Result<(Vec<SharePacket>, Vec<String>)> {
    let packets = safeparts_core::split_secret(secret, k, n, passphrase)
        .map_err(|error| anyhow!(error.user_message()))
        .with_context(|| format!("split failed (k={k}, n={n})"))?;

    let shares = packets
        .iter()
        .map(|packet| {
            core_encoding::encode_packet(packet, encoding.core())
                .map_err(|error| anyhow!(error.user_message()))
        })
        .collect::<Result<Vec<_>>>()?;

    Ok((packets, shares))
}

pub fn combine_shares(
    input: &str,
    encoding: Encoding,
    passphrase: Option<&[u8]>,
) -> Result<(Vec<SharePacket>, Vec<u8>, Encoding)> {
    let parsed = core_encoding::parse_share_packets_wrapped_mnemonics(input, encoding.core())
        .map_err(|error| anyhow!(error.user_message()))?;
    let secret = safeparts_core::combine_shares(&parsed.packets, passphrase)
        .map_err(|error| anyhow!(error.user_message()))
        .context("combine failed")?;

    Ok((parsed.packets, secret, Encoding::from_core(parsed.encoding)))
}

pub fn set_id_hex(packets: &[SharePacket]) -> Option<String> {
    let first = packets.first()?;
    let mut s = String::with_capacity(32);
    for b in first.set_id.0 {
        s.push_str(&format!("{b:02x}"));
    }
    Some(s)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn labels_use_core_canonical_names() {
        assert_eq!(Encoding::Base64url.label(), "base64url");
        assert_eq!(Encoding::Base58check.label(), "base58check");
    }

    #[test]
    fn mnemonic_recovery_rejects_bad_extra_shares_without_echoing_input() {
        for encoding in [Encoding::MnemoWords, Encoding::MnemoBip39] {
            let (_, shares) = split_secret(b"synthetic primary set", 2, 3, encoding, None).unwrap();
            let (_, other) = split_secret(b"synthetic another set", 2, 3, encoding, None).unwrap();
            let words = shares[2].split_whitespace().collect::<Vec<_>>();
            let truncated = words[..words.len() - 1].join(" ");
            let corrupted = format!("SENSITIVE-INVALID-WORD {}", words[1..].join(" "));
            for bad in [&shares[0], &other[2], &truncated, &corrupted] {
                for separator in ["\n", "\n\n", "\r\n", "\r\n\r\n"] {
                    let input = [shares[0].as_str(), shares[1].as_str(), bad].join(separator);
                    for mode in [Encoding::Auto, encoding] {
                        let error = combine_shares(&input, mode, None).unwrap_err();
                        let message = format!("{error:#}");
                        assert!(!message.contains("SENSITIVE-INVALID-WORD"));
                        for share in &shares {
                            assert!(!message.contains(share));
                        }
                    }
                }
            }
        }
    }

    #[test]
    fn tui_recovers_every_desktop_encoding() {
        const FIXTURES: &[(&str, Encoding)] = &[
            (
                include_str!(
                    "../../safeparts_core/tests/fixtures/surface_interoperability/desktop/base64url.txt"
                ),
                Encoding::Base64url,
            ),
            (
                include_str!(
                    "../../safeparts_core/tests/fixtures/surface_interoperability/desktop/base58check.txt"
                ),
                Encoding::Base58check,
            ),
            (
                include_str!(
                    "../../safeparts_core/tests/fixtures/surface_interoperability/desktop/mnemo-words.txt"
                ),
                Encoding::MnemoWords,
            ),
            (
                include_str!(
                    "../../safeparts_core/tests/fixtures/surface_interoperability/desktop/mnemo-bip39.txt"
                ),
                Encoding::MnemoBip39,
            ),
        ];

        for &(fixture, expected_encoding) in FIXTURES {
            let (_, recovered, detected) = combine_shares(fixture, Encoding::Auto, None).unwrap();
            assert_eq!(detected, expected_encoding);
            assert_eq!(recovered, b"synthetic desktop interoperability");
        }
    }

    #[test]
    fn malformed_share_errors_do_not_echo_input() {
        let sensitive = "SECRET-SHARE-TEXT";
        let error = combine_shares(sensitive, Encoding::MnemoWords, None)
            .unwrap_err()
            .to_string();

        assert!(!error.contains(sensitive));
        assert!(error.contains("could not be decoded"));
    }
}

#[cfg(test)]
mod protected_desktop_interoperability {
    use super::*;
    #[test]
    fn tui_recovers_every_protected_desktop_encoding() {
        const FIXTURES: &[&str] = &[
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/base64url.txt"
            ),
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/base58check.txt"
            ),
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/mnemo-words.txt"
            ),
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/desktop/mnemo-bip39.txt"
            ),
        ];
        for fixture in FIXTURES {
            let (_, secret, _) = combine_shares(
                fixture,
                Encoding::Auto,
                Some(b"issue-142 synthetic interoperability passphrase"),
            )
            .unwrap();
            assert_eq!(secret, b"synthetic protected desktop interoperability");
        }
    }
}
