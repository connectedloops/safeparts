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
fn public_operation_bounds_paste_tokens_and_retained_recovery_input() {
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
    for batch_count in 1..=10 {
        let retained = operation.add_recovery_words(3, &maximum_batch);
        assert!(retained.status == Status::MalformedInput);
        assert_eq!(retained.recovery_batch_count, batch_count);
    }
    let rejected = operation.add_recovery_words(3, &maximum_batch);
    assert!(rejected.status == Status::RetainedInputTooLarge);
    assert_eq!(rejected.recovery_batch_count, 10);
}
