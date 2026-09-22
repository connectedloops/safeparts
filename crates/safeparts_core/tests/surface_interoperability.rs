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
