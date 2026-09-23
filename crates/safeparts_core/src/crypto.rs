use chacha20poly1305::aead::{Aead, KeyInit};
use chacha20poly1305::{ChaCha20Poly1305, Key, Nonce};
use rand::RngCore;
use rand::rngs::OsRng;
use zeroize::Zeroizing;

use crate::error::{CoreError, CoreResult};

pub const SALT_LEN: usize = 16;
pub const NONCE_LEN: usize = 12;
pub const MIN_MEM_COST_KIB: u32 = 8 * 1024;
pub const MAX_MEM_COST_KIB: u32 = 256 * 1024;
pub const MIN_TIME_COST: u32 = 1;
pub const MAX_TIME_COST: u32 = 10;
pub const MIN_PARALLELISM: u32 = 1;
pub const MAX_PARALLELISM: u32 = 4;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CryptoWorkFactor {
    pub mem_cost_kib: u32,
    pub time_cost: u32,
    pub parallelism: u32,
}

impl CryptoWorkFactor {
    pub const DEFAULT: Self = Self {
        mem_cost_kib: 65_536,
        time_cost: 3,
        parallelism: 1,
    };

    pub fn validate_policy(self) -> CoreResult<()> {
        let supported = (MIN_MEM_COST_KIB..=MAX_MEM_COST_KIB).contains(&self.mem_cost_kib)
            && (MIN_TIME_COST..=MAX_TIME_COST).contains(&self.time_cost)
            && (MIN_PARALLELISM..=MAX_PARALLELISM).contains(&self.parallelism);
        if !supported {
            return Err(CoreError::UnsupportedCryptoParams {
                mem_cost_kib: self.mem_cost_kib,
                time_cost: self.time_cost,
                parallelism: self.parallelism,
            });
        }
        Ok(())
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct CryptoParams {
    pub salt: [u8; SALT_LEN],
    pub nonce: [u8; NONCE_LEN],
    pub mem_cost_kib: u32,
    pub time_cost: u32,
    pub parallelism: u32,
}

impl CryptoParams {
    pub fn random_default() -> Self {
        Self::random_with_work_factor(CryptoWorkFactor::DEFAULT)
    }

    pub fn random_with_work_factor(work_factor: CryptoWorkFactor) -> Self {
        let mut salt = [0u8; SALT_LEN];
        let mut nonce = [0u8; NONCE_LEN];
        OsRng.fill_bytes(&mut salt);
        OsRng.fill_bytes(&mut nonce);

        Self {
            salt,
            nonce,
            mem_cost_kib: work_factor.mem_cost_kib,
            time_cost: work_factor.time_cost,
            parallelism: work_factor.parallelism,
        }
    }

    pub fn validate_policy(&self) -> CoreResult<()> {
        CryptoWorkFactor {
            mem_cost_kib: self.mem_cost_kib,
            time_cost: self.time_cost,
            parallelism: self.parallelism,
        }
        .validate_policy()
    }
}

pub fn encrypt(plaintext: &[u8], passphrase: &[u8]) -> CoreResult<(Vec<u8>, CryptoParams)> {
    encrypt_with_work_factor(plaintext, passphrase, CryptoWorkFactor::DEFAULT)
}

pub fn encrypt_with_work_factor(
    plaintext: &[u8],
    passphrase: &[u8],
    work_factor: CryptoWorkFactor,
) -> CoreResult<(Vec<u8>, CryptoParams)> {
    work_factor.validate_policy()?;
    let params = CryptoParams::random_with_work_factor(work_factor);
    let key = derive_key(passphrase, &params)?;

    let cipher = ChaCha20Poly1305::new(Key::from_slice(key.as_ref()));
    let nonce = Nonce::from_slice(&params.nonce);

    let ciphertext = cipher
        .encrypt(nonce, plaintext)
        .map_err(|_| CoreError::EncryptFailed)?;

    Ok((ciphertext, params))
}

pub fn decrypt(ciphertext: &[u8], passphrase: &[u8], params: CryptoParams) -> CoreResult<Vec<u8>> {
    let key = derive_key(passphrase, &params)?;

    let cipher = ChaCha20Poly1305::new(Key::from_slice(key.as_ref()));
    let nonce = Nonce::from_slice(&params.nonce);

    cipher
        .decrypt(nonce, ciphertext)
        .map_err(|_| CoreError::DecryptFailed)
}

fn derive_key(passphrase: &[u8], params: &CryptoParams) -> CoreResult<Zeroizing<[u8; 32]>> {
    params.validate_policy()?;
    let argon_params = argon2::Params::new(
        params.mem_cost_kib,
        params.time_cost,
        params.parallelism,
        Some(32),
    )
    .map_err(|e| CoreError::Crypto(format!("argon2 params: {e}")))?;

    let argon = argon2::Argon2::new(
        argon2::Algorithm::Argon2id,
        argon2::Version::V0x13,
        argon_params,
    );

    let mut key = Zeroizing::new([0u8; 32]);
    argon
        .hash_password_into(passphrase, &params.salt, key.as_mut())
        .map_err(|e| CoreError::Crypto(format!("argon2: {e}")))?;

    Ok(key)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encrypt_decrypt_round_trip() {
        let plaintext = b"secret";
        let passphrase = b"pass";

        let (ciphertext, params) = encrypt(plaintext, passphrase).unwrap();
        let recovered = decrypt(&ciphertext, passphrase, params).unwrap();
        assert_eq!(recovered, plaintext);
    }

    #[test]
    fn wrong_passphrase_fails() {
        let plaintext = b"secret";
        let passphrase = b"pass";

        let (ciphertext, params) = encrypt(plaintext, passphrase).unwrap();
        let err = decrypt(&ciphertext, b"wrong", params).unwrap_err();
        assert!(matches!(
            err,
            CoreError::DecryptFailed | CoreError::Crypto(_)
        ));
    }

    #[test]
    fn work_factor_policy_accepts_defaults_and_rejects_out_of_range_values() {
        CryptoParams::random_default().validate_policy().unwrap();

        for params in [
            CryptoParams {
                mem_cost_kib: MIN_MEM_COST_KIB - 1,
                ..CryptoParams::random_default()
            },
            CryptoParams {
                mem_cost_kib: MAX_MEM_COST_KIB + 1,
                ..CryptoParams::random_default()
            },
            CryptoParams {
                time_cost: MAX_TIME_COST + 1,
                ..CryptoParams::random_default()
            },
            CryptoParams {
                parallelism: MAX_PARALLELISM + 1,
                ..CryptoParams::random_default()
            },
        ] {
            assert!(matches!(
                params.validate_policy(),
                Err(CoreError::UnsupportedCryptoParams { .. })
            ));
        }
    }
}
