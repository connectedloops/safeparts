use safeparts_core::packet::{PacketVersion, SharePacket};
use safeparts_core::{inspect_share_set, split_secret};

#[test]
fn inspection_reports_expected_unprotected_secret_length_without_combining() {
    let packets = split_secret(b"synthetic desktop admission", 2, 3, None).unwrap();

    let inspected = inspect_share_set(&packets[..2]).unwrap();

    assert_eq!(inspected.threshold, 2);
    assert_eq!(inspected.share_count, 3);
    assert_eq!(inspected.supplied_count, 2);
    assert_eq!(inspected.expected_secret_len, 27);
    assert!(!inspected.passphrase_protected);
}

#[test]
fn binary_admission_retains_the_on_wire_packet_version() {
    let packet = &split_secret(b"synthetic version", 1, 1, None).unwrap()[0];
    let mut encoded = packet.encode_binary().unwrap();
    encoded[4] = 1;

    let decoded = SharePacket::decode_binary_with_version(&encoded).unwrap();

    assert_eq!(decoded.version, PacketVersion::V1);
    assert_eq!(decoded.packet.payload, packet.payload);
}

#[test]
fn inspection_rejects_duplicates_before_recovery() {
    let packets = split_secret(b"synthetic duplicate", 2, 3, None).unwrap();

    let error = inspect_share_set(&[packets[0].clone(), packets[0].clone()]).unwrap_err();

    assert!(matches!(
        error,
        safeparts_core::CoreError::DuplicateX { .. }
    ));
}
