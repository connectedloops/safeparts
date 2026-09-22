//! Reversible text encodings for share packets.
//!
//! Use this module when an application needs to store, display, paste, or parse
//! [`SharePacket`] values as text. `base64url` is compact and machine-friendly,
//! while the mnemonic formats are better for paper and manual transcription.
//! `Encoding::Auto` is accepted only by parsing functions.

use crate::error::{CoreError, CoreResult};
use crate::packet::{DecodedSharePacket, SharePacket};
use crate::{ascii, mnemo_bip39, mnemo_words};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
#[non_exhaustive]
pub enum Encoding {
    Auto,
    Base64url,
    Base58check,
    MnemoWords,
    MnemoBip39,
}

impl Encoding {
    pub const CONCRETE: &'static [Encoding] = &[
        Encoding::Base64url,
        Encoding::Base58check,
        Encoding::MnemoWords,
        Encoding::MnemoBip39,
    ];

    pub const WITH_AUTO: &'static [Encoding] = &[
        Encoding::Auto,
        Encoding::Base64url,
        Encoding::Base58check,
        Encoding::MnemoWords,
        Encoding::MnemoBip39,
    ];

    pub fn label(self) -> &'static str {
        match self {
            Encoding::Auto => "auto",
            Encoding::Base64url => "base64url",
            Encoding::Base58check => "base58check",
            Encoding::MnemoWords => "mnemo-words",
            Encoding::MnemoBip39 => "mnemo-bip39",
        }
    }

    pub fn parse_name(name: &str) -> CoreResult<Self> {
        match name {
            "auto" => Ok(Encoding::Auto),
            "base64url" | "base64" => Ok(Encoding::Base64url),
            "base58check" | "base58" => Ok(Encoding::Base58check),
            "mnemo-words" => Ok(Encoding::MnemoWords),
            "mnemo-bip39" => Ok(Encoding::MnemoBip39),
            _ => Err(CoreError::UnknownEncoding(name.to_string())),
        }
    }

    pub fn is_auto(self) -> bool {
        self == Encoding::Auto
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParsedSharePackets {
    pub packets: Vec<SharePacket>,
    pub encoding: Encoding,
}

/// Strictly parsed share packets with their released wire versions retained.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct ParsedDecodedSharePackets {
    pub packets: Vec<DecodedSharePacket>,
    pub encoding: Encoding,
}

/// Encode one share packet as text.
///
/// `Encoding::Auto` is not valid for output because callers must choose a
/// concrete representation.
///
/// # Example
///
/// ```
/// use safeparts_core::encoding::{self, Encoding};
/// use safeparts_core::{split_secret, CoreResult};
///
/// fn main() -> CoreResult<()> {
///     let shares = split_secret(b"example", 2, 3, None)?;
///     let encoded = encoding::encode_packet(&shares[0], Encoding::Base64url)?;
///     assert!(!encoded.is_empty());
///     Ok(())
/// }
/// ```
pub fn encode_packet(packet: &SharePacket, encoding: Encoding) -> CoreResult<String> {
    match encoding {
        Encoding::Auto => Err(CoreError::AutoEncodingForOutput),
        Encoding::Base64url => ascii::encode_packet(packet, ascii::Encoding::Base64url),
        Encoding::Base58check => ascii::encode_packet(packet, ascii::Encoding::Base58check),
        Encoding::MnemoWords => mnemo_words::encode_packet(packet),
        Encoding::MnemoBip39 => mnemo_bip39::encode_packet(packet),
    }
}

/// Decode one share packet from text.
///
/// Pass a concrete encoding when the UI or storage layer already knows the
/// format. Use `Encoding::Auto` for pasted input where the format is unknown.
pub fn decode_packet(s: &str, encoding: Encoding) -> CoreResult<SharePacket> {
    match encoding {
        Encoding::Auto => parse_share_packets(s, Encoding::Auto).and_then(|parsed| {
            let mut packets = parsed.packets;
            if packets.len() == 1 {
                Ok(packets.remove(0))
            } else {
                Err(CoreError::Encoding(format!(
                    "expected one share packet, got {}",
                    packets.len()
                )))
            }
        }),
        Encoding::Base64url => ascii::decode_packet(s, ascii::Encoding::Base64url),
        Encoding::Base58check => ascii::decode_packet(s, ascii::Encoding::Base58check),
        Encoding::MnemoWords => mnemo_words::decode_packet(s),
        Encoding::MnemoBip39 => mnemo_bip39::decode_packet(s),
    }
}

/// Parse one or more share packets from pasted text.
///
/// Compact encodings may be separated by any whitespace. Mnemonic shares are
/// normally separated by lines or blank lines. The returned value includes the
/// packets and the concrete encoding that was used.
pub fn parse_share_packets(input: &str, encoding: Encoding) -> CoreResult<ParsedSharePackets> {
    parse_share_packets_with_mnemonic_lines(input, encoding, MnemonicLineMode::Shares)
}

/// Parse share text that may contain wrapped mnemonic Recovery shares.
///
/// For mnemonic encodings, first accept one complete, strictly decoded packet
/// per nonempty line (including CLI output). If that fails, decode each
/// blank-line-separated paragraph as one wrapped packet. Both attempts consume
/// all nonempty input; invalid lines, words, and frames are never skipped.
/// LF and CRLF line endings are accepted. Compact encodings use whitespace
/// separators, as in [`parse_share_packets`].
pub fn parse_share_packets_wrapped_mnemonics(
    input: &str,
    encoding: Encoding,
) -> CoreResult<ParsedSharePackets> {
    let parsed = parse_share_packets_wrapped_mnemonics_with_versions(input, encoding)?;
    Ok(ParsedSharePackets {
        packets: parsed.packets.into_iter().map(|item| item.packet).collect(),
        encoding: parsed.encoding,
    })
}

/// Parse complete share input while retaining each packet's released wire version.
///
/// Detection, strict all-content consumption, and mnemonic framing stay core-owned.
/// The returned encoding is always concrete, including when `Encoding::Auto` is
/// requested.
pub fn parse_share_packets_wrapped_mnemonics_with_versions(
    input: &str,
    encoding: Encoding,
) -> CoreResult<ParsedDecodedSharePackets> {
    parse_decoded_share_packets_with_mnemonic_lines(input, encoding, MnemonicLineMode::WrappedShare)
}

/// Parse one or more wrapped Words shares while retaining each packet version.
///
/// This is intended for front ends whose supported-input policy is narrower
/// than the core compatibility policy. Framing remains core-owned and consumes
/// every nonempty line or paragraph.
pub fn parse_mnemo_words_packets_with_versions(input: &str) -> CoreResult<Vec<DecodedSharePacket>> {
    let lines = nonempty_lines(input);
    if lines.is_empty() {
        return Err(CoreError::EmptyShareInput);
    }

    let line_packets = lines
        .iter()
        .map(|line| mnemo_words::decode_packet_with_version(line))
        .collect::<CoreResult<Vec<_>>>();
    if let Ok(packets) = line_packets {
        return Ok(packets);
    }

    split_mnemonic_input(input, MnemonicLineMode::WrappedShare)
        .iter()
        .map(|block| mnemo_words::decode_packet_with_version(block))
        .collect()
}

fn parse_share_packets_with_mnemonic_lines(
    input: &str,
    encoding: Encoding,
    mnemonic_line_mode: MnemonicLineMode,
) -> CoreResult<ParsedSharePackets> {
    let nonempty_lines = nonempty_lines(input);

    if nonempty_lines.is_empty() {
        return Err(CoreError::EmptyShareInput);
    }

    let automatic = encoding.is_auto();
    let encoding = if automatic {
        detect_encoding_from_lines(&nonempty_lines, input)?
            .ok_or(CoreError::CouldNotDetectEncoding)?
    } else {
        encoding
    };

    let packets = match decode_share_packets_known(input, encoding, mnemonic_line_mode) {
        Ok(packets) => packets,
        Err(_) if automatic && has_mixed_share_encodings(input) => {
            return Err(CoreError::MixedEncoding);
        }
        Err(error) => return Err(error),
    };
    Ok(ParsedSharePackets { packets, encoding })
}

/// Try to detect the share encoding without decoding the caller's intent.
///
/// Returns `Ok(None)` when the input is non-empty but does not clearly match a
/// supported encoding.
pub fn detect_encoding(input: &str) -> CoreResult<Option<Encoding>> {
    let nonempty_lines = nonempty_lines(input);

    if nonempty_lines.is_empty() {
        return Err(CoreError::EmptyShareInput);
    }

    detect_encoding_from_lines(&nonempty_lines, input)
}

fn nonempty_lines(input: &str) -> Vec<&str> {
    input
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .collect()
}

fn detect_encoding_from_lines(
    nonempty_lines: &[&str],
    full_input: &str,
) -> CoreResult<Option<Encoding>> {
    match decode_share_packets_known(full_input, Encoding::Base64url, MnemonicLineMode::Shares) {
        Ok(_) => return Ok(Some(Encoding::Base64url)),
        Err(error) if is_known_unsupported_packet(&error) => return Err(error),
        Err(_) => {}
    }

    match decode_share_packets_known(full_input, Encoding::Base58check, MnemonicLineMode::Shares) {
        Ok(_) => return Ok(Some(Encoding::Base58check)),
        Err(error) if is_known_unsupported_packet(&error) => return Err(error),
        Err(_) => {}
    }

    let looks_mnemonic = nonempty_lines
        .iter()
        .any(|line| line.contains('/') || line.split_whitespace().count() > 1);

    if looks_mnemonic {
        let looks_bip39 = nonempty_lines.iter().any(|line| line.contains('/'));
        return Ok(Some(if looks_bip39 {
            Encoding::MnemoBip39
        } else {
            Encoding::MnemoWords
        }));
    }

    Ok(None)
}

fn is_known_unsupported_packet(error: &CoreError) -> bool {
    matches!(
        error,
        CoreError::UnsupportedPacketVersion { .. }
            | CoreError::UnsupportedPacketFlags { .. }
            | CoreError::UnsupportedCryptoParams { .. }
    )
}

fn decode_share_packets_known(
    input: &str,
    encoding: Encoding,
    mnemonic_line_mode: MnemonicLineMode,
) -> CoreResult<Vec<SharePacket>> {
    decode_decoded_share_packets_known(input, encoding, mnemonic_line_mode)
        .map(|packets| packets.into_iter().map(|item| item.packet).collect())
}

fn parse_decoded_share_packets_with_mnemonic_lines(
    input: &str,
    encoding: Encoding,
    mnemonic_line_mode: MnemonicLineMode,
) -> CoreResult<ParsedDecodedSharePackets> {
    let lines = nonempty_lines(input);
    if lines.is_empty() {
        return Err(CoreError::EmptyShareInput);
    }
    let automatic = encoding.is_auto();
    let encoding = if automatic {
        detect_encoding_from_lines(&lines, input)?.ok_or(CoreError::CouldNotDetectEncoding)?
    } else {
        encoding
    };
    let packets = match decode_decoded_share_packets_known(input, encoding, mnemonic_line_mode) {
        Ok(packets) => packets,
        Err(_) if automatic && has_mixed_share_encodings(input) => {
            return Err(CoreError::MixedEncoding);
        }
        Err(error) => return Err(error),
    };
    Ok(ParsedDecodedSharePackets { packets, encoding })
}

fn decode_decoded_share_packets_known(
    input: &str,
    encoding: Encoding,
    mnemonic_line_mode: MnemonicLineMode,
) -> CoreResult<Vec<DecodedSharePacket>> {
    if matches!(mnemonic_line_mode, MnemonicLineMode::WrappedShare)
        && matches!(encoding, Encoding::MnemoWords | Encoding::MnemoBip39)
    {
        let line_packets = nonempty_lines(input)
            .iter()
            .map(|line| decode_packet_with_version(line, encoding))
            .collect::<CoreResult<Vec<_>>>();
        if let Ok(packets) = line_packets {
            return Ok(packets);
        }
    }

    match encoding {
        Encoding::Auto => Err(CoreError::CouldNotDetectEncoding),
        Encoding::MnemoWords => split_mnemonic_input(input, mnemonic_line_mode)
            .iter()
            .map(|block| mnemo_words::decode_packet_with_version(block))
            .collect(),
        Encoding::MnemoBip39 => split_mnemonic_input(input, mnemonic_line_mode)
            .iter()
            .map(|block| mnemo_bip39::decode_packet_with_version(block))
            .collect(),
        Encoding::Base64url => input
            .split_whitespace()
            .map(|token| ascii::decode_packet_with_version(token, ascii::Encoding::Base64url))
            .collect(),
        Encoding::Base58check => input
            .split_whitespace()
            .map(|token| ascii::decode_packet_with_version(token, ascii::Encoding::Base58check))
            .collect(),
    }
}

fn decode_packet_with_version(input: &str, encoding: Encoding) -> CoreResult<DecodedSharePacket> {
    match encoding {
        Encoding::Auto => Err(CoreError::CouldNotDetectEncoding),
        Encoding::Base64url => ascii::decode_packet_with_version(input, ascii::Encoding::Base64url),
        Encoding::Base58check => {
            ascii::decode_packet_with_version(input, ascii::Encoding::Base58check)
        }
        Encoding::MnemoWords => mnemo_words::decode_packet_with_version(input),
        Encoding::MnemoBip39 => mnemo_bip39::decode_packet_with_version(input),
    }
}

fn has_mixed_share_encodings(input: &str) -> bool {
    let lines = nonempty_lines(input);
    if units_have_mixed_encodings(&lines) {
        return true;
    }

    let normalized = input.replace("\r\n", "\n");
    let blocks = split_mnemonic_blocks(&normalized);
    let block_refs = blocks.iter().map(String::as_str).collect::<Vec<_>>();
    if units_have_mixed_encodings(&block_refs) {
        return true;
    }

    let compact_tokens = input.split_whitespace().collect::<Vec<_>>();
    units_have_mixed_compact_encodings(&compact_tokens)
}

fn units_have_mixed_encodings(units: &[&str]) -> bool {
    mixed_concrete_encodings(
        units,
        &[
            Encoding::Base64url,
            Encoding::Base58check,
            Encoding::MnemoWords,
            Encoding::MnemoBip39,
        ],
    )
}

fn units_have_mixed_compact_encodings(units: &[&str]) -> bool {
    mixed_concrete_encodings(units, &[Encoding::Base64url, Encoding::Base58check])
}

fn mixed_concrete_encodings(units: &[&str], encodings: &[Encoding]) -> bool {
    if units.len() < 2 {
        return false;
    }

    let mut detected = None;
    let mut mixed = false;
    for unit in units {
        let Some(encoding) = encodings.iter().copied().find(|encoding| {
            decode_decoded_share_packets_known(unit, *encoding, MnemonicLineMode::WrappedShare)
                .is_ok_and(|packets| packets.len() == 1)
        }) else {
            return false;
        };
        mixed |= detected.is_some_and(|existing| existing != encoding);
        detected = Some(encoding);
    }
    mixed
}

#[derive(Clone, Copy)]
enum MnemonicLineMode {
    Shares,
    WrappedShare,
}

fn split_mnemonic_input(input: &str, line_mode: MnemonicLineMode) -> Vec<String> {
    let normalized = input.replace("\r\n", "\n");

    if normalized.contains("\n\n") {
        return split_mnemonic_blocks(&normalized);
    }

    let lines: Vec<String> = normalized
        .lines()
        .map(str::trim)
        .filter(|line| !line.is_empty())
        .map(ToOwned::to_owned)
        .collect();

    if lines.len() > 1 && matches!(line_mode, MnemonicLineMode::Shares) {
        return lines;
    }

    split_mnemonic_blocks(&normalized)
}

fn split_mnemonic_blocks(input: &str) -> Vec<String> {
    input
        .split("\n\n")
        .map(|block| {
            block
                .lines()
                .map(str::trim)
                .filter(|line| !line.is_empty())
                .collect::<Vec<_>>()
                .join(" ")
        })
        .map(|block| block.trim().to_string())
        .filter(|block| !block.is_empty())
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sss::SetId;
    use base64::Engine;

    fn packet() -> SharePacket {
        SharePacket {
            set_id: SetId([7u8; 16]),
            k: 2,
            n: 3,
            x: 1,
            payload: vec![1, 2, 3, 4],
            crypto_params: None,
        }
    }

    #[test]
    fn parse_names_accepts_canonical_names_and_cli_aliases() {
        assert_eq!(
            Encoding::parse_name("base64url").unwrap(),
            Encoding::Base64url
        );
        assert_eq!(Encoding::parse_name("base64").unwrap(), Encoding::Base64url);
        assert_eq!(
            Encoding::parse_name("base58check").unwrap(),
            Encoding::Base58check
        );
        assert_eq!(
            Encoding::parse_name("base58").unwrap(),
            Encoding::Base58check
        );
    }

    #[test]
    fn base64url_round_trip_reports_detected_encoding() {
        let encoded = encode_packet(&packet(), Encoding::Base64url).unwrap();
        let parsed = parse_share_packets(&encoded, Encoding::Auto).unwrap();
        assert_eq!(parsed.encoding, Encoding::Base64url);
        assert_eq!(parsed.packets, vec![packet()]);
    }

    #[test]
    fn auto_preserves_known_unsupported_packet_errors() {
        let mut bytes = packet().encode_binary().unwrap();
        bytes[4] = 99;
        let encoded = base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(bytes);

        let error = parse_share_packets(&encoded, Encoding::Auto).unwrap_err();
        assert!(matches!(
            error,
            CoreError::UnsupportedPacketVersion { version: 99 }
        ));
    }

    #[test]
    fn auto_reports_mixed_encodings_within_one_input_batch() {
        let words = encode_packet(&packet(), Encoding::MnemoWords).unwrap();
        let base64 = encode_packet(&packet(), Encoding::Base64url).unwrap();
        let base58 = encode_packet(&packet(), Encoding::Base58check).unwrap();

        for mixed in [format!("{words}\n\n{base64}"), format!("{base64} {base58}")] {
            assert!(matches!(
                parse_share_packets_wrapped_mnemonics(&mixed, Encoding::Auto),
                Err(CoreError::MixedEncoding)
            ));
        }
    }

    #[test]
    fn empty_input_is_typed_error() {
        let err = parse_share_packets("  \n\t", Encoding::Auto).unwrap_err();
        assert!(matches!(err, CoreError::EmptyShareInput));
    }

    #[test]
    fn split_mnemonic_input_treats_multiple_lines_as_multiple_shares() {
        let blocks = split_mnemonic_input("alpha beta\ngamma delta\n", MnemonicLineMode::Shares);
        assert_eq!(
            blocks,
            vec!["alpha beta".to_string(), "gamma delta".to_string()]
        );
    }

    #[test]
    fn split_mnemonic_input_can_treat_multiple_lines_as_wrapped_share() {
        let blocks =
            split_mnemonic_input("alpha beta\ngamma delta\n", MnemonicLineMode::WrappedShare);
        assert_eq!(blocks, vec!["alpha beta gamma delta".to_string()]);
    }

    #[test]
    fn wrapped_mnemonic_parser_decodes_single_wrapped_share() {
        let encoded = encode_packet(&packet(), Encoding::MnemoWords).unwrap();
        let mut words = encoded.split_whitespace();
        let first_line = words.by_ref().take(8).collect::<Vec<_>>().join(" ");
        let second_line = words.collect::<Vec<_>>().join(" ");
        let wrapped = format!("{first_line}\n{second_line}");

        let parsed = parse_share_packets_wrapped_mnemonics(&wrapped, Encoding::MnemoWords).unwrap();
        assert_eq!(parsed.packets, vec![packet()]);
    }

    #[test]
    fn auto_detects_same_line_compact_shares_with_mixed_whitespace() {
        let packets = crate::split_secret(b"synthetic compact whitespace", 2, 3, None).unwrap();

        for encoding in [Encoding::Base64url, Encoding::Base58check] {
            let first = encode_packet(&packets[0], encoding).unwrap();
            let second = encode_packet(&packets[1], encoding).unwrap();
            let parsed =
                parse_share_packets(&format!("{first} \t {second}"), Encoding::Auto).unwrap();

            assert_eq!(parsed.encoding, encoding);
            assert_eq!(parsed.packets, packets[..2]);
        }
    }
}
