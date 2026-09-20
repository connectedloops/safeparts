#![forbid(unsafe_code)]

use std::mem::size_of;
use std::panic::{AssertUnwindSafe, catch_unwind};
use std::str;
use std::sync::Once;

use safeparts_core::encoding::{Encoding, encode_packet, parse_mnemo_words_packets_with_versions};
use safeparts_core::packet::{PacketVersion, SharePacket};
use safeparts_core::{CoreError, combine_shares, inspect_share_set, split_secret};
use zeroize::{Zeroize, Zeroizing};

use crate::bridge::ffi::{BytesOutput, OperationOutput, Status};

const MIB: usize = 1_048_576;
const MAX_SECRET_BYTES: usize = MIB;
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
    recovery_batches: Vec<Zeroizing<Vec<u8>>>,
    recovery_packets: Vec<SharePacket>,
    recovered: Zeroizing<Vec<u8>>,
    inspection: OperationOutput,
}

pub fn new_operation() -> Box<Operation> {
    PANIC_HOOK.call_once(|| std::panic::set_hook(Box::new(|_| {})));
    Box::new(Operation {
        created_packets: Vec::new(),
        recovery_batches: Vec::new(),
        recovery_packets: Vec::new(),
        recovered: Zeroizing::new(Vec::new()),
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
        match catch_unwind(AssertUnwindSafe(|| {
            self.create_words_inner(generation, secret, threshold, share_count)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    fn create_words_inner(
        &mut self,
        generation: u64,
        secret: &[u8],
        threshold: u8,
        share_count: u8,
    ) -> OperationOutput {
        if secret.is_empty() {
            return output(generation, Status::EmptySecret);
        }
        if str::from_utf8(secret).is_err() {
            return output(generation, Status::InvalidUtf8);
        }
        if secret.len() > MAX_SECRET_BYTES {
            return output(generation, Status::SecretTooLarge);
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

        let packets = match split_secret(secret, threshold, share_count, None) {
            Ok(packets) => packets,
            Err(error) => return output(generation, status_from_core(&error)),
        };
        clear_packets(&mut self.created_packets);
        self.created_packets = packets;
        let mut result = output(generation, Status::Ok);
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
        let encoded = match encode_packet(packet, Encoding::MnemoWords) {
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
        match catch_unwind(AssertUnwindSafe(|| {
            self.add_recovery_words_inner(generation, input)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    fn add_recovery_words_inner(&mut self, generation: u64, input: &[u8]) -> OperationOutput {
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
        if !self.recovery_batch_fits_memory(input.len(), token_count) {
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
        self.inspect_recovery(generation)
    }

    pub fn remove_recovery_batch(&mut self, generation: u64, batch_index: u16) -> OperationOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            if usize::from(batch_index) >= self.recovery_batches.len() {
                return self.with_batch_count(output(generation, Status::InvalidIndex));
            }
            self.recovery_batches.remove(usize::from(batch_index));
            self.inspect_recovery(generation)
        })) {
            Ok(result) => result,
            Err(_) => self.panic_output(generation),
        }
    }

    fn inspect_recovery(&mut self, generation: u64) -> OperationOutput {
        self.recovered.zeroize();
        self.recovered.clear();
        clear_packets(&mut self.recovery_packets);
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
            let mut decoded = match parse_mnemo_words_packets_with_versions(text) {
                Ok(decoded) => decoded,
                Err(_) => {
                    clear_packets(&mut packets);
                    self.inspection =
                        self.with_batch_count(output(generation, Status::MalformedInput));
                    return clone_output(&self.inspection);
                }
            };
            if decoded.iter().any(|item| {
                item.version != PacketVersion::V2 || item.packet.crypto_params.is_some()
            }) {
                for item in &mut decoded {
                    item.packet.payload.zeroize();
                }
                clear_packets(&mut packets);
                self.inspection =
                    self.with_batch_count(output(generation, Status::UnsupportedInput));
                return clone_output(&self.inspection);
            }
            packets.extend(decoded.into_iter().map(|item| item.packet));
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
        let ready = inspected.supplied_count >= usize::from(inspected.threshold);
        let mut result = output(
            generation,
            if ready {
                Status::Ok
            } else {
                Status::NotEnoughShares
            },
        );
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
        match catch_unwind(AssertUnwindSafe(|| self.recover_words_inner(generation))) {
            Ok(result) => result,
            Err(_) => self.panic_bytes(generation),
        }
    }

    fn recover_words_inner(&mut self, generation: u64) -> BytesOutput {
        if !self.inspection.ready || self.inspection.status != Status::Ok {
            return bytes_output(generation, self.inspection.status);
        }
        let recovered = match combine_shares(&self.recovery_packets, None) {
            Ok(recovered) => Zeroizing::new(recovered),
            Err(error) => return bytes_output(generation, status_from_core(&error)),
        };
        if recovered.len() > MAX_SECRET_BYTES {
            return bytes_output(generation, Status::SecretTooLarge);
        }
        let expected_len = match inspect_share_set(&self.recovery_packets) {
            Ok(inspected) => inspected.expected_secret_len,
            Err(error) => return bytes_output(generation, status_from_core(&error)),
        };
        if recovered.len() != expected_len {
            return bytes_output(generation, Status::IntegrityFailure);
        }
        if str::from_utf8(&recovered).is_err() {
            return bytes_output(generation, Status::InvalidUtf8);
        }

        self.recovered.zeroize();
        self.recovered = recovered;
        BytesOutput {
            generation,
            status: Status::Ok,
            bytes: self.recovered.to_vec(),
        }
    }

    pub fn recovered_text(&mut self, generation: u64) -> BytesOutput {
        match catch_unwind(AssertUnwindSafe(|| {
            if self.recovered.is_empty() {
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

    fn recovery_batch_fits_memory(&self, input_len: usize, token_count: usize) -> bool {
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
        let Some(raw_after) = raw_payload_after
            .and_then(|payload| raw_headers_after.and_then(|headers| payload.checked_add(headers)))
        else {
            return false;
        };
        let Some(created_state) =
            packet_storage_bytes(&self.created_packets, self.created_packets.capacity())
        else {
            return false;
        };
        let decoded_state = MAX_ACCEPTED_SHARES
            .checked_mul(PACKET_WIRE_OVERHEAD)
            .and_then(|overhead| MAX_LOGICAL_VOLUME.checked_add(overhead))
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
        let workspace = input_len
            .checked_mul(3)
            .and_then(|bytes| {
                largest_batch
                    .checked_mul(2)
                    .and_then(|parser_copies| bytes.checked_add(parser_copies))
            })
            .and_then(|bytes| parser_refs.and_then(|refs| bytes.checked_add(refs)))
            .and_then(|bytes| decoded_scratch.and_then(|scratch| bytes.checked_add(scratch)))
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

    fn clear_all(&mut self) {
        clear_packets(&mut self.created_packets);
        self.recovery_batches.clear();
        clear_packets(&mut self.recovery_packets);
        self.recovered.zeroize();
        self.recovered.clear();
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
        CoreError::PassphraseRequired
        | CoreError::UnsupportedCryptoParams { .. }
        | CoreError::UnsupportedPacketFlags { .. } => Status::UnsupportedInput,
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
