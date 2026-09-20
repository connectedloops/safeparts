//! Core split, combine, packet, and encoding APIs for Safeparts.
//!
//! Most callers should use [`split_secret`] and [`combine_shares`], then encode
//! the returned [`packet::SharePacket`] values with [`encoding::encode_packet`].
//! The crate works on bytes, so callers can split UTF-8 text, binary keys, or
//! exported credential files without a separate conversion layer.
//!
//! # Example
//!
//! ```
//! use safeparts_core::{combine_shares, split_secret, CoreResult};
//!
//! fn main() -> CoreResult<()> {
//!     let shares = split_secret(b"example secret", 2, 3, None)?;
//!     let recovered = combine_shares(&shares[..2], None)?;
//!
//!     assert_eq!(recovered, b"example secret");
//!     Ok(())
//! }
//! ```

pub mod ascii;
pub mod crypto;
pub mod encoding;
pub mod error;
pub mod gf256;
pub mod mnemo_bip39;
pub mod mnemo_words;
pub mod packet;
pub mod sss;

pub use crate::error::{CoreError, CoreResult};
use zeroize::Zeroizing;

pub const INTEGRITY_TAG_LEN: usize = 32;
const AEAD_TAG_LEN: usize = 16;

/// Validated metadata needed to admit a recovery operation before interpolation.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ShareSetInspection {
    pub threshold: u8,
    pub share_count: u8,
    pub supplied_count: usize,
    pub expected_secret_len: usize,
    pub passphrase_protected: bool,
}

/// Validate a complete supplied share collection without reconstructing it.
///
/// This checks packet metadata, duplicate coordinates, declared set size,
/// payload lengths, and supported crypto parameters. It intentionally permits
/// fewer than the threshold number of shares so front ends can report
/// readiness before an explicit recovery action.
pub fn inspect_share_set(packets: &[packet::SharePacket]) -> CoreResult<ShareSetInspection> {
    let first = packets
        .first()
        .ok_or(CoreError::NotEnoughShares { k: 1, m: 0 })?;
    sss::validate_share_metadata(first.k, first.n, first.x)?;
    if packets.len() > usize::from(first.n) {
        return Err(CoreError::TooManyShares {
            n: first.n,
            m: packets.len(),
        });
    }
    if let Some(params) = first.crypto_params {
        params.validate_policy()?;
    }

    let overhead = INTEGRITY_TAG_LEN
        + if first.crypto_params.is_some() {
            AEAD_TAG_LEN
        } else {
            0
        };
    let expected_secret_len =
        first
            .payload
            .len()
            .checked_sub(overhead)
            .ok_or(CoreError::InvalidCombinedLength {
                len: first.payload.len(),
            })?;

    let mut seen = [false; 256];
    for packet in packets {
        sss::validate_share_metadata(packet.k, packet.n, packet.x)?;
        if packet.set_id != first.set_id
            || packet.k != first.k
            || packet.n != first.n
            || packet.payload.len() != first.payload.len()
            || packet.crypto_params != first.crypto_params
        {
            return Err(CoreError::InconsistentMetadata);
        }
        if seen[usize::from(packet.x)] {
            return Err(CoreError::DuplicateX { x: packet.x });
        }
        seen[usize::from(packet.x)] = true;
    }

    Ok(ShareSetInspection {
        threshold: first.k,
        share_count: first.n,
        supplied_count: packets.len(),
        expected_secret_len,
        passphrase_protected: first.crypto_params.is_some(),
    })
}

/// Split plaintext bytes into `n` share packets with a threshold of `k`.
///
/// This is a compatibility wrapper around [`split_secret`] without passphrase
/// protection. New code should usually call [`split_secret`] directly so the
/// passphrase policy is visible at the call site.
pub fn tag_and_split(secret: &[u8], k: u8, n: u8) -> CoreResult<Vec<packet::SharePacket>> {
    split_secret(secret, k, n, None)
}

/// Combine share packets created without passphrase protection.
///
/// This is a compatibility wrapper around [`combine_shares`]. New code should
/// usually call [`combine_shares`] directly so passphrase handling is explicit.
pub fn combine_and_verify(packets: &[packet::SharePacket]) -> CoreResult<Vec<u8>> {
    combine_shares(packets, None)
}

/// Split secret bytes into self-describing share packets.
///
/// `k` is the number of shares required for recovery. `n` is the total number
/// of shares to create. The valid range is `1 <= k <= n <= 255`.
///
/// When `passphrase` is `Some`, Safeparts encrypts the secret before splitting
/// it. Recovery then requires both at least `k` shares and the same passphrase.
/// The returned packets carry the non-secret crypto parameters needed for
/// decryption, but never the passphrase.
///
/// # Example
///
/// ```
/// use safeparts_core::{combine_shares, split_secret, CoreResult};
///
/// fn main() -> CoreResult<()> {
///     let shares = split_secret(b"example secret", 2, 3, None)?;
///     let recovered = combine_shares(&shares[..2], None)?;
///
///     assert_eq!(recovered, b"example secret");
///     Ok(())
/// }
/// ```
pub fn split_secret(
    secret: &[u8],
    k: u8,
    n: u8,
    passphrase: Option<&[u8]>,
) -> CoreResult<Vec<packet::SharePacket>> {
    let (mut data_to_split, crypto_params) = if let Some(passphrase) = passphrase {
        let (ciphertext, params) = crypto::encrypt(secret, passphrase)?;
        (Zeroizing::new(ciphertext), Some(params))
    } else {
        (Zeroizing::new(secret.to_vec()), None)
    };

    let tag = blake3::hash(&data_to_split);
    data_to_split.extend_from_slice(tag.as_bytes());

    let set_id = sss::SetId::random();
    let shares = sss::split(&data_to_split, k, n, set_id)?;

    Ok(shares
        .into_iter()
        .map(|share| packet::SharePacket::from_raw_share(share).with_crypto_params(crypto_params))
        .collect())
}

/// Recover the original secret bytes from share packets.
///
/// The input must contain at least `k` packets from the same split set. Extra
/// packets are allowed, but duplicate share coordinates or mixed metadata cause
/// typed errors.
///
/// Pass `Some(passphrase)` when the shares were created with passphrase
/// protection. Passing `None` for encrypted shares returns
/// [`CoreError::PassphraseRequired`].
///
/// # Example
///
/// ```
/// use safeparts_core::{combine_shares, split_secret, CoreResult};
///
/// fn main() -> CoreResult<()> {
///     let passphrase = b"example passphrase";
///     let shares = split_secret(b"example secret", 2, 3, Some(passphrase))?;
///     let recovered = combine_shares(&shares[..2], Some(passphrase))?;
///
///     assert_eq!(recovered, b"example secret");
///     Ok(())
/// }
/// ```
pub fn combine_shares(
    packets: &[packet::SharePacket],
    passphrase: Option<&[u8]>,
) -> CoreResult<Vec<u8>> {
    if packets.is_empty() {
        return Err(CoreError::NotEnoughShares { k: 1, m: 0 });
    }

    let crypto_params = packets[0].crypto_params;
    for p in packets {
        if p.crypto_params != crypto_params {
            return Err(CoreError::CryptoParamsMismatch);
        }
    }

    let shares: Vec<sss::RawShare> = packets
        .iter()
        .map(packet::SharePacket::to_raw_share)
        .collect::<CoreResult<_>>()?;

    let combined = Zeroizing::new(sss::combine(&shares)?);
    if combined.len() < INTEGRITY_TAG_LEN {
        return Err(CoreError::InvalidCombinedLength {
            len: combined.len(),
        });
    }

    let (data, tag) = combined.split_at(combined.len() - INTEGRITY_TAG_LEN);
    let expected = blake3::hash(data);
    if expected.as_bytes() != tag {
        return Err(CoreError::IntegrityCheckFailed);
    }

    match crypto_params {
        None => Ok(data.to_vec()),
        Some(params) => {
            let passphrase = passphrase.ok_or(CoreError::PassphraseRequired)?;
            crypto::decrypt(data, passphrase, params)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::crypto::{CryptoParams, MAX_MEM_COST_KIB};
    use crate::packet::SharePacket;
    use crate::sss::SetId;

    #[test]
    fn combine_rejects_unsupported_work_factors_before_key_derivation() {
        let mut payload = b"crafted ciphertext".to_vec();
        let tag = blake3::hash(&payload);
        payload.extend_from_slice(tag.as_bytes());
        let packet = SharePacket {
            set_id: SetId([1; 16]),
            k: 1,
            n: 1,
            x: 1,
            payload,
            crypto_params: Some(CryptoParams {
                mem_cost_kib: MAX_MEM_COST_KIB + 1,
                ..CryptoParams::random_default()
            }),
        };

        let err = combine_shares(&[packet], Some(b"passphrase")).unwrap_err();
        assert!(matches!(err, CoreError::UnsupportedCryptoParams { .. }));
    }
}
