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
