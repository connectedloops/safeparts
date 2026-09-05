use safeparts_core::encoding::{self, Encoding};
use safeparts_core::{combine_shares, split_secret};

#[test]
fn mnemonic_framing_preserves_every_packet_and_recovers_with_lf_and_crlf() {
    let secret = b"synthetic mnemonic framing secret";
    let packets = split_secret(secret, 2, 3, None).unwrap();
    for encoding in [Encoding::MnemoWords, Encoding::MnemoBip39] {
        let shares = packets
            .iter()
            .map(|packet| encoding::encode_packet(packet, encoding).unwrap())
            .collect::<Vec<_>>();
        let wrapped = shares
            .iter()
            .map(|share| {
                share
                    .split_whitespace()
                    .collect::<Vec<_>>()
                    .chunks(8)
                    .map(|words| words.join(" "))
                    .collect::<Vec<_>>()
                    .join("\n")
            })
            .collect::<Vec<_>>();
        for count in [1, 2, 3] {
            for input in [shares[..count].join("\n"), wrapped[..count].join("\n\n")] {
                for ending in ["\n", "\r\n"] {
                    let input = format!("\n{input}\n\n").replace('\n', ending);
                    for mode in [Encoding::Auto, encoding] {
                        let parsed =
                            encoding::parse_share_packets_wrapped_mnemonics(&input, mode).unwrap();
                        assert_eq!(parsed.encoding, encoding);
                        assert_eq!(parsed.packets, packets[..count]);
                        if count >= 2 {
                            assert_eq!(combine_shares(&parsed.packets, None).unwrap(), secret);
                        }
                    }
                }
            }
        }
    }
}

#[test]
fn malformed_mnemonic_suffixes_are_not_discarded_after_threshold() {
    let packets = split_secret(b"synthetic strict framing secret", 2, 3, None).unwrap();
    for encoding in [Encoding::MnemoWords, Encoding::MnemoBip39] {
        let shares = packets
            .iter()
            .map(|packet| encoding::encode_packet(packet, encoding).unwrap())
            .collect::<Vec<_>>();
        let words = shares[2].split_whitespace().collect::<Vec<_>>();
        let truncated = words[..words.len() - 1].join(" ");
        let corrupt = format!("NOT-A-SHARE-WORD {}", words[1..].join(" "));
        let extra_word = format!("{} NOT-A-SHARE-WORD", shares[2]);
        let extra_frame = format!("{} /", shares[2]);
        for bad in [&truncated, &corrupt, &extra_word, &extra_frame] {
            for separator in ["\n", "\n\n", "\r\n", "\r\n\r\n"] {
                let input = [shares[0].as_str(), shares[1].as_str(), bad].join(separator);
                for mode in [Encoding::Auto, encoding] {
                    let error =
                        encoding::parse_share_packets_wrapped_mnemonics(&input, mode).unwrap_err();
                    let message = error.user_message();
                    assert!(!message.contains("NOT-A-SHARE-WORD"));
                    assert!(!message.contains(&shares[0]));
                }
            }
        }
    }
}

#[test]
fn wrapped_parser_keeps_compact_whitespace_behavior() {
    let packets = split_secret(b"synthetic compact parser secret", 2, 3, None).unwrap();
    for encoding in [Encoding::Base64url, Encoding::Base58check] {
        let shares = packets
            .iter()
            .map(|packet| encoding::encode_packet(packet, encoding).unwrap())
            .collect::<Vec<_>>();
        let input = format!("\t{} {}\r\n\n{}\n", shares[0], shares[1], shares[2]);
        for mode in [Encoding::Auto, encoding] {
            let parsed = encoding::parse_share_packets_wrapped_mnemonics(&input, mode).unwrap();
            assert_eq!(parsed.packets, packets);
            assert_eq!(parsed.encoding, encoding);
            assert!(
                encoding::parse_share_packets_wrapped_mnemonics(
                    &format!("{input}NOT-A-SHARE"),
                    mode
                )
                .is_err()
            );
        }
    }
}
