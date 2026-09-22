#![forbid(unsafe_code)]

use std::mem::size_of;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::str;
use std::sync::Once;

use safeparts_core::encoding::{
    Encoding as CoreEncoding, encode_packet, parse_share_packets_wrapped_mnemonics_with_versions,
};
use safeparts_core::packet::{PacketVersion, SharePacket};
use safeparts_core::{CoreError, combine_shares, inspect_share_set, split_secret};
use zeroize::{Zeroize, Zeroizing};

use crate::bridge::ffi::{BytesOutput, OperationOutput, ShareEncoding, Status};

const MIB: usize = 1_048_576;
const MAX_SECRET_BYTES: usize = MIB;
const MAX_PASSPHRASE_BYTES: usize = MIB;
const MAX_LOGICAL_VOLUME: usize = 16 * MIB;
const MAX_PASTE_BYTES: usize = 16 * MIB;
const MAX_PASTE_TOKENS: usize = 1_048_576;
const MAX_RETAINED_INPUT_BYTES: usize = 160 * MIB;
const MAX_ACCEPTED_SHARES: usize = 255;
const MAX_ENCODED_SHARE_BYTES: usize = 8 * MIB;
const OPERATION_STATE_BUDGET: usize = 256 * MIB;
const PHASE_WORKSPACE_BUDGET: usize = 256 * MIB;
const UI_RUNTIME_HEADROOM: usize = 256 * MIB;
const PROCESS_MEMORY_BUDGET: usize = 1_024 * MIB;
const PACKET_WIRE_OVERHEAD: usize = 117;
const PARSER_FIXED_WORKSPACE: usize = MIB;

static PANIC_HOOK: Once = Once::new();

pub struct Operation {
    created_packets: Vec<SharePacket>,
    created_encoding: CoreEncoding,
    recovery_batches: Vec<Zeroizing<Vec<u8>>>,
    recovery_packets: Vec<SharePacket>,
    recovery_encoding: Option<CoreEncoding>,
    recovery_version: Option<PacketVersion>,
    recovered: Zeroizing<Vec<u8>>,
    has_recovered: bool,
    inspection: OperationOutput,
}

pub fn new_operation() -> Box<Operation> {
    PANIC_HOOK.call_once(|| std::panic::set_hook(Box::new(|_| {})));
    Box::new(Operation {
        created_packets: Vec::new(),
        created_encoding: CoreEncoding::MnemoWords,
        recovery_batches: Vec::new(),
        recovery_packets: Vec::new(),
        recovery_encoding: None,
        recovery_version: None,
        recovered: Zeroizing::new(Vec::new()),
        has_recovered: false,
        inspection: output(0, Status::NotEnoughShares),
    })
}

pub fn destroy_operation(operation: Box<Operation>) {
    drop(operation);
}

impl Operation {
    pub fn reset(&mut self, generation: u64) -> OperationOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            self.clear_all();
            self.inspection = output(generation, Status::NotEnoughShares);
            clone_output(&self.inspection)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    pub fn create_words(
        &mut self,
        generation: u64,
        secret: &[u8],
        threshold: u8,
        share_count: u8,
    ) -> OperationOutput {
        self.create(
            generation,
            secret,
            threshold,
            share_count,
            ShareEncoding::MnemoWords,
        )
    }

    pub fn create(
        &mut self,
        generation: u64,
        secret: &[u8],
        threshold: u8,
        share_count: u8,
        encoding: ShareEncoding,
    ) -> OperationOutput {
        self.create_with_passphrase(generation, secret, threshold, share_count, encoding, &[])
    }

    pub fn create_with_passphrase(
        &mut self,
        generation: u64,
        secret: &[u8],
        threshold: u8,
        share_count: u8,
        encoding: ShareEncoding,
        passphrase: &[u8],
    ) -> OperationOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            self.create_inner(
                generation,
                secret,
                threshold,
                share_count,
                encoding,
                passphrase,
            )
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    fn create_inner(
        &mut self,
        generation: u64,
        secret: &[u8],
        threshold: u8,
        share_count: u8,
        encoding: ShareEncoding,
        passphrase: &[u8],
    ) -> OperationOutput {
        let Some(encoding) = concrete_core_encoding(encoding) else {
            return output(generation, Status::UnsupportedInput);
        };
        if secret.is_empty() {
            return output(generation, Status::EmptySecret);
        }
        if secret.len() > MAX_SECRET_BYTES {
            return output(generation, Status::SecretTooLarge);
        }
        if passphrase.len() > MAX_PASSPHRASE_BYTES {
            return output(generation, Status::PassphraseTooLarge);
        }
        if std::str::from_utf8(passphrase).is_err() {
            return output(generation, Status::InvalidUtf8);
        }
        if threshold == 0 || share_count == 0 || threshold > share_count {
            return output(generation, Status::InvalidThreshold);
        }
        let Some(logical_volume) = secret.len().checked_mul(usize::from(share_count)) else {
            return output(generation, Status::ResourceLimit);
        };
        if logical_volume > MAX_LOGICAL_VOLUME {
            return output(generation, Status::LogicalVolumeTooLarge);
        }

        let passphrase = (!passphrase.is_empty()).then_some(passphrase);
        let packets = match split_secret(secret, threshold, share_count, passphrase) {
            Ok(packets) => packets,
            Err(error) => return output(generation, status_from_core(&error)),
        };
        clear_packets(&mut self.created_packets);
        self.created_packets = packets;
        self.created_encoding = encoding;
        let mut result = output(generation, Status::Ok);
        result.encoding = bridge_encoding(encoding);
        result.threshold = threshold;
        result.share_count = u16::from(share_count);
        result
    }

    pub fn encode_share(&mut self, generation: u64, share_index: u16) -> BytesOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            self.encode_share_inner(generation, share_index)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_bytes(generation),
        }
    }

    fn encode_share_inner(&self, generation: u64, share_index: u16) -> BytesOutput {
        let Some(packet) = self.created_packets.get(usize::from(share_index)) else {
            return bytes_output(generation, Status::InvalidIndex);
        };
        let encoded = match encode_packet(packet, self.created_encoding) {
            Ok(encoded) => encoded,
            Err(error) => return bytes_output(generation, status_from_core(&error)),
        };
        if encoded.len() > MAX_ENCODED_SHARE_BYTES {
            return bytes_output(generation, Status::EncodedShareTooLarge);
        }
        BytesOutput {
            generation,
            status: Status::Ok,
            bytes: encoded.into_bytes(),
        }
    }

    pub fn add_recovery_words(&mut self, generation: u64, input: &[u8]) -> OperationOutput {
        self.add_recovery(generation, input, ShareEncoding::MnemoWords)
    }

    pub fn add_recovery(
        &mut self,
        generation: u64,
        input: &[u8],
        encoding: ShareEncoding,
    ) -> OperationOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            self.add_recovery_inner(generation, input, encoding)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    fn add_recovery_inner(
        &mut self,
        generation: u64,
        input: &[u8],
        encoding: ShareEncoding,
    ) -> OperationOutput {
        let Some(requested_encoding) = core_encoding(encoding) else {
            return self.with_batch_count(output(generation, Status::UnsupportedInput));
        };
        if input.len() > MAX_PASTE_BYTES {
            return self.with_batch_count(output(generation, Status::PasteTooLarge));
        }
        let input_text = match str::from_utf8(input) {
            Ok(input_text) => input_text,
            Err(_) => return self.with_batch_count(output(generation, Status::InvalidUtf8)),
        };
        let token_count = input_text
            .split_whitespace()
            .take(MAX_PASTE_TOKENS + 1)
            .count();
        if token_count > MAX_PASTE_TOKENS {
            return self.with_batch_count(output(generation, Status::TokenLimit));
        }
        let retained = self
            .recovery_batches
            .iter()
            .try_fold(input.len(), |total, batch| total.checked_add(batch.len()));
        let Some(retained) = retained else {
            return self.with_batch_count(output(generation, Status::ResourceLimit));
        };
        if retained > MAX_RETAINED_INPUT_BYTES {
            return self.with_batch_count(output(generation, Status::RetainedInputTooLarge));
        }
        if !self.recovery_batch_fits_memory(input.len(), token_count, requested_encoding) {
            return self.with_batch_count(output(generation, Status::ResourceLimit));
        }

        let mut retained_input = Vec::new();
        if retained_input.try_reserve_exact(input.len()).is_err()
            || self.recovery_batches.try_reserve(1).is_err()
        {
            return self.with_batch_count(output(generation, Status::ResourceLimit));
        }
        retained_input.extend_from_slice(input);
        self.recovery_batches.push(Zeroizing::new(retained_input));
        self.inspect_recovery(generation, requested_encoding)
    }

    pub fn remove_recovery_batch(&mut self, generation: u64, batch_index: u16) -> OperationOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            if usize::from(batch_index) >= self.recovery_batches.len() {
                return self.with_batch_count(output(generation, Status::InvalidIndex));
            }
            self.recovery_batches.remove(usize::from(batch_index));
            self.inspect_recovery(generation, CoreEncoding::Auto)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    fn inspect_recovery(
        &mut self,
        generation: u64,
        requested_encoding: CoreEncoding,
    ) -> OperationOutput {
        self.clear_recovered();
        clear_packets(&mut self.recovery_packets);
        self.recovery_encoding = None;
        self.recovery_version = None;
        if self.recovery_batches.is_empty() {
            self.inspection = self.with_batch_count(output(generation, Status::NotEnoughShares));
            return clone_output(&self.inspection);
        }

        let mut packets = Vec::new();
        for batch in &self.recovery_batches {
            let text = match str::from_utf8(batch) {
                Ok(text) => text,
                Err(_) => {
                    clear_packets(&mut packets);
                    self.inspection =
                        self.with_batch_count(output(generation, Status::InvalidUtf8));
                    return clone_output(&self.inspection);
                }
            };
            let parsed =
                match parse_share_packets_wrapped_mnemonics_with_versions(text, requested_encoding)
                {
                    Ok(parsed) => parsed,
                    Err(error) => {
                        clear_packets(&mut packets);
                        self.inspection =
                            self.with_batch_count(output(generation, status_from_core(&error)));
                        return clone_output(&self.inspection);
                    }
                };
            if let Some(existing) = self.recovery_encoding {
                if existing != parsed.encoding {
                    clear_decoded_packets(parsed.packets);
                    clear_packets(&mut packets);
                    self.inspection =
                        self.with_batch_count(output(generation, Status::MixedEncoding));
                    return clone_output(&self.inspection);
                }
            } else {
                self.recovery_encoding = Some(parsed.encoding);
            }
            for item in &parsed.packets {
                if self
                    .recovery_version
                    .is_some_and(|version| version != item.version)
                {
                    clear_decoded_packets(parsed.packets);
                    clear_packets(&mut packets);
                    self.inspection =
                        self.with_batch_count(output(generation, Status::MixedVersion));
                    return clone_output(&self.inspection);
                }
                self.recovery_version = Some(item.version);
            }
            packets.extend(parsed.packets.into_iter().map(|item| item.packet));
            if packets.len() > MAX_ACCEPTED_SHARES {
                clear_packets(&mut packets);
                self.inspection = self.with_batch_count(output(generation, Status::TooManyShares));
                return clone_output(&self.inspection);
            }
            if !self.decoded_packets_fit_memory(&packets, packets.capacity()) {
                clear_packets(&mut packets);
                self.inspection = self.with_batch_count(output(generation, Status::ResourceLimit));
                return clone_output(&self.inspection);
            }
        }

        let inspected = match inspect_share_set(&packets) {
            Ok(inspected) => inspected,
            Err(error) => {
                clear_packets(&mut packets);
                self.inspection =
                    self.with_batch_count(output(generation, status_from_core(&error)));
                return clone_output(&self.inspection);
            }
        };
        if inspected.expected_secret_len > MAX_SECRET_BYTES {
            clear_packets(&mut packets);
            self.inspection = self.with_batch_count(output(generation, Status::SecretTooLarge));
            return clone_output(&self.inspection);
        }
        let Some(logical_volume) = inspected
            .expected_secret_len
            .checked_mul(inspected.supplied_count)
        else {
            clear_packets(&mut packets);
            self.inspection = self.with_batch_count(output(generation, Status::ResourceLimit));
            return clone_output(&self.inspection);
        };
        if logical_volume > MAX_LOGICAL_VOLUME {
            clear_packets(&mut packets);
            self.inspection =
                self.with_batch_count(output(generation, Status::LogicalVolumeTooLarge));
            return clone_output(&self.inspection);
        }

        self.recovery_packets = packets;
        let protected = inspected.passphrase_protected;
        let enough_shares = inspected.supplied_count >= usize::from(inspected.threshold);
        let ready = !protected && enough_shares;
        let mut result = output(
            generation,
            if protected {
                Status::PassphraseRequired
            } else if ready {
                Status::Ok
            } else {
                Status::NotEnoughShares
            },
        );
        result.encoding = self
            .recovery_encoding
            .map(bridge_encoding)
            .unwrap_or(ShareEncoding::Auto);
        result.passphrase_protected = protected;
        result.threshold = inspected.threshold;
        result.share_count = u16::from(inspected.share_count);
        result.supplied_count = u16::try_from(inspected.supplied_count).unwrap_or(u16::MAX);
        result.recovery_batch_count =
            u16::try_from(self.recovery_batches.len()).unwrap_or(u16::MAX);
        result.ready = ready;
        self.inspection = clone_output(&result);
        result
    }

    pub fn recover_words(&mut self, generation: u64) -> BytesOutput {
        self.recover(generation)
    }

    pub fn recover(&mut self, generation: u64) -> BytesOutput {
        self.recover_with_passphrase(generation, &[])
    }

    pub fn recover_with_passphrase(&mut self, generation: u64, passphrase: &[u8]) -> BytesOutput {
        self.recover_entry(generation, passphrase, true)
    }

    pub fn recover_bytes_with_passphrase(
        &mut self,
        generation: u64,
        passphrase: &[u8],
    ) -> BytesOutput {
        self.recover_entry(generation, passphrase, false)
    }

    fn recover_entry(
        &mut self,
        generation: u64,
        passphrase: &[u8],
        require_utf8: bool,
    ) -> BytesOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            self.clear_recovered();
            self.recover_inner(generation, passphrase, require_utf8)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_bytes(generation),
        }
    }

    fn recover_inner(
        &mut self,
        generation: u64,
        passphrase: &[u8],
        require_utf8: bool,
    ) -> BytesOutput {
        if passphrase.len() > MAX_PASSPHRASE_BYTES {
            return bytes_output(generation, Status::PassphraseTooLarge);
        }
        if std::str::from_utf8(passphrase).is_err() {
            return bytes_output(generation, Status::InvalidUtf8);
        }
        let protected = self.inspection.passphrase_protected;
        if self.recovery_packets.len() < usize::from(self.inspection.threshold) {
            return bytes_output(generation, Status::NotEnoughShares);
        }
        if protected && passphrase.is_empty() {
            return bytes_output(generation, Status::PassphraseRequired);
        }
        if !protected && (!self.inspection.ready || self.inspection.status != Status::Ok) {
            return bytes_output(generation, self.inspection.status);
        }
        let passphrase = protected.then_some(passphrase);
        let recovered = match combine_shares(&self.recovery_packets, passphrase) {
            Ok(recovered) => Zeroizing::new(recovered),
            Err(error) => return bytes_output(generation, status_from_core(&error)),
        };
        if recovered.len() > MAX_SECRET_BYTES {
            return bytes_output(generation, Status::SecretTooLarge);
        }
        if require_utf8 && str::from_utf8(&recovered).is_err() {
            return bytes_output(generation, Status::InvalidUtf8);
        }

        self.recovered = recovered;
        self.has_recovered = true;
        BytesOutput {
            generation,
            status: Status::Ok,
            bytes: self.recovered.to_vec(),
        }
    }

    pub fn recovered_bytes(&mut self, generation: u64) -> BytesOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            if !self.has_recovered {
                bytes_output(generation, Status::NotEnoughShares)
            } else {
                BytesOutput {
                    generation,
                    status: Status::Ok,
                    bytes: self.recovered.to_vec(),
                }
            }
        })) {
            Ok(result) => result,
            Err(_) => self.panic_bytes(generation),
        }
    }

    pub fn recovered_text(&mut self, generation: u64) -> BytesOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            if !self.has_recovered {
                bytes_output(generation, Status::NotEnoughShares)
            } else if str::from_utf8(&self.recovered).is_err() {
                bytes_output(generation, Status::InvalidUtf8)
            } else {
                BytesOutput {
                    generation,
                    status: Status::Ok,
                    bytes: self.recovered.to_vec(),
                }
            }
        })) {
            Ok(result) => result,
            Err(_) => self.panic_bytes(generation),
        }
    }

    fn recovery_batch_fits_memory(
        &self,
        input_len: usize,
        token_count: usize,
        requested_encoding: CoreEncoding,
    ) -> bool {
        let Some(current_state) = operation_state_bytes(
            &self.created_packets,
            self.created_packets.capacity(),
            &self.recovery_batches,
            self.recovery_batches.capacity(),
            &self.recovery_packets,
            self.recovery_packets.capacity(),
            self.recovered.capacity(),
        ) else {
            return false;
        };
        let Some(current_with_input) = current_state.checked_add(input_len) else {
            return false;
        };
        if current_with_input > OPERATION_STATE_BUDGET {
            return false;
        }

        let raw_payload_after = self
            .recovery_batches
            .iter()
            .try_fold(input_len, |total, batch| {
                total.checked_add(batch.capacity())
            });
        let raw_headers_after = self
            .recovery_batches
            .len()
            .checked_add(1)
            .and_then(|count| count.checked_mul(2))
            .and_then(|capacity| capacity.checked_mul(size_of::<Zeroizing<Vec<u8>>>()));
        let Some(raw_payload_after) = raw_payload_after else {
            return false;
        };
        let Some(raw_after) =
            raw_headers_after.and_then(|headers| raw_payload_after.checked_add(headers))
        else {
            return false;
        };
        let Some(created_state) =
            packet_storage_bytes(&self.created_packets, self.created_packets.capacity())
        else {
            return false;
        };
        // Before packet metadata is available, the retained text itself is the only
        // trustworthy upper bound. Compact decoders cannot produce more bytes than
        // their input (Base64url is tighter); mnemonic decoders are also bounded by
        // input length. Account that decoded storage before invoking the parser.
        let decoded_payload_bound =
            decoded_bytes_upper_bound(raw_payload_after, requested_encoding);
        let decoded_state = decoded_payload_bound
            .and_then(|payload| {
                MAX_ACCEPTED_SHARES
                    .checked_mul(PACKET_WIRE_OVERHEAD)
                    .and_then(|overhead| payload.checked_add(overhead))
            })
            .and_then(|bytes| {
                MAX_ACCEPTED_SHARES
                    .checked_mul(size_of::<SharePacket>())
                    .and_then(|headers| bytes.checked_add(headers))
            });
        let Some(next_state) = decoded_state
            .and_then(|bytes| bytes.checked_add(raw_after))
            .and_then(|bytes| bytes.checked_add(created_state))
        else {
            return false;
        };
        if next_state > OPERATION_STATE_BUDGET {
            return false;
        }

        let mut largest_batch = input_len;
        let mut largest_tokens = token_count;
        for batch in &self.recovery_batches {
            largest_batch = largest_batch.max(batch.len());
            let Ok(text) = str::from_utf8(batch) else {
                return false;
            };
            largest_tokens =
                largest_tokens.max(text.split_whitespace().take(MAX_PASTE_TOKENS + 1).count());
        }
        let parser_refs = largest_tokens
            .checked_mul(size_of::<&str>())
            .and_then(|bytes| bytes.checked_mul(2));
        let decoded_scratch = largest_tokens
            .checked_mul(11)
            .and_then(|bits| bits.checked_add(7))
            .map(|bits| bits / 8);
        let parser_packet_headers =
            largest_tokens.checked_mul(size_of::<safeparts_core::packet::DecodedSharePacket>());
        let largest_decoded = decoded_bytes_upper_bound(largest_batch, requested_encoding);
        let workspace = input_len
            .checked_mul(3)
            .and_then(|bytes| {
                largest_batch
                    .checked_mul(2)
                    .and_then(|parser_copies| bytes.checked_add(parser_copies))
            })
            .and_then(|bytes| parser_refs.and_then(|refs| bytes.checked_add(refs)))
            .and_then(|bytes| decoded_scratch.and_then(|scratch| bytes.checked_add(scratch)))
            .and_then(|bytes| parser_packet_headers.and_then(|headers| bytes.checked_add(headers)))
            .and_then(|bytes| largest_decoded.and_then(|decoded| bytes.checked_add(decoded)))
            .and_then(|bytes| bytes.checked_add(PARSER_FIXED_WORKSPACE));
        let Some(workspace) = workspace else {
            return false;
        };
        if workspace > PHASE_WORKSPACE_BUDGET {
            return false;
        }
        next_state
            .checked_add(workspace)
            .and_then(|bytes| bytes.checked_add(UI_RUNTIME_HEADROOM))
            .is_some_and(|bytes| bytes <= PROCESS_MEMORY_BUDGET)
    }

    fn decoded_packets_fit_memory(&self, packets: &[SharePacket], packet_capacity: usize) -> bool {
        let Some(raw) =
            recovery_batch_storage_bytes(&self.recovery_batches, self.recovery_batches.capacity())
        else {
            return false;
        };
        let Some(created) =
            packet_storage_bytes(&self.created_packets, self.created_packets.capacity())
        else {
            return false;
        };
        let Some(decoded) = packet_storage_bytes(packets, packet_capacity) else {
            return false;
        };
        raw.checked_add(created)
            .and_then(|bytes| bytes.checked_add(decoded))
            .is_some_and(|bytes| bytes <= OPERATION_STATE_BUDGET)
    }

    fn with_batch_count(&self, mut result: OperationOutput) -> OperationOutput {
        result.recovery_batch_count =
            u16::try_from(self.recovery_batches.len()).unwrap_or(u16::MAX);
        result
    }

    fn clear_recovered(&mut self) {
        self.recovered.zeroize();
        self.recovered.clear();
        self.has_recovered = false;
    }

    fn clear_all(&mut self) {
        clear_packets(&mut self.created_packets);
        self.recovery_batches.clear();
        clear_packets(&mut self.recovery_packets);
        self.recovery_encoding = None;
        self.recovery_version = None;
        self.clear_recovered();
    }

    fn panic_output(&mut self, generation: u64) -> OperationOutput {
        self.clear_all();
        self.inspection = output(generation, Status::InternalPanic);
        clone_output(&self.inspection)
    }

    fn panic_bytes(&mut self, generation: u64) -> BytesOutput {
        self.clear_all();
        self.inspection = output(generation, Status::InternalPanic);
        bytes_output(generation, Status::InternalPanic)
    }
}

impl Drop for Operation {
    fn drop(&mut self) {
        self.clear_all();
    }
}

fn decoded_bytes_upper_bound(encoded_bytes: usize, encoding: CoreEncoding) -> Option<usize> {
    match encoding {
        CoreEncoding::Base64url => encoded_bytes
            .checked_mul(3)
            .and_then(|bytes| bytes.checked_add(3))
            .map(|bytes| bytes / 4),
        CoreEncoding::Auto
        | CoreEncoding::Base58check
        | CoreEncoding::MnemoWords
        | CoreEncoding::MnemoBip39 => Some(encoded_bytes),
        _ => None,
    }
}

fn packet_storage_bytes(packets: &[SharePacket], outer_capacity: usize) -> Option<usize> {
    outer_capacity
        .checked_mul(size_of::<SharePacket>())
        .and_then(|headers| {
            packets.iter().try_fold(headers, |total, packet| {
                total.checked_add(packet.payload.capacity())
            })
        })
}

fn recovery_batch_storage_bytes(
    batches: &[Zeroizing<Vec<u8>>],
    outer_capacity: usize,
) -> Option<usize> {
    outer_capacity
        .checked_mul(size_of::<Zeroizing<Vec<u8>>>())
        .and_then(|headers| {
            batches
                .iter()
                .try_fold(headers, |total, batch| total.checked_add(batch.capacity()))
        })
}

fn operation_state_bytes(
    created: &[SharePacket],
    created_capacity: usize,
    batches: &[Zeroizing<Vec<u8>>],
    batch_capacity: usize,
    recovery: &[SharePacket],
    recovery_capacity: usize,
    recovered_capacity: usize,
) -> Option<usize> {
    packet_storage_bytes(created, created_capacity)
        .and_then(|bytes| {
            recovery_batch_storage_bytes(batches, batch_capacity)
                .and_then(|raw| bytes.checked_add(raw))
        })
        .and_then(|bytes| {
            packet_storage_bytes(recovery, recovery_capacity)
                .and_then(|packets| bytes.checked_add(packets))
        })
        .and_then(|bytes| bytes.checked_add(recovered_capacity))
}

fn clear_packets(packets: &mut Vec<SharePacket>) {
    for packet in packets.iter_mut() {
        packet.payload.zeroize();
    }
    packets.clear();
}

fn clear_decoded_packets(mut packets: Vec<safeparts_core::packet::DecodedSharePacket>) {
    for item in &mut packets {
        item.packet.payload.zeroize();
    }
}

fn core_encoding(encoding: ShareEncoding) -> Option<CoreEncoding> {
    match encoding {
        ShareEncoding::Auto => Some(CoreEncoding::Auto),
        ShareEncoding::Base64url => Some(CoreEncoding::Base64url),
        ShareEncoding::Base58check => Some(CoreEncoding::Base58check),
        ShareEncoding::MnemoWords => Some(CoreEncoding::MnemoWords),
        ShareEncoding::MnemoBip39 => Some(CoreEncoding::MnemoBip39),
        _ => None,
    }
}

fn concrete_core_encoding(encoding: ShareEncoding) -> Option<CoreEncoding> {
    core_encoding(encoding).filter(|encoding| !encoding.is_auto())
}

fn bridge_encoding(encoding: CoreEncoding) -> ShareEncoding {
    match encoding {
        CoreEncoding::Auto => ShareEncoding::Auto,
        CoreEncoding::Base64url => ShareEncoding::Base64url,
        CoreEncoding::Base58check => ShareEncoding::Base58check,
        CoreEncoding::MnemoWords => ShareEncoding::MnemoWords,
        CoreEncoding::MnemoBip39 => ShareEncoding::MnemoBip39,
        _ => ShareEncoding::Auto,
    }
}

fn status_from_core(error: &CoreError) -> Status {
    match error {
        CoreError::InvalidKAndN { .. } => Status::InvalidThreshold,
        CoreError::NotEnoughShares { .. } => Status::NotEnoughShares,
        CoreError::DuplicateX { .. } => Status::DuplicateShare,
        CoreError::InconsistentMetadata
        | CoreError::CryptoParamsMismatch
        | CoreError::InvalidShareIndex { .. }
        | CoreError::InvalidX => Status::MixedShareSet,
        CoreError::TooManyShares { .. } => Status::TooManyShares,
        CoreError::IntegrityCheckFailed | CoreError::DecryptFailed => Status::IntegrityFailure,
        CoreError::PassphraseRequired => Status::PassphraseRequired,
        CoreError::UnsupportedPacketVersion { .. } => Status::UnsupportedVersion,
        CoreError::MixedEncoding => Status::MixedEncoding,
        CoreError::UnsupportedCryptoParams { .. } | CoreError::UnsupportedPacketFlags { .. } => {
            Status::UnsupportedParameters
        }
        CoreError::InvalidPacket(_)
        | CoreError::Encoding(_)
        | CoreError::EmptyShareInput
        | CoreError::CouldNotDetectEncoding => Status::MalformedInput,
        _ => Status::CoreError,
    }
}

fn output(generation: u64, status: Status) -> OperationOutput {
    OperationOutput {
        generation,
        status,
        threshold: 0,
        share_count: 0,
        supplied_count: 0,
        recovery_batch_count: 0,
        encoding: ShareEncoding::Auto,
        passphrase_protected: false,
        ready: false,
    }
}

fn clone_output(value: &OperationOutput) -> OperationOutput {
    OperationOutput {
        generation: value.generation,
        status: value.status,
        threshold: value.threshold,
        share_count: value.share_count,
        supplied_count: value.supplied_count,
        recovery_batch_count: value.recovery_batch_count,
        encoding: value.encoding,
        passphrase_protected: value.passphrase_protected,
        ready: value.ready,
    }
}

fn bytes_output(generation: u64, status: Status) -> BytesOutput {
    BytesOutput {
        generation,
        status,
        bytes: Vec::new(),
    }
}
