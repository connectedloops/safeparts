use thiserror::Error;

pub type CoreResult<T> = Result<T, CoreError>;

#[derive(Debug, Error)]
pub enum CoreError {
    #[error("invalid parameters: require 1 <= k <= n <= 255, got k={k}, n={n}")]
    InvalidKAndN { k: u8, n: u8 },

    #[error("need at least k shares: need {k}, got {m}")]
    NotEnoughShares { k: u8, m: usize },

    #[error("share set metadata mismatch")]
    InconsistentMetadata,

    #[error("duplicate x coordinate {x}")]
    DuplicateX { x: u8 },

    #[error("invalid x coordinate 0")]
    InvalidX,

    #[error("share index {x} exceeds declared share count {n}")]
    InvalidShareIndex { x: u8, n: u8 },

    #[error("received more shares than declared: share count {n}, got {m}")]
    TooManyShares { n: u8, m: usize },

    #[error("unsupported packet version {version}")]
    UnsupportedPacketVersion { version: u8 },

    #[error("recovery shares use mixed encodings")]
    MixedEncoding,

    #[error("unsupported packet flags 0x{flags:02x} for version {version}")]
    UnsupportedPacketFlags { version: u8, flags: u8 },

    #[error("cannot invert zero")]
    DivisionByZero,

    #[error("invalid combined length {len}")]
    InvalidCombinedLength { len: usize },

    #[error("integrity check failed")]
    IntegrityCheckFailed,

    #[error("invalid packet: {0}")]
    InvalidPacket(String),

    #[error("encoding error: {0}")]
    Encoding(String),

    #[error("unknown encoding: {0}")]
    UnknownEncoding(String),

    #[error("no shares provided")]
    EmptyShareInput,

    #[error("could not detect share encoding")]
    CouldNotDetectEncoding,

    #[error("auto encoding is not valid for output")]
    AutoEncodingForOutput,

    #[error("crypto error: {0}")]
    Crypto(String),

    #[error("encryption failed")]
    EncryptFailed,

    #[error("decryption failed")]
    DecryptFailed,

    #[error("passphrase required")]
    PassphraseRequired,

    #[error(
        "unsupported crypto parameters: memory={mem_cost_kib} KiB, time={time_cost}, parallelism={parallelism}"
    )]
    UnsupportedCryptoParams {
        mem_cost_kib: u32,
        time_cost: u32,
        parallelism: u32,
    },

    #[error("crypto params mismatch")]
    CryptoParamsMismatch,
}

impl CoreError {
    /// Return a user-facing message that does not include recovery-share input.
    pub fn user_message(&self) -> String {
        match self {
            Self::InvalidPacket(_) | Self::Encoding(_) => {
                "recovery shares could not be decoded".to_string()
            }
            Self::Crypto(_) => "cryptographic operation failed".to_string(),
            other => other.to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::CoreError;

    #[test]
    fn user_message_preserves_safe_structured_diagnostics() {
        let error = CoreError::InvalidShareIndex { x: 4, n: 3 };

        assert_eq!(error.user_message(), error.to_string());
    }
}
