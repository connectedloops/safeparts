use core::convert::TryInto;

use bip39::{Language, Mnemonic};

use crate::error::{CoreError, CoreResult};
use crate::packet::{self, SharePacket};

const ENTROPY_LEN: usize = 32;
const CHUNK_LEN: usize = 28;
const FRAME_HEADER_LEN: usize = 4;
const FRAME_SEPARATOR: &str = " / ";

pub fn encode_packet(packet: &SharePacket) -> CoreResult<String> {
    let bytes = packet.encode_binary()?;
    let frames: Vec<&[u8]> = bytes.chunks(CHUNK_LEN).collect();

    let frame_count = u16::try_from(frames.len())
        .map_err(|_| CoreError::Encoding("too many bip39 frames".to_string()))?;

    let mut out = Vec::with_capacity(frames.len());
    for (idx, chunk) in frames.into_iter().enumerate() {
        let frame_idx = u16::try_from(idx)
            .map_err(|_| CoreError::Encoding("too many bip39 frames".to_string()))?;

        let mut entropy = [0u8; ENTROPY_LEN];
        entropy[0..2].copy_from_slice(&frame_idx.to_be_bytes());
        entropy[2..4].copy_from_slice(&frame_count.to_be_bytes());
        entropy[FRAME_HEADER_LEN..FRAME_HEADER_LEN + chunk.len()].copy_from_slice(chunk);

        let mnemonic = Mnemonic::from_entropy_in(Language::English, &entropy)
            .map_err(|e| CoreError::Encoding(e.to_string()))?;
        out.push(mnemonic.to_string());
    }

    Ok(out.join(FRAME_SEPARATOR))
}

pub fn decode_packet(s: &str) -> CoreResult<SharePacket> {
    let phrases: Vec<&str> = s.split('/').map(str::trim).collect();

    if phrases.iter().any(|phrase| phrase.is_empty()) {
        return Err(CoreError::Encoding("empty bip39 frame".to_string()));
    }

    let mut expected_count: Option<u16> = None;
    let mut chunks: Vec<Option<[u8; CHUNK_LEN]>> = Vec::new();

    for phrase in phrases {
        let lowered;
        let phrase = if phrase.bytes().any(|b| b.is_ascii_uppercase()) {
            lowered = phrase.to_ascii_lowercase();
            lowered.as_str()
        } else {
            phrase
        };

        let mnemonic = Mnemonic::parse_in(Language::English, phrase)
            .map_err(|e| CoreError::Encoding(e.to_string()))?;

        let entropy_vec = mnemonic.to_entropy();

        if entropy_vec.len() != ENTROPY_LEN {
            return Err(CoreError::Encoding(
                "unexpected bip39 entropy length".to_string(),
            ));
        }

        let entropy: [u8; ENTROPY_LEN] = entropy_vec
            .as_slice()
            .try_into()
            .map_err(|_| CoreError::Encoding("unexpected bip39 entropy length".to_string()))?;

        let frame_idx_bytes: [u8; 2] = entropy[0..2]
            .try_into()
            .map_err(|_| CoreError::Encoding("frame index missing".to_string()))?;
        let frame_count_bytes: [u8; 2] = entropy[2..4]
            .try_into()
            .map_err(|_| CoreError::Encoding("frame count missing".to_string()))?;

        let frame_idx = u16::from_be_bytes(frame_idx_bytes);
        let frame_count = u16::from_be_bytes(frame_count_bytes);

        if frame_count == 0 {
            return Err(CoreError::Encoding("invalid bip39 frame count".to_string()));
        }

        if let Some(expected) = expected_count {
            if expected != frame_count {
                return Err(CoreError::Encoding(
                    "bip39 frame count mismatch".to_string(),
                ));
            }
        } else {
            expected_count = Some(frame_count);
            chunks.resize(frame_count as usize, None);
        }

        if frame_idx >= frame_count {
            return Err(CoreError::Encoding(
                "bip39 frame index out of range".to_string(),
            ));
        }

        let slot = &mut chunks[frame_idx as usize];
        if slot.is_some() {
            return Err(CoreError::Encoding(
                "duplicate bip39 frame index".to_string(),
            ));
        }

        let mut chunk = [0u8; CHUNK_LEN];
        chunk.copy_from_slice(&entropy[FRAME_HEADER_LEN..]);
        *slot = Some(chunk);
    }

    let count = expected_count.ok_or_else(|| CoreError::Encoding("no bip39 frames".to_string()))?;

    if chunks.len() != count as usize || chunks.iter().any(Option::is_none) {
        return Err(CoreError::Encoding("missing bip39 frames".to_string()));
    }

    let mut combined = Vec::with_capacity(CHUNK_LEN * chunks.len());
    for chunk in chunks.into_iter().flatten() {
        combined.extend_from_slice(&chunk);
    }

    let total_len = packet::binary_total_len(&combined)
        .map_err(|e| CoreError::Encoding(format!("invalid packet header: {e}")))?;

    if combined.len() < total_len {
        return Err(CoreError::Encoding("packet bytes truncated".to_string()));
    }

    if combined[total_len..].iter().any(|&b| b != 0) {
        return Err(CoreError::Encoding(
            "nonzero padding in bip39 frames".to_string(),
        ));
    }

    SharePacket::decode_binary(&combined[..total_len])
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::packet::SharePacket;
    use crate::sss::SetId;

    #[test]
    fn round_trip_multi_frame_packet() {
        let pkt = SharePacket {
            set_id: SetId([7u8; 16]),
            k: 3,
            n: 5,
            x: 4,
            payload: (0u8..200).collect(),
            crypto_params: None,
        };

        let s = encode_packet(&pkt).unwrap();
        let decoded = decode_packet(&s).unwrap();
        assert_eq!(decoded, pkt);
    }

    #[test]
    fn decode_accepts_mixed_case_words() {
        let pkt = SharePacket {
            set_id: SetId([7u8; 16]),
            k: 3,
            n: 5,
            x: 4,
            payload: (0u8..200).collect(),
            crypto_params: None,
        };

        let s = encode_packet(&pkt).unwrap();
        let phrases: Vec<String> = s
            .split('/')
            .map(str::trim)
            .filter(|p| !p.is_empty())
            .map(|phrase| {
                let words: Vec<String> = phrase
                    .split_whitespace()
                    .enumerate()
                    .map(|(i, w)| {
                        if i % 3 == 0 {
                            w.to_ascii_uppercase()
                        } else {
                            w.to_string()
                        }
                    })
                    .collect();

                words.join(" ")
            })
            .collect();

        let mixed = phrases.join(FRAME_SEPARATOR);
        let decoded = decode_packet(&mixed).unwrap();
        assert_eq!(decoded, pkt);
    }

    #[test]
    fn missing_frame_is_rejected() {
        let pkt = SharePacket {
            set_id: SetId([1u8; 16]),
            k: 2,
            n: 3,
            x: 1,
            payload: (0u8..120).collect(),
            crypto_params: None,
        };

        let s = encode_packet(&pkt).unwrap();
        let mut parts: Vec<&str> = s
            .split('/')
            .map(str::trim)
            .filter(|p| !p.is_empty())
            .collect();
        parts.pop();
        let truncated = parts.join(FRAME_SEPARATOR);

        let err = decode_packet(&truncated).unwrap_err();
        assert!(matches!(err, CoreError::Encoding(_)));
    }

    #[test]
    fn invalid_phrase_is_rejected() {
        let pkt = SharePacket {
            set_id: SetId([2u8; 16]),
            k: 2,
            n: 3,
            x: 2,
            payload: vec![1, 2, 3, 4, 5, 6],
            crypto_params: None,
        };

        let s = encode_packet(&pkt).unwrap();
        let mut phrases: Vec<String> = s
            .split('/')
            .map(str::trim)
            .filter(|p| !p.is_empty())
            .map(str::to_string)
            .collect();

        let mut words: Vec<String> = phrases[0].split_whitespace().map(str::to_string).collect();
        if let Some(last) = words.last_mut() {
            *last = "ability".to_string();
        }
        phrases[0] = words.join(" ");

        let corrupted = phrases.join(FRAME_SEPARATOR);

        let err = decode_packet(&corrupted).unwrap_err();
        assert!(matches!(err, CoreError::Encoding(_)));
    }
}
