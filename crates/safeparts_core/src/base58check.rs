use core::fmt;

use ibig::{UBig, ops::DivRem};
use sha2::{Digest, Sha256};
use zeroize::Zeroize;

const ALPHABET: &[u8; 58] = b"123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
const CHECKSUM_BYTES: usize = 4;
const MAX_CODEC_BYTES: usize = 8 * 1_048_576;

#[cfg(target_pointer_width = "64")]
const LEAF_DIGITS: usize = 10;
#[cfg(target_pointer_width = "64")]
const LEAF_RADIX: u64 = 430_804_206_899_405_824; // 58^10

#[cfg(not(target_pointer_width = "64"))]
const LEAF_DIGITS: usize = 5;
#[cfg(not(target_pointer_width = "64"))]
const LEAF_RADIX: u64 = 656_356_768; // 58^5

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Base58CheckError {
    InputTooLarge,
    OutputTooLarge,
    InvalidCharacter,
    InvalidChecksum,
    ArithmeticFailure,
}

impl fmt::Display for Base58CheckError {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str(match self {
            Self::InputTooLarge => "base58check input exceeds the supported limit",
            Self::OutputTooLarge => "base58check output exceeds the supported limit",
            Self::InvalidCharacter => "base58check input contains an invalid character",
            Self::InvalidChecksum => "base58check checksum mismatch",
            Self::ArithmeticFailure => "base58check conversion failed",
        })
    }
}

pub(crate) fn encode(input: &[u8]) -> Result<String, Base58CheckError> {
    if input.len() > MAX_CODEC_BYTES.saturating_sub(CHECKSUM_BYTES) {
        return Err(Base58CheckError::InputTooLarge);
    }
    let mut checked = Vec::new();
    checked
        .try_reserve_exact(input.len() + CHECKSUM_BYTES)
        .map_err(|_| Base58CheckError::OutputTooLarge)?;
    checked.extend_from_slice(input);
    checked.extend_from_slice(&checksum(input));

    let leading_zeroes = checked.iter().take_while(|&&byte| byte == 0).count();
    let number = UBig::from_be_bytes(&checked[leading_zeroes..]);
    let estimated_digits = checked
        .len()
        .checked_mul(138)
        .and_then(|value| value.checked_div(100))
        .and_then(|value| value.checked_add(2 + leading_zeroes))
        .ok_or(Base58CheckError::OutputTooLarge)?;
    if estimated_digits > MAX_CODEC_BYTES {
        checked.zeroize();
        return Err(Base58CheckError::OutputTooLarge);
    }

    let mut chunks = Vec::new();
    emit_variable_chunks(number, &mut chunks)?;
    let mut output = String::new();
    if output.try_reserve_exact(estimated_digits).is_err() {
        checked.zeroize();
        return Err(Base58CheckError::OutputTooLarge);
    }
    output.extend(core::iter::repeat_n('1', leading_zeroes));
    for (index, chunk) in chunks.into_iter().enumerate() {
        emit_chunk(&mut output, chunk, index != 0);
    }
    checked.zeroize();
    if output.len() > MAX_CODEC_BYTES {
        return Err(Base58CheckError::OutputTooLarge);
    }
    Ok(output)
}

pub(crate) fn decode(input: &str) -> Result<Vec<u8>, Base58CheckError> {
    if input.is_empty() || input.len() > MAX_CODEC_BYTES || !input.is_ascii() {
        return Err(Base58CheckError::InputTooLarge);
    }
    let leading_zeroes = input.bytes().take_while(|&byte| byte == b'1').count();
    let significant = &input.as_bytes()[leading_zeroes..];
    let predicted_bytes = significant
        .len()
        .checked_mul(733)
        .and_then(|value| value.checked_add(999))
        .and_then(|value| value.checked_div(1000))
        .and_then(|value| value.checked_add(leading_zeroes))
        .ok_or(Base58CheckError::OutputTooLarge)?;
    if predicted_bytes > MAX_CODEC_BYTES + CHECKSUM_BYTES {
        return Err(Base58CheckError::OutputTooLarge);
    }

    let mut leaves = Vec::new();
    let first_width = match significant.len() % LEAF_DIGITS {
        0 => LEAF_DIGITS,
        width => width,
    };
    let mut offset = 0;
    while offset < significant.len() {
        let width = if offset == 0 {
            first_width
        } else {
            LEAF_DIGITS
        };
        let end = offset + width;
        leaves.push((UBig::from(parse_chunk(&significant[offset..end])?), 1usize));
        offset = end;
    }
    let number = combine_balanced(&leaves)?;
    let magnitude = number.to_be_bytes();
    let total = leading_zeroes
        .checked_add(magnitude.len())
        .ok_or(Base58CheckError::OutputTooLarge)?;
    if total > MAX_CODEC_BYTES + CHECKSUM_BYTES {
        return Err(Base58CheckError::OutputTooLarge);
    }
    let mut checked = Vec::new();
    checked
        .try_reserve_exact(total)
        .map_err(|_| Base58CheckError::OutputTooLarge)?;
    checked.resize(leading_zeroes, 0);
    checked.extend_from_slice(&magnitude);
    let payload_len = checked
        .len()
        .checked_sub(CHECKSUM_BYTES)
        .ok_or(Base58CheckError::InvalidChecksum)?;
    let expected = checksum(&checked[..payload_len]);
    let mut difference = 0u8;
    for (&actual, expected) in checked[payload_len..].iter().zip(expected) {
        difference |= actual ^ expected;
    }
    if difference != 0 {
        checked.zeroize();
        return Err(Base58CheckError::InvalidChecksum);
    }
    checked.truncate(payload_len);
    Ok(checked)
}

fn checksum(input: &[u8]) -> [u8; CHECKSUM_BYTES] {
    let first = Sha256::digest(input);
    let second = Sha256::digest(first);
    [second[0], second[1], second[2], second[3]]
}

fn emit_variable_chunks(number: UBig, output: &mut Vec<u64>) -> Result<(), Base58CheckError> {
    if number < UBig::from(LEAF_RADIX) {
        output.push(u64::try_from(number).map_err(|_| Base58CheckError::ArithmeticFailure)?);
        return Ok(());
    }
    let mut powers = vec![UBig::from(LEAF_RADIX)];
    while powers.last().is_some_and(|power| power <= &number) {
        let next = powers
            .last()
            .ok_or(Base58CheckError::ArithmeticFailure)?
            .pow(2);
        if next > number {
            break;
        }
        powers.push(next);
    }
    let level = powers.len() - 1;
    let (quotient, remainder) = number.div_rem(&powers[level]);
    emit_variable_chunks(quotient, output)?;
    emit_fixed_chunks(remainder, level, &powers, output)
}

fn emit_fixed_chunks(
    number: UBig,
    level: usize,
    powers: &[UBig],
    output: &mut Vec<u64>,
) -> Result<(), Base58CheckError> {
    if level == 0 {
        output.push(u64::try_from(number).map_err(|_| Base58CheckError::ArithmeticFailure)?);
        return Ok(());
    }
    let divisor = powers
        .get(level - 1)
        .ok_or(Base58CheckError::ArithmeticFailure)?;
    let (left, right) = number.div_rem(divisor);
    emit_fixed_chunks(left, level - 1, powers, output)?;
    emit_fixed_chunks(right, level - 1, powers, output)
}

fn combine_balanced(leaves: &[(UBig, usize)]) -> Result<UBig, Base58CheckError> {
    if leaves.is_empty() {
        return Ok(UBig::from(0u8));
    }
    if leaves.len() == 1 {
        return Ok(leaves[0].0.clone());
    }
    let middle = leaves.len() / 2;
    let left = combine_balanced(&leaves[..middle])?;
    let right = combine_balanced(&leaves[middle..])?;
    let right_width = leaves[middle..]
        .iter()
        .try_fold(0usize, |total, (_, width)| total.checked_add(*width))
        .ok_or(Base58CheckError::ArithmeticFailure)?;
    Ok(left * UBig::from(LEAF_RADIX).pow(right_width) + right)
}

fn parse_chunk(input: &[u8]) -> Result<u64, Base58CheckError> {
    input.iter().try_fold(0u64, |value, &character| {
        let digit = ALPHABET
            .iter()
            .position(|&candidate| candidate == character)
            .ok_or(Base58CheckError::InvalidCharacter)? as u64;
        value
            .checked_mul(58)
            .and_then(|value| value.checked_add(digit))
            .ok_or(Base58CheckError::ArithmeticFailure)
    })
}

fn emit_chunk(output: &mut String, mut chunk: u64, padded: bool) {
    let mut digits = [b'1'; LEAF_DIGITS];
    let mut index = LEAF_DIGITS;
    while chunk != 0 {
        index -= 1;
        digits[index] = ALPHABET[(chunk % 58) as usize];
        chunk /= 58;
    }
    let begin = if padded { 0 } else { index };
    // All bytes originate from the fixed ASCII alphabet.
    output.push_str(core::str::from_utf8(&digits[begin..]).expect("base58 alphabet is ASCII"));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn independent_bitcoin_vectors_match() {
        assert_eq!(encode(b"").unwrap(), "3QJmnh");
        assert_eq!(decode("3QJmnh").unwrap(), b"");
        assert_eq!(encode(b"hello world").unwrap(), "3vQB7B6MrGQZaxCuFg4oh");
        assert_eq!(decode("3vQB7B6MrGQZaxCuFg4oh").unwrap(), b"hello world");
    }

    #[test]
    fn released_fixtures_reencode_identically() {
        for fixture in [
            include_str!("../tests/fixtures/share_compatibility/v1-unprotected/base58check.txt"),
            include_str!("../tests/fixtures/share_compatibility/v2-unprotected/base58check.txt"),
            include_str!(
                "../tests/fixtures/share_compatibility/v2-passphrase-protected/base58check.txt"
            ),
        ] {
            for token in fixture.lines() {
                let bytes = decode(token).unwrap();
                assert_eq!(encode(&bytes).unwrap(), token);
            }
        }
    }

    #[test]
    fn differential_boundaries_match_bs58() {
        for length in [0, 1, 4, 5, 9, 10, 11, 31, 32, 63, 64, 255, 1024, 4096] {
            for leading_zeroes in [0, 1, 3] {
                let mut input = (0..length)
                    .map(|index| ((index * 131 + 17) % 251) as u8)
                    .collect::<Vec<_>>();
                for byte in input.iter_mut().take(leading_zeroes) {
                    *byte = 0;
                }
                let expected = bs58::encode(&input).with_check().into_string();
                assert_eq!(encode(&input).unwrap(), expected);
                assert_eq!(decode(&expected).unwrap(), input);
            }
        }
    }

    #[test]
    #[ignore = "run through the fresh-process desktop capacity evidence task"]
    fn maximum_codec_vector_round_trips_with_pinned_shape() {
        let input = (0..1_048_576usize)
            .map(|index| ((index * 131 + 17) % 251) as u8)
            .collect::<Vec<_>>();
        let encoded = encode(&input).unwrap();
        let hash = Sha256::digest(encoded.as_bytes());
        assert_eq!(encoded.len(), 1_432_002);
        assert_eq!(
            format!("{hash:x}"),
            "a8ba2db05e699548ec46141fa07078b146cfeb551bea1b745f44a0f5816f08e8"
        );
        assert_eq!(decode(&encoded).unwrap(), input);
    }

    #[test]
    fn invalid_character_checksum_and_bounds_are_rejected() {
        assert_eq!(decode("0"), Err(Base58CheckError::InvalidCharacter));
        assert_eq!(decode("3QJmni"), Err(Base58CheckError::InvalidChecksum));
        assert_eq!(
            encode(&vec![0; MAX_CODEC_BYTES - CHECKSUM_BYTES + 1]),
            Err(Base58CheckError::InputTooLarge)
        );
        assert_eq!(
            decode(&"1".repeat(MAX_CODEC_BYTES + 1)),
            Err(Base58CheckError::InputTooLarge)
        );
    }
}
