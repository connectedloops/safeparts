use base64::Engine;

use crate::base58check;
use crate::error::{CoreError, CoreResult};
use crate::packet::{DecodedSharePacket, SharePacket};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Encoding {
    Base58check,
    Base64url,
}

pub fn encode_packet(packet: &SharePacket, encoding: Encoding) -> CoreResult<String> {
    let bytes = packet.encode_binary()?;
    match encoding {
        Encoding::Base58check => {
            base58check::encode(&bytes).map_err(|error| CoreError::Encoding(error.to_string()))
        }
        Encoding::Base64url => Ok(base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(bytes)),
    }
}

pub fn decode_packet(s: &str, encoding: Encoding) -> CoreResult<SharePacket> {
    Ok(decode_packet_with_version(s, encoding)?.packet)
}

pub fn decode_packet_with_version(s: &str, encoding: Encoding) -> CoreResult<DecodedSharePacket> {
    let bytes = match encoding {
        Encoding::Base58check => {
            base58check::decode(s).map_err(|error| CoreError::Encoding(error.to_string()))?
        }
        Encoding::Base64url => base64::engine::general_purpose::URL_SAFE_NO_PAD
            .decode(s)
            .map_err(|e| CoreError::Encoding(e.to_string()))?,
    };

    SharePacket::decode_binary_with_version(&bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::sss::SetId;

    #[test]
    fn base64url_round_trip() {
        let pkt = SharePacket {
            set_id: SetId([7u8; 16]),
            k: 3,
            n: 5,
            x: 1,
            payload: vec![1, 2, 3, 4],
            crypto_params: None,
        };

        let enc = encode_packet(&pkt, Encoding::Base64url).unwrap();
        let dec = decode_packet(&enc, Encoding::Base64url).unwrap();
        assert_eq!(dec, pkt);
    }

    #[test]
    fn base58check_round_trip() {
        let pkt = SharePacket {
            set_id: SetId([9u8; 16]),
            k: 2,
            n: 3,
            x: 3,
            payload: vec![0, 255, 4, 9],
            crypto_params: None,
        };

        let enc = encode_packet(&pkt, Encoding::Base58check).unwrap();
        let dec = decode_packet(&enc, Encoding::Base58check).unwrap();
        assert_eq!(dec, pkt);
    }
}
