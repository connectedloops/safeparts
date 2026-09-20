use safeparts_desktop_bridge::{Status, new_operation};

const FIDELITY_TEXT: &str =
    "\0 leading\nline\u{00a0}space\u{2028}separator\u{2029}paragraph\ne\u{301}\ntrailing \n";

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
