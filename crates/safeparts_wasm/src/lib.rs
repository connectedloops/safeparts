use std::fmt::Display;

use js_sys::{Array, Object, Reflect, Uint8Array};
use safeparts_core::CoreError;
use safeparts_core::encoding::{self, Encoding};
use safeparts_core::packet::SharePacket;
use wasm_bindgen::prelude::*;
use zeroize::Zeroizing;

#[wasm_bindgen]
pub fn share_threshold(share: &str, encoding: &str) -> Result<u8, JsValue> {
    let packet = decode_packet(share, encoding).map_err(js_error)?;
    Ok(packet.k)
}

#[wasm_bindgen]
pub fn split_secret(
    secret: &[u8],
    k: u8,
    n: u8,
    encoding: &str,
    passphrase: Option<String>,
) -> Result<Array, JsValue> {
    let passphrase = passphrase.map(Zeroizing::new);
    let passphrase_bytes = passphrase.as_ref().map(|value| value.as_bytes());

    let packets =
        safeparts_core::split_secret(secret, k, n, passphrase_bytes).map_err(js_core_error)?;

    let out = Array::new();
    for packet in packets {
        let encoded = Zeroizing::new(encode_packet(&packet, encoding).map_err(js_error)?);
        out.push(&JsValue::from_str(&encoded));
    }

    Ok(out)
}

#[wasm_bindgen]
pub fn combine_shares(
    shares: Array,
    encoding: &str,
    passphrase: Option<String>,
) -> Result<Uint8Array, JsValue> {
    let passphrase = passphrase.map(Zeroizing::new);
    let passphrase_bytes = passphrase.as_ref().map(|value| value.as_bytes());

    let mut packets = Vec::with_capacity(shares.length() as usize);
    for share in shares.iter() {
        let share_str = Zeroizing::new(
            share
                .as_string()
                .ok_or_else(|| recovery_error("invalid_share"))?,
        );
        let packet = decode_recovery_packet(&share_str, encoding).map_err(js_recovery_error)?;
        packets.push(packet);
    }

    combine_packets(&packets, passphrase_bytes)
}

#[wasm_bindgen]
pub fn combine_share_input(
    input: &str,
    encoding: &str,
    passphrase: Option<String>,
) -> Result<Uint8Array, JsValue> {
    let passphrase = passphrase.map(Zeroizing::new);
    let passphrase_bytes = passphrase.as_ref().map(|value| value.as_bytes());
    let encoding = Encoding::parse_name(encoding).map_err(js_recovery_error)?;
    let parsed = encoding::parse_share_packets_wrapped_mnemonics(input, encoding)
        .map_err(js_recovery_error)?;

    combine_packets(&parsed.packets, passphrase_bytes)
}

#[wasm_bindgen]
pub fn inspect_share(share: &str, encoding: &str) -> Result<JsValue, JsValue> {
    let (packet, detected) = decode_packet_with_encoding(share, encoding).map_err(js_error)?;
    packet_info(&packet, detected, 1)
}

#[wasm_bindgen]
pub fn inspect_share_input(input: &str, encoding: &str) -> Result<JsValue, JsValue> {
    let encoding = Encoding::parse_name(encoding).map_err(js_core_error)?;
    let parsed =
        encoding::parse_share_packets_wrapped_mnemonics(input, encoding).map_err(js_core_error)?;
    let first = parsed
        .packets
        .first()
        .ok_or_else(|| JsValue::from_str("no shares provided"))?;

    packet_info(first, parsed.encoding, parsed.packets.len())
}

fn combine_packets(
    packets: &[SharePacket],
    passphrase: Option<&[u8]>,
) -> Result<Uint8Array, JsValue> {
    let secret = Zeroizing::new(
        safeparts_core::combine_shares(packets, passphrase).map_err(js_recovery_error)?,
    );

    Ok(Uint8Array::from(secret.as_slice()))
}

fn packet_info(
    packet: &SharePacket,
    encoding: Encoding,
    share_count: usize,
) -> Result<JsValue, JsValue> {
    let obj = Object::new();
    Reflect::set(
        &obj,
        &JsValue::from_str("k"),
        &JsValue::from_f64(packet.k as f64),
    )?;
    Reflect::set(
        &obj,
        &JsValue::from_str("n"),
        &JsValue::from_f64(packet.n as f64),
    )?;
    Reflect::set(
        &obj,
        &JsValue::from_str("x"),
        &JsValue::from_f64(packet.x as f64),
    )?;
    Reflect::set(
        &obj,
        &JsValue::from_str("encrypted"),
        &JsValue::from_bool(packet.crypto_params.is_some()),
    )?;
    Reflect::set(
        &obj,
        &JsValue::from_str("encoding"),
        &JsValue::from_str(encoding.label()),
    )?;
    Reflect::set(
        &obj,
        &JsValue::from_str("shareCount"),
        &JsValue::from_f64(share_count as f64),
    )?;

    Ok(obj.into())
}

fn js_error(error: impl Display) -> JsValue {
    JsValue::from_str(&error.to_string())
}

fn recovery_error(code: &str) -> JsValue {
    let obj = Object::new();
    // Fresh plain objects have no setters; never include core Display text.
    let _ = Reflect::set(&obj, &JsValue::from_str("code"), &JsValue::from_str(code));
    obj.into()
}

fn js_recovery_error(error: CoreError) -> JsValue {
    recovery_error(match error {
        CoreError::InvalidPacket(_)
        | CoreError::Encoding(_)
        | CoreError::CouldNotDetectEncoding => "invalid_share",
        CoreError::UnknownEncoding(_) | CoreError::AutoEncodingForOutput => "unsupported_encoding",
        CoreError::NotEnoughShares { k, m } => {
            let obj = recovery_error("insufficient_shares");
            let _ = Reflect::set(
                &obj,
                &JsValue::from_str("required"),
                &JsValue::from_f64(k as f64),
            );
            let _ = Reflect::set(
                &obj,
                &JsValue::from_str("provided"),
                &JsValue::from_f64(m as f64),
            );
            return obj;
        }
        CoreError::DuplicateX { x } => {
            let obj = recovery_error("duplicate_share");
            let _ = Reflect::set(
                &obj,
                &JsValue::from_str("coordinate"),
                &JsValue::from_f64(x as f64),
            );
            return obj;
        }
        CoreError::InconsistentMetadata
        | CoreError::CryptoParamsMismatch
        | CoreError::TooManyShares { .. } => "inconsistent_shares",
        CoreError::PassphraseRequired => "passphrase_required",
        CoreError::DecryptFailed => "decryption_failed",
        CoreError::UnsupportedPacketFlags { .. } | CoreError::UnsupportedCryptoParams { .. } => {
            "unsupported_parameters"
        }
        CoreError::EmptyShareInput
        | CoreError::InvalidKAndN { .. }
        | CoreError::InvalidX
        | CoreError::InvalidShareIndex { .. }
        | CoreError::InvalidCombinedLength { .. }
        | CoreError::IntegrityCheckFailed => "invalid_share",
        CoreError::DivisionByZero | CoreError::Crypto(_) | CoreError::EncryptFailed => {
            "recovery_failed"
        }
    })
}

fn js_core_error(error: CoreError) -> JsValue {
    JsValue::from_str(&error.user_message())
}

fn encode_packet(packet: &SharePacket, encoding: &str) -> Result<String, String> {
    let encoding = Encoding::parse_name(encoding).map_err(|error| error.user_message())?;
    encoding::encode_packet(packet, encoding).map_err(|error| error.user_message())
}

fn decode_recovery_packet(input: &str, requested: &str) -> Result<SharePacket, CoreError> {
    let requested = Encoding::parse_name(requested)?;
    if !requested.is_auto() {
        return encoding::decode_packet(input, requested);
    }
    let mut parsed = encoding::parse_share_packets(input, requested)?;
    if parsed.packets.len() != 1 {
        return Err(CoreError::InvalidPacket(
            "expected one recovery share".to_string(),
        ));
    }
    Ok(parsed.packets.remove(0))
}

// Inspection retains its legacy string-error contract.
fn decode_packet(s: &str, encoding: &str) -> Result<SharePacket, String> {
    decode_packet_with_encoding(s, encoding).map(|(packet, _)| packet)
}

fn decode_packet_with_encoding(
    input: &str,
    requested: &str,
) -> Result<(SharePacket, Encoding), String> {
    let requested = Encoding::parse_name(requested).map_err(|error| error.user_message())?;
    if requested.is_auto() {
        let parsed = encoding::parse_share_packets(input, requested)
            .map_err(|error| error.user_message())?;
        if parsed.packets.len() != 1 {
            return Err("expected one recovery share".to_string());
        }
        let mut packets = parsed.packets;
        return Ok((packets.remove(0), parsed.encoding));
    }

    let packet = encoding::decode_packet(input, requested).map_err(|error| error.user_message())?;
    Ok((packet, requested))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn unknown_encoding_is_error() {
        let pkt = safeparts_core::packet::SharePacket {
            set_id: safeparts_core::sss::SetId([0u8; 16]),
            k: 2,
            n: 3,
            x: 1,
            payload: vec![1, 2, 3],
            crypto_params: None,
        };

        let err = encode_packet(&pkt, "nope").unwrap_err();
        assert!(err.contains("unknown encoding"));
    }

    #[test]
    fn malformed_share_errors_do_not_echo_input() {
        let sensitive = "SECRET-SHARE-TEXT";
        let error = decode_packet(sensitive, "mnemo-words").unwrap_err();

        assert!(!error.contains(sensitive));
        assert!(error.contains("could not be decoded"));
    }
}
