use safeparts_core::combine_shares;
use safeparts_core::encoding::{self, Encoding};

const DESKTOP_FIXTURES: &[(&str, Encoding)] = &[
    (
        include_str!("fixtures/surface_interoperability/desktop/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/surface_interoperability/desktop/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/surface_interoperability/desktop/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/surface_interoperability/desktop/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
];

#[test]
fn library_recovers_every_desktop_encoding() {
    for &(fixture, expected_encoding) in DESKTOP_FIXTURES {
        let parsed = encoding::parse_share_packets_wrapped_mnemonics(fixture, Encoding::Auto)
            .unwrap_or_else(|error| {
                panic!("{} fixture failed: {error}", expected_encoding.label())
            });
        assert_eq!(parsed.encoding, expected_encoding);
        assert_eq!(
            combine_shares(&parsed.packets, None).unwrap(),
            b"synthetic desktop interoperability"
        );
    }
}

const BINARY_SECRET: &[u8] = include_bytes!("fixtures/binary_surface_interoperability/secret.bin");
const BINARY_PASSPHRASE: &[u8] = b"issue-143 synthetic binary interoperability passphrase";

const BINARY_FIXTURES: &[(&str, Encoding)] = &[
    (
        include_str!("fixtures/binary_surface_interoperability/desktop/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/desktop/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/desktop/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/desktop/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/cli/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/cli/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/cli/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/cli/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/tui/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/tui/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/tui/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/tui/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/wasm/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/wasm/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/wasm/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/binary_surface_interoperability/wasm/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
];

#[test]
fn library_recovers_every_binary_surface_fixture_exactly() {
    for &(fixture, expected_encoding) in BINARY_FIXTURES {
        for requested in [Encoding::Auto, expected_encoding] {
            let parsed = encoding::parse_share_packets_wrapped_mnemonics(fixture, requested)
                .unwrap_or_else(|error| {
                    panic!(
                        "{} binary fixture failed: {error}",
                        expected_encoding.label()
                    )
                });
            assert_eq!(parsed.encoding, expected_encoding);
            assert_eq!(
                parsed
                    .packets
                    .iter()
                    .map(|packet| (packet.k, packet.n, packet.x))
                    .collect::<Vec<_>>(),
                vec![(2, 3, 1), (2, 3, 2), (2, 3, 3)]
            );
            assert_eq!(
                combine_shares(&parsed.packets, None).unwrap(),
                BINARY_SECRET
            );
        }
    }
}

#[test]
fn library_recovers_protected_binary_desktop_fixtures_exactly() {
    const FIXTURES: &[(&str, Encoding)] = &[
        (
            include_str!(
                "fixtures/binary_surface_interoperability/desktop-protected/base64url.txt"
            ),
            Encoding::Base64url,
        ),
        (
            include_str!(
                "fixtures/binary_surface_interoperability/desktop-protected/base58check.txt"
            ),
            Encoding::Base58check,
        ),
        (
            include_str!(
                "fixtures/binary_surface_interoperability/desktop-protected/mnemo-words.txt"
            ),
            Encoding::MnemoWords,
        ),
        (
            include_str!(
                "fixtures/binary_surface_interoperability/desktop-protected/mnemo-bip39.txt"
            ),
            Encoding::MnemoBip39,
        ),
    ];
    for &(fixture, expected_encoding) in FIXTURES {
        for requested in [Encoding::Auto, expected_encoding] {
            let parsed =
                encoding::parse_share_packets_wrapped_mnemonics(fixture, requested).unwrap();
            assert_eq!(parsed.encoding, expected_encoding);
            assert!(
                parsed
                    .packets
                    .iter()
                    .all(|packet| packet.crypto_params.is_some())
            );
            assert_eq!(
                combine_shares(&parsed.packets, Some(BINARY_PASSPHRASE)).unwrap(),
                BINARY_SECRET
            );
        }
    }
}

const PROTECTED_PASSPHRASE: &[u8] = b"issue-142 synthetic interoperability passphrase";

#[test]
fn library_recovers_every_protected_desktop_encoding() {
    const FIXTURES: &[(&str, Encoding)] = &[
        (
            include_str!("fixtures/protected_surface_interoperability/desktop/base64url.txt"),
            Encoding::Base64url,
        ),
        (
            include_str!("fixtures/protected_surface_interoperability/desktop/base58check.txt"),
            Encoding::Base58check,
        ),
        (
            include_str!("fixtures/protected_surface_interoperability/desktop/mnemo-words.txt"),
            Encoding::MnemoWords,
        ),
        (
            include_str!("fixtures/protected_surface_interoperability/desktop/mnemo-bip39.txt"),
            Encoding::MnemoBip39,
        ),
    ];
    for &(fixture, expected_encoding) in FIXTURES {
        let parsed =
            encoding::parse_share_packets_wrapped_mnemonics(fixture, Encoding::Auto).unwrap();
        assert_eq!(parsed.encoding, expected_encoding);
        assert!(
            parsed
                .packets
                .iter()
                .all(|packet| packet.crypto_params.is_some())
        );
        assert_eq!(
            parsed
                .packets
                .iter()
                .map(|p| (p.k, p.n, p.x))
                .collect::<Vec<_>>(),
            vec![(2, 3, 1), (2, 3, 2), (2, 3, 3)]
        );
        assert_eq!(
            combine_shares(&parsed.packets, Some(PROTECTED_PASSPHRASE)).unwrap(),
            b"synthetic protected desktop interoperability"
        );
    }
}
