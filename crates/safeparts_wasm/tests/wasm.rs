#![cfg(target_arch = "wasm32")]

use js_sys::{Array, Reflect, Uint8Array};
use safeparts_wasm::{
    combine_share_input, combine_shares, inspect_share, inspect_share_input, share_threshold,
    split_secret,
};
use wasm_bindgen::{JsCast, JsValue};
use wasm_bindgen_test::*;

wasm_bindgen_test_configure!(run_in_browser);

fn error_json(error: &JsValue) -> String {
    js_sys::JSON::stringify(error).unwrap().as_string().unwrap()
}

#[wasm_bindgen_test]
fn every_encoding_round_trips_binary_secrets() {
    let secret = [0, 255, 3, 128];

    for encoding in ["base64url", "base58check", "mnemo-words", "mnemo-bip39"] {
        let shares = split_secret(&secret, 2, 3, encoding, None).unwrap();
        let selected = Array::new();
        selected.push(&shares.get(0));
        selected.push(&shares.get(1));

        let recovered = combine_shares(selected, "auto", None).unwrap();
        assert_eq!(recovered.to_vec(), secret);
    }
}

#[wasm_bindgen_test]
fn passphrase_failures_and_success_are_reported() {
    let shares = split_secret(
        b"synthetic protected wasm secret",
        2,
        3,
        "base64url",
        Some("correct".to_string()),
    )
    .unwrap();
    let selected = || {
        let values = Array::new();
        values.push(&shares.get(0));
        values.push(&shares.get(1));
        values
    };

    let missing = combine_shares(selected(), "auto", None).unwrap_err();
    assert_eq!(error_json(&missing), r#"{"code":"passphrase_required"}"#);

    let wrong = combine_shares(selected(), "auto", Some("wrong".to_string())).unwrap_err();
    assert_eq!(error_json(&wrong), r#"{"code":"decryption_failed"}"#);

    let insufficient = Array::new();
    insufficient.push(&shares.get(0));
    let insufficient =
        combine_shares(insufficient, "auto", Some("correct".to_string())).unwrap_err();
    assert_eq!(
        error_json(&insufficient),
        r#"{"code":"insufficient_shares","required":2,"provided":1}"#
    );

    assert_eq!(
        combine_shares(selected(), "auto", Some("correct".to_string()))
            .unwrap()
            .to_vec(),
        b"synthetic protected wasm secret"
    );
}

#[wasm_bindgen_test]
fn auto_inspection_reports_the_concrete_encoding() {
    let shares = split_secret(b"inspect wasm share", 2, 3, "base58check", None).unwrap();
    let share = shares.get(0).as_string().unwrap();
    assert_eq!(share_threshold(&share, "auto").unwrap(), 2);

    let info = inspect_share(&share, "auto").unwrap();
    let encoding = Reflect::get(&info, &JsValue::from_str("encoding"))
        .unwrap()
        .as_string()
        .unwrap();

    assert_eq!(encoding, "base58check");

    let combined_input = format!("{}\n{}", share, shares.get(1).as_string().unwrap());
    let combined_info = inspect_share_input(&combined_input, "auto").unwrap();
    let share_count = Reflect::get(&combined_info, &JsValue::from_str("shareCount"))
        .unwrap()
        .as_f64()
        .unwrap();
    assert_eq!(share_count, 2.0);
}

#[wasm_bindgen_test]
fn malformed_values_and_share_text_are_rejected_safely() {
    let values = Array::new();
    values.push(&JsValue::from_f64(42.0));
    assert!(combine_shares(values, "auto", None).is_err());

    let sensitive = "SECRET-SHARE-TEXT";
    let error = combine_share_input(sensitive, "mnemo-words", None).unwrap_err();
    assert_eq!(error_json(&error), r#"{"code":"invalid_share"}"#);
}

#[wasm_bindgen_test]
fn recovered_bytes_are_returned_as_a_uint8_array() {
    let shares = split_secret(b"typed wasm output", 1, 1, "base64url", None).unwrap();
    let recovered = combine_shares(shares, "auto", None).unwrap();
    let value: JsValue = recovered.into();

    assert!(value.is_instance_of::<Uint8Array>());
}
