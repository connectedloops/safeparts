use safeparts_core::crypto::CryptoParams;
use safeparts_core::encoding::{self, Encoding};
use safeparts_core::packet::{PacketVersion, SharePacket};
use safeparts_core::{combine_shares, sss::SetId};

const V1_SECRET: &[u8] = b"\x00Safeparts V1 synthetic compatibility Secret\xff\n";
const V2_SECRET: &[u8] = b"Safeparts V2 synthetic compatibility Secret\x00\x80\xfe";
const V2_PROTECTED_SECRET: &[u8] = b"\xffSafeparts V2 protected synthetic Secret\x00\x01\x02\n";
const V2_PASSPHRASE: &[u8] = b"issue-60 synthetic fixture passphrase";

const V1_FIXTURES: &[(&str, Encoding)] = &[
    (
        include_str!("fixtures/share_compatibility/v1-unprotected/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/share_compatibility/v1-unprotected/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/share_compatibility/v1-unprotected/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/share_compatibility/v1-unprotected/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
];

const V2_FIXTURES: &[(&str, Encoding)] = &[
    (
        include_str!("fixtures/share_compatibility/v2-unprotected/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/share_compatibility/v2-unprotected/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/share_compatibility/v2-unprotected/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/share_compatibility/v2-unprotected/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
];

const V2_PROTECTED_FIXTURES: &[(&str, Encoding)] = &[
    (
        include_str!("fixtures/share_compatibility/v2-passphrase-protected/base64url.txt"),
        Encoding::Base64url,
    ),
    (
        include_str!("fixtures/share_compatibility/v2-passphrase-protected/base58check.txt"),
        Encoding::Base58check,
    ),
    (
        include_str!("fixtures/share_compatibility/v2-passphrase-protected/mnemo-words.txt"),
        Encoding::MnemoWords,
    ),
    (
        include_str!("fixtures/share_compatibility/v2-passphrase-protected/mnemo-bip39.txt"),
        Encoding::MnemoBip39,
    ),
];

#[test]
fn v1_unprotected_fixtures_preserve_released_recovery() {
    assert_fixture_set(
        "V1 unprotected",
        V1_FIXTURES,
        ExpectedSet {
            set_id: [
                0x01, 0x00, 0x23, 0x98, 0x7c, 0x50, 0x5f, 0xce, 0x03, 0x2d, 0xbb, 0xe9, 0xda, 0xc9,
                0x6f, 0xb9,
            ],
            payload_len: 78,
            crypto_params: None,
            secret: V1_SECRET,
            passphrase: None,
        },
    );
}

#[test]
fn v2_unprotected_fixtures_preserve_released_recovery() {
    assert_fixture_set(
        "V2 unprotected",
        V2_FIXTURES,
        ExpectedSet {
            set_id: [
                0xac, 0x70, 0x98, 0x67, 0x70, 0x2c, 0xc0, 0xe1, 0x19, 0x67, 0x6a, 0x43, 0xd7, 0xc5,
                0x28, 0xc8,
            ],
            payload_len: 78,
            crypto_params: None,
            secret: V2_SECRET,
            passphrase: None,
        },
    );
}

#[test]
fn version_preserving_parser_covers_every_released_encoding() {
    assert_fixture_versions(V1_FIXTURES, PacketVersion::V1);
    assert_fixture_versions(V2_FIXTURES, PacketVersion::V2);
    assert_fixture_versions(V2_PROTECTED_FIXTURES, PacketVersion::V2);
}

#[test]
fn v2_passphrase_protected_fixtures_preserve_released_recovery() {
    assert_fixture_set(
        "V2 passphrase-protected",
        V2_PROTECTED_FIXTURES,
        ExpectedSet {
            set_id: [
                0xa6, 0x24, 0x94, 0x1f, 0xa7, 0xa4, 0xcf, 0xd1, 0x9c, 0x25, 0xd0, 0xab, 0x2a, 0x99,
                0x06, 0x1f,
            ],
            payload_len: 92,
            crypto_params: Some(CryptoParams {
                salt: [
                    0x6e, 0x1e, 0x79, 0x6a, 0x20, 0x25, 0xbb, 0xc3, 0x70, 0x27, 0x0a, 0x37, 0xdb,
                    0xb1, 0x8c, 0xd5,
                ],
                nonce: [
                    0x82, 0x23, 0x4e, 0x7f, 0xed, 0x0e, 0x21, 0x33, 0x53, 0x49, 0xe2, 0xc4,
                ],
                mem_cost_kib: 65_536,
                time_cost: 3,
                parallelism: 1,
            }),
            secret: V2_PROTECTED_SECRET,
            passphrase: Some(V2_PASSPHRASE),
        },
    );
}

#[derive(Clone, Copy)]
struct ExpectedSet {
    set_id: [u8; 16],
    payload_len: usize,
    crypto_params: Option<CryptoParams>,
    secret: &'static [u8],
    passphrase: Option<&'static [u8]>,
}

fn assert_fixture_set(name: &str, fixtures: &[(&str, Encoding)], expected: ExpectedSet) {
    assert_eq!(fixtures.len(), Encoding::CONCRETE.len());
    for concrete in Encoding::CONCRETE {
        assert!(
            fixtures.iter().any(|(_, encoding)| encoding == concrete),
            "{name}: missing {} fixture",
            concrete.label()
        );
    }

    for &(text, share_encoding) in fixtures {
        let parsed = encoding::parse_share_packets(text, share_encoding).unwrap();
        assert_eq!(parsed.encoding, share_encoding, "{name}: detected encoding");
        assert_metadata(name, &parsed.packets, expected);

        let recovered = combine_shares(&parsed.packets, expected.passphrase).unwrap();
        assert_eq!(
            recovered.as_slice(),
            expected.secret,
            "{name}: recovered bytes"
        );
    }
}

fn assert_fixture_versions(fixtures: &[(&str, Encoding)], expected: PacketVersion) {
    for &(text, encoding) in fixtures {
        for requested in [encoding, Encoding::Auto] {
            let parsed =
                encoding::parse_share_packets_wrapped_mnemonics_with_versions(text, requested)
                    .unwrap();
            assert_eq!(parsed.encoding, encoding);
            assert!(parsed.packets.iter().all(|item| item.version == expected));
        }
    }
}

fn assert_metadata(name: &str, packets: &[SharePacket], expected: ExpectedSet) {
    assert_eq!(packets.len(), 3, "{name}: Recovery share count");

    for (offset, packet) in packets.iter().enumerate() {
        assert_eq!(packet.set_id, SetId(expected.set_id), "{name}: set ID");
        assert_eq!(packet.k, 2, "{name}: Threshold");
        assert_eq!(packet.n, 3, "{name}: Share count");
        assert_eq!(usize::from(packet.x), offset + 1, "{name}: share index");
        assert_eq!(
            packet.payload.len(),
            expected.payload_len,
            "{name}: packet length"
        );
        assert_eq!(
            packet.crypto_params, expected.crypto_params,
            "{name}: Passphrase protection metadata"
        );
    }
}
