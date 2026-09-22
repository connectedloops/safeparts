use base64::Engine;
use safeparts_core::encoding::{Encoding, encode_packet};
use safeparts_core::split_secret;
use safeparts_desktop_bridge::{ShareEncoding, Status, new_operation};

const FIDELITY_TEXT: &str = "\0 leading\nline\u{00a0}space\u{2028}separator\u{2029}paragraph\ne\u{301} \u{1f600}\ntrailing \n";

#[test]
fn public_operation_round_trips_exact_utf8_and_encodes_on_demand() {
    let mut operation = new_operation();
    let created = operation.create_words(1, FIDELITY_TEXT.as_bytes(), 2, 3);
    assert!(created.status == Status::Ok);
    assert_eq!(created.share_count, 3);

    let first = operation.encode_share(1, 0);
    let second = operation.encode_share(1, 1);
    assert!(first.status == Status::Ok);
    assert!(second.status == Status::Ok);

    operation.reset(2);
    let first_inspection = operation.add_recovery_words(2, &first.bytes);
    assert!(first_inspection.status == Status::NotEnoughShares);
    assert!(!first_inspection.ready);
    let ready = operation.add_recovery_words(2, &second.bytes);
    assert!(ready.status == Status::Ok);
    assert!(ready.ready);

    let recovered = operation.recover_words(2);
    assert!(recovered.status == Status::Ok);
    assert_eq!(recovered.bytes, FIDELITY_TEXT.as_bytes());
}

#[test]
fn public_operation_rejects_duplicate_and_preserves_correctable_input() {
    let mut operation = new_operation();
    assert!(
        operation
            .create_words(7, b"synthetic duplicate", 2, 3)
            .status
            == Status::Ok
    );
    let first = operation.encode_share(7, 0);

    operation.reset(8);
    assert!(operation.add_recovery_words(8, &first.bytes).status == Status::NotEnoughShares);
    let duplicate = operation.add_recovery_words(8, &first.bytes);
    assert!(duplicate.status == Status::DuplicateShare);
    assert_eq!(duplicate.recovery_batch_count, 2);

    let corrected = operation.remove_recovery_batch(8, 1);
    assert!(corrected.status == Status::NotEnoughShares);
    assert_eq!(corrected.recovery_batch_count, 1);
}

#[test]
fn public_operation_rejects_trailing_mixed_sets_and_encodings() {
    let mut first_set = new_operation();
    assert!(
        first_set
            .create_words(1, b"synthetic first set", 2, 3)
            .status
            == Status::Ok
    );
    let first = first_set.encode_share(1, 0);

    let mut trailing = first.bytes.clone();
    trailing.extend_from_slice(b" abandon");
    let mut recovery = new_operation();
    assert!(recovery.add_recovery_words(2, &trailing).status == Status::MalformedInput);
    let failed = recovery.recover_words(2);
    assert!(failed.status == Status::MalformedInput);
    assert!(failed.bytes.is_empty());

    let mut second_set = new_operation();
    assert!(
        second_set
            .create_words(3, b"synthetic second set", 2, 3)
            .status
            == Status::Ok
    );
    let second = second_set.encode_share(3, 0);
    recovery.reset(4);
    assert!(recovery.add_recovery_words(4, &first.bytes).status == Status::NotEnoughShares);
    let malformed_after_valid = recovery.add_recovery_words(4, b"abandon");
    assert!(malformed_after_valid.status == Status::MalformedInput);
    assert_eq!(malformed_after_valid.recovery_batch_count, 2);
    let corrected = recovery.remove_recovery_batch(4, 1);
    assert!(corrected.status == Status::NotEnoughShares);
    assert_eq!(corrected.recovery_batch_count, 1);
    assert!(recovery.add_recovery_words(4, &second.bytes).status == Status::MixedShareSet);

    let packet = split_secret(b"synthetic mixed encoding", 1, 1, None)
        .unwrap_or_else(|error| panic!("synthetic split failed: {error}"));
    let words = encode_packet(&packet[0], Encoding::MnemoWords)
        .unwrap_or_else(|error| panic!("synthetic encode failed: {error}"));
    let base64 = encode_packet(&packet[0], Encoding::Base64url)
        .unwrap_or_else(|error| panic!("synthetic encode failed: {error}"));
    let mut mixed_encoding = new_operation();
    assert!(
        mixed_encoding
            .add_recovery(5, words.as_bytes(), ShareEncoding::Auto)
            .status
            == Status::Ok
    );
    assert!(
        mixed_encoding
            .add_recovery(5, base64.as_bytes(), ShareEncoding::Auto)
            .status
            == Status::MixedEncoding
    );

    let same_batch = format!("{words}\n\n{base64}");
    let mut mixed_batch = new_operation();
    let rejected = mixed_batch.add_recovery(6, same_batch.as_bytes(), ShareEncoding::Auto);
    assert!(rejected.status == Status::MixedEncoding);
    assert!(!rejected.ready);
    assert_eq!(rejected.recovery_batch_count, 1);
}

#[test]
fn public_operation_creates_and_recovers_every_encoding_and_released_version() {
    const ENCODINGS: &[(Encoding, ShareEncoding)] = &[
        (Encoding::Base64url, ShareEncoding::Base64url),
        (Encoding::Base58check, ShareEncoding::Base58check),
        (Encoding::MnemoWords, ShareEncoding::MnemoWords),
        (Encoding::MnemoBip39, ShareEncoding::MnemoBip39),
    ];
    for &(core_encoding, bridge_encoding) in ENCODINGS {
        let mut created = new_operation();
        assert!(
            created
                .create(1, FIDELITY_TEXT.as_bytes(), 2, 3, bridge_encoding)
                .status
                == Status::Ok
        );
        let first = created.encode_share(1, 0);
        let parsed = safeparts_core::encoding::decode_packet(
            std::str::from_utf8(&first.bytes).unwrap_or(""),
            core_encoding,
        );
        assert!(parsed.is_ok());
    }

    const FIXTURES: &[(&str, ShareEncoding)] = &[
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/base64url.txt"
            ),
            ShareEncoding::Base64url,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/base58check.txt"
            ),
            ShareEncoding::Base58check,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/mnemo-words.txt"
            ),
            ShareEncoding::MnemoWords,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/mnemo-bip39.txt"
            ),
            ShareEncoding::MnemoBip39,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/base64url.txt"
            ),
            ShareEncoding::Base64url,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/base58check.txt"
            ),
            ShareEncoding::Base58check,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/mnemo-words.txt"
            ),
            ShareEncoding::MnemoWords,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/mnemo-bip39.txt"
            ),
            ShareEncoding::MnemoBip39,
        ),
    ];
    for &(fixture, manual) in FIXTURES {
        for requested in [ShareEncoding::Auto, manual] {
            let mut recovery = new_operation();
            let inspected = recovery.add_recovery(2, fixture.as_bytes(), requested);
            assert!(inspected.status == Status::Ok);
            assert!(inspected.ready);
            assert!(inspected.encoding == manual);
            assert!(recovery.recover(2).status == Status::InvalidUtf8);
        }
    }
}

#[test]
fn desktop_recovers_every_supported_surface_encoding() {
    const FIXTURES: &[(&str, &[u8])] = &[
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/cli/base64url.txt"
            ),
            b"synthetic CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/cli/base58check.txt"
            ),
            b"synthetic CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/cli/mnemo-words.txt"
            ),
            b"synthetic CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/cli/mnemo-bip39.txt"
            ),
            b"synthetic CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/tui/base64url.txt"
            ),
            b"synthetic TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/tui/base58check.txt"
            ),
            b"synthetic TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/tui/mnemo-words.txt"
            ),
            b"synthetic TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/tui/mnemo-bip39.txt"
            ),
            b"synthetic TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/web/base64url.txt"
            ),
            b"synthetic web interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/web/base58check.txt"
            ),
            b"synthetic web interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/web/mnemo-words.txt"
            ),
            b"synthetic web interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/surface_interoperability/web/mnemo-bip39.txt"
            ),
            b"synthetic web interoperability",
        ),
    ];

    for &(fixture, expected) in FIXTURES {
        let mut operation = new_operation();
        let inspected = operation.add_recovery(30, fixture.as_bytes(), ShareEncoding::Auto);
        assert!(inspected.status == Status::Ok);
        assert!(inspected.ready);
        assert_eq!(operation.recover(30).bytes, expected);
    }
}

#[test]
fn public_operation_recognizes_protected_shares_without_recovering() {
    const PROTECTED: &[(&str, ShareEncoding)] = &[
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/base64url.txt"
            ),
            ShareEncoding::Base64url,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/base58check.txt"
            ),
            ShareEncoding::Base58check,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/mnemo-words.txt"
            ),
            ShareEncoding::MnemoWords,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/mnemo-bip39.txt"
            ),
            ShareEncoding::MnemoBip39,
        ),
    ];
    for &(protected, expected_encoding) in PROTECTED {
        let mut operation = new_operation();
        let inspected = operation.add_recovery(3, protected.as_bytes(), ShareEncoding::Auto);
        assert!(inspected.status == Status::PassphraseRequired);
        assert!(inspected.passphrase_protected);
        assert!(!inspected.ready);
        assert!(inspected.encoding == expected_encoding);
        assert!(operation.recover(3).status == Status::PassphraseRequired);
    }
}

#[test]
fn public_operation_rejects_mixed_versions_and_honors_manual_encoding() {
    let v1 = include_str!(
        "../../safeparts_core/tests/fixtures/share_compatibility/v1-unprotected/mnemo-words.txt"
    )
    .lines()
    .next()
    .unwrap_or("");
    let v2 = include_str!(
        "../../safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/mnemo-words.txt"
    )
    .lines()
    .next()
    .unwrap_or("");
    let mut mixed = new_operation();
    assert!(
        mixed
            .add_recovery(4, v1.as_bytes(), ShareEncoding::Auto)
            .status
            == Status::NotEnoughShares
    );
    assert!(
        mixed
            .add_recovery(4, v2.as_bytes(), ShareEncoding::Auto)
            .status
            == Status::MixedVersion
    );

    let base64 = include_str!(
        "../../safeparts_core/tests/fixtures/share_compatibility/v2-unprotected/base64url.txt"
    );
    let mut wrong_manual = new_operation();
    assert!(
        wrong_manual
            .add_recovery(5, base64.as_bytes(), ShareEncoding::MnemoWords)
            .status
            == Status::MalformedInput
    );
}

#[test]
fn public_operation_reports_unsupported_versions_separately() {
    let packet = split_secret(b"synthetic unsupported version", 1, 1, None)
        .unwrap_or_else(|error| panic!("synthetic split failed: {error}"));
    let mut binary = packet[0]
        .encode_binary()
        .unwrap_or_else(|error| panic!("synthetic packet encode failed: {error}"));
    binary[4] = 99;
    let encoded = base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(binary);
    let mut operation = new_operation();
    let inspected = operation.add_recovery(10, encoded.as_bytes(), ShareEncoding::Base64url);
    assert!(inspected.status == Status::UnsupportedVersion);
    assert!(!inspected.ready);
    assert_eq!(inspected.recovery_batch_count, 1);
    assert!(operation.recover(10).status == Status::UnsupportedVersion);

    let exact_cxx_fixture = b"U01OMWMAAgMBrHCYZ3AswOEZZ2pD18UoyAAAAE4PXdVG9ykm_tyf_J5-HJ-_0WtdkvNhn-0vZQL6QwI6H7UZR6ES2tASrurEc-tXOUXN_QPIZSMQAQ1BHoHSBc8k5MjGtaOkiHOGApBppcI";
    let mut exact_operation = new_operation();
    assert!(
        exact_operation
            .add_recovery(11, exact_cxx_fixture, ShareEncoding::Base64url)
            .status
            == Status::UnsupportedVersion
    );
}

#[test]
fn public_operation_rejects_reconstructed_non_utf8_without_returning_bytes() {
    let packets = split_secret(&[0xff, 0xfe, 0xfd], 2, 3, None)
        .unwrap_or_else(|error| panic!("synthetic split failed: {error}"));
    let first = encode_packet(&packets[0], Encoding::MnemoWords)
        .unwrap_or_else(|error| panic!("synthetic encode failed: {error}"));
    let second = encode_packet(&packets[1], Encoding::MnemoWords)
        .unwrap_or_else(|error| panic!("synthetic encode failed: {error}"));

    let mut operation = new_operation();
    assert!(operation.add_recovery_words(1, first.as_bytes()).status == Status::NotEnoughShares);
    assert!(operation.add_recovery_words(1, second.as_bytes()).status == Status::Ok);
    let rejected = operation.recover_words(1);
    assert!(rejected.status == Status::InvalidUtf8);
    assert!(rejected.bytes.is_empty());
    assert!(operation.recovered_text(1).status == Status::NotEnoughShares);
}

#[test]
fn public_operation_covers_threshold_endpoints_and_reordered_subsets() {
    for (threshold, share_count) in [(1, 1), (1, 255), (255, 255)] {
        let mut operation = new_operation();
        let created = operation.create(20, b"x", threshold, share_count, ShareEncoding::Base64url);
        assert!(created.status == Status::Ok);
        assert_eq!(created.threshold, threshold);
        assert_eq!(created.share_count, u16::from(share_count));
        assert!(
            operation
                .encode_share(20, u16::from(share_count - 1))
                .status
                == Status::Ok
        );
    }

    let mut created = new_operation();
    assert!(
        created
            .create(
                21,
                b"shuffled non-leading subset",
                3,
                5,
                ShareEncoding::MnemoWords
            )
            .status
            == Status::Ok
    );
    let fifth = created.encode_share(21, 4);
    let second = created.encode_share(21, 1);
    let fourth = created.encode_share(21, 3);
    let mut recovery = new_operation();
    for share in [&fifth.bytes, &second.bytes] {
        assert!(
            recovery.add_recovery(22, share, ShareEncoding::Auto).status == Status::NotEnoughShares
        );
    }
    let ready = recovery.add_recovery(22, &fourth.bytes, ShareEncoding::Auto);
    assert!(ready.status == Status::Ok);
    assert!(ready.ready);
    assert_eq!(recovery.recover(22).bytes, b"shuffled non-leading subset");

    let mut over_count_source = new_operation();
    assert!(
        over_count_source
            .create(23, b"over count", 1, 2, ShareEncoding::Base64url)
            .status
            == Status::Ok
    );
    let first = over_count_source.encode_share(23, 0);
    let mut over_count = new_operation();
    assert!(
        over_count
            .add_recovery(24, &first.bytes, ShareEncoding::Auto)
            .status
            == Status::Ok
    );
    assert!(
        over_count
            .add_recovery(24, &first.bytes, ShareEncoding::Auto)
            .status
            == Status::DuplicateShare
    );
    assert!(
        over_count
            .add_recovery(24, &first.bytes, ShareEncoding::Auto)
            .status
            == Status::TooManyShares
    );
}

#[test]
fn public_operation_keeps_maximum_create_exportable_in_every_encoding() {
    const ENCODINGS: &[ShareEncoding] = &[
        ShareEncoding::Base64url,
        ShareEncoding::Base58check,
        ShareEncoding::MnemoWords,
        ShareEncoding::MnemoBip39,
    ];
    let maximum_secret = vec![b'm'; 1_048_576];
    for &encoding in ENCODINGS {
        let mut operation = new_operation();
        assert!(
            operation
                .create(25, &maximum_secret, 2, 16, encoding)
                .status
                == Status::Ok
        );
        if encoding == ShareEncoding::MnemoWords {
            for index in 0..16 {
                let share = operation.encode_share(25, index);
                assert!(share.status == Status::Ok);
                assert!(!share.bytes.is_empty());
                assert!(share.bytes.len() <= 8 * 1_048_576);
            }
        }
    }

    // Exercise both ends of the identity range for every encoder without turning
    // this correctness test into the exhaustive maximum-workload benchmark owned
    // by issue 147 (BIP-39 maximum output contains tens of thousands of frames).
    let representative_secret = vec![b'r'; 1_024];
    for &encoding in ENCODINGS {
        let mut operation = new_operation();
        assert!(
            operation
                .create(26, &representative_secret, 2, 16, encoding)
                .status
                == Status::Ok
        );
        for index in [0, 15] {
            let share = operation.encode_share(26, index);
            assert!(share.status == Status::Ok);
            assert!(!share.bytes.is_empty());
        }
    }
}

#[test]
fn public_operation_enforces_create_admission_before_core_work() {
    let mut operation = new_operation();
    assert!(operation.create_words(1, b"", 2, 3).status == Status::EmptySecret);
    assert!(operation.create_words(2, &[b'x'; 1_048_577], 2, 3).status == Status::SecretTooLarge);
    assert!(
        operation.create_words(3, &[b'x'; 70_000], 2, 241).status == Status::LogicalVolumeTooLarge
    );

    let admitted = operation.create_words(4, &[b'x'; 1_048_576], 2, 16);
    assert!(admitted.status == Status::Ok);
    let encoded = operation.encode_share(4, 0);
    assert!(encoded.status == Status::Ok);
    assert!(encoded.bytes.len() <= 8 * 1_048_576);
    assert!(
        operation.create_words(5, &[b'x'; 1_048_576], 2, 17).status
            == Status::LogicalVolumeTooLarge
    );
}

#[test]
fn public_operation_preflights_dense_compact_decoder_storage() {
    const DENSE_BYTES: usize = 8 * 1_048_576;
    for (encoding, byte) in [
        (ShareEncoding::Base64url, b'A'),
        (ShareEncoding::Base58check, b'1'),
    ] {
        let mut operation = new_operation();
        let dense = vec![byte; DENSE_BYTES];
        let mut retained_count = 0;
        loop {
            let result = operation.add_recovery(9, &dense, encoding);
            if result.status == Status::ResourceLimit {
                assert_eq!(usize::from(result.recovery_batch_count), retained_count);
                assert!(retained_count < 20);
                break;
            }
            assert!(result.status == Status::MalformedInput);
            retained_count += 1;
            assert_eq!(usize::from(result.recovery_batch_count), retained_count);
            assert!(
                retained_count < 20,
                "resource admission did not precede retained-input limit"
            );
        }
    }
}

#[test]
fn public_operation_bounds_paste_tokens_and_recovery_memory() {
    let mut operation = new_operation();
    assert!(operation.create_words(0, &[b'm'; 1_048_576], 2, 16).status == Status::Ok);
    let too_large = vec![b'x'; 16 * 1_048_576 + 1];
    let rejected = operation.add_recovery_words(1, &too_large);
    assert!(rejected.status == Status::PasteTooLarge);
    assert_eq!(rejected.recovery_batch_count, 0);

    let too_many_tokens = "x ".repeat(1_048_577);
    let rejected = operation.add_recovery_words(2, too_many_tokens.as_bytes());
    assert!(rejected.status == Status::TokenLimit);
    assert_eq!(rejected.recovery_batch_count, 0);

    let maximum_batch = vec![b'x'; 16 * 1_048_576];
    let mut retained_count = 0;
    loop {
        let result = operation.add_recovery_words(3, &maximum_batch);
        if result.status == Status::ResourceLimit {
            assert_eq!(usize::from(result.recovery_batch_count), retained_count);
            assert!(retained_count < 10);
            break;
        }
        assert!(result.status == Status::MalformedInput);
        retained_count += 1;
        assert_eq!(usize::from(result.recovery_batch_count), retained_count);
    }

    // The conservative process budget may reject adversarial recovery text
    // before the independent 160 MiB retained-input ceiling. Rejection must not
    // retain the incoming batch.
    let rejected = operation.add_recovery_words(3, &maximum_batch);
    assert!(rejected.status == Status::ResourceLimit);
    assert_eq!(usize::from(rejected.recovery_batch_count), retained_count);
}

#[test]
fn protected_operation_preserves_exact_passphrase_and_safe_failures_in_all_encodings() {
    const SECRET: &[u8] = b"synthetic protected desktop secret";
    const PASSPHRASE: &[u8] = "  cafe\u{301} 🔐  ".as_bytes();
    for encoding in [
        ShareEncoding::Base64url,
        ShareEncoding::Base58check,
        ShareEncoding::MnemoWords,
        ShareEncoding::MnemoBip39,
    ] {
        let mut operation = new_operation();
        let created = operation.create_with_passphrase(1, SECRET, 2, 3, encoding, PASSPHRASE);
        assert!(created.status == Status::Ok);
        let first = operation.encode_share(1, 0);
        let second = operation.encode_share(1, 1);

        operation.reset(2);
        operation.add_recovery(2, &first.bytes, ShareEncoding::Auto);
        let inspected = operation.add_recovery(2, &second.bytes, ShareEncoding::Auto);
        assert!(inspected.status == Status::PassphraseRequired);
        assert!(inspected.passphrase_protected);
        assert!(!inspected.ready);

        let missing = operation.recover_with_passphrase(2, b"");
        assert!(missing.status == Status::PassphraseRequired);
        assert!(missing.bytes.is_empty());
        let wrong = operation.recover_with_passphrase(2, "  cafe 🔐  ".as_bytes());
        assert!(wrong.status == Status::IntegrityFailure);
        assert!(wrong.bytes.is_empty());
        let recovered = operation.recover_with_passphrase(2, PASSPHRASE);
        assert!(recovered.status == Status::Ok);
        assert_eq!(recovered.bytes, SECRET);
    }
}

#[test]
fn passphrase_utf8_and_inclusive_byte_boundary_are_enforced_publicly() {
    let invalid_utf8 = [0xff];
    let mut invalid = new_operation();
    assert!(
        invalid
            .create_with_passphrase(
                1,
                b"synthetic invalid utf8",
                1,
                1,
                ShareEncoding::Base64url,
                &invalid_utf8,
            )
            .status
            == Status::InvalidUtf8
    );
    assert!(invalid.recover_with_passphrase(1, &invalid_utf8).status == Status::InvalidUtf8);

    let maximum = vec![b'x'; 1_048_576];
    let mut boundary = new_operation();
    assert!(
        boundary
            .create_with_passphrase(
                2,
                b"synthetic exact passphrase boundary",
                1,
                1,
                ShareEncoding::Base64url,
                &maximum,
            )
            .status
            == Status::Ok
    );
}

#[test]
fn passphrases_over_the_inclusive_limit_are_rejected() {
    let too_large = vec![b'x'; 1_048_577];
    let mut operation = new_operation();
    let created = operation.create_with_passphrase(
        1,
        b"synthetic limit",
        1,
        1,
        ShareEncoding::Base64url,
        &too_large,
    );
    assert!(created.status == Status::PassphraseTooLarge);
    assert!(operation.recover_with_passphrase(2, &too_large).status == Status::PassphraseTooLarge);
}

#[test]
fn desktop_recovers_every_protected_supported_surface_encoding() {
    const PASS: &[u8] = b"issue-142 synthetic interoperability passphrase";
    const FIXTURES: &[(&str, &[u8])] = &[
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/cli/base64url.txt"
            ),
            b"synthetic protected CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/cli/base58check.txt"
            ),
            b"synthetic protected CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/cli/mnemo-words.txt"
            ),
            b"synthetic protected CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/cli/mnemo-bip39.txt"
            ),
            b"synthetic protected CLI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/tui/base64url.txt"
            ),
            b"synthetic protected TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/tui/base58check.txt"
            ),
            b"synthetic protected TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/tui/mnemo-words.txt"
            ),
            b"synthetic protected TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/tui/mnemo-bip39.txt"
            ),
            b"synthetic protected TUI interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/wasm/base64url.txt"
            ),
            b"synthetic protected WASM interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/wasm/base58check.txt"
            ),
            b"synthetic protected WASM interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/wasm/mnemo-words.txt"
            ),
            b"synthetic protected WASM interoperability",
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/protected_surface_interoperability/wasm/mnemo-bip39.txt"
            ),
            b"synthetic protected WASM interoperability",
        ),
    ];
    for &(fixture, expected) in FIXTURES {
        let mut operation = new_operation();
        let inspected = operation.add_recovery(80, fixture.as_bytes(), ShareEncoding::Auto);
        assert!(inspected.status == Status::PassphraseRequired);
        assert!(inspected.passphrase_protected);
        let recovered = operation.recover_with_passphrase(80, PASS);
        assert!(recovered.status == Status::Ok);
        assert_eq!(recovered.bytes, expected);
    }
}

#[test]
fn released_protected_fixture_recovers_exact_binary_and_metadata() {
    const PASS: &[u8] = b"issue-60 synthetic fixture passphrase";
    const EXPECTED: &[u8] = b"\xffSafeparts V2 protected synthetic Secret\0\x01\x02\n";
    const FIXTURES: &[(&str, ShareEncoding)] = &[
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/base64url.txt"
            ),
            ShareEncoding::Base64url,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/base58check.txt"
            ),
            ShareEncoding::Base58check,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/mnemo-words.txt"
            ),
            ShareEncoding::MnemoWords,
        ),
        (
            include_str!(
                "../../safeparts_core/tests/fixtures/share_compatibility/v2-passphrase-protected/mnemo-bip39.txt"
            ),
            ShareEncoding::MnemoBip39,
        ),
    ];
    for &(fixture, encoding) in FIXTURES {
        for requested in [ShareEncoding::Auto, encoding] {
            let mut op = new_operation();
            let inspected = op.add_recovery(90, fixture.as_bytes(), requested);
            assert!(inspected.status == Status::PassphraseRequired);
            assert!(inspected.passphrase_protected);
            assert!(inspected.encoding == encoding);
            let recovered = op.recover_bytes_with_passphrase(90, PASS);
            assert!(recovered.status == Status::Ok);
            assert_eq!(recovered.bytes, EXPECTED);
            let text = op.recovered_text(91);
            assert!(text.status == Status::InvalidUtf8);
            assert!(text.bytes.is_empty());
        }
    }
}
