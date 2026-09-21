#[cxx::bridge]
pub mod ffi {
    #[repr(u8)]
    enum Status {
        Ok = 0,
        EmptySecret = 1,
        InvalidThreshold = 2,
        SecretTooLarge = 3,
        LogicalVolumeTooLarge = 4,
        PasteTooLarge = 5,
        RetainedInputTooLarge = 6,
        TokenLimit = 7,
        TooManyShares = 8,
        UnsupportedInput = 9,
        MalformedInput = 10,
        DuplicateShare = 11,
        MixedShareSet = 12,
        NotEnoughShares = 13,
        InvalidUtf8 = 14,
        EncodedShareTooLarge = 15,
        IntegrityFailure = 16,
        ResourceLimit = 17,
        CoreError = 18,
        InternalPanic = 19,
        InvalidIndex = 20,
        PassphraseRequired = 21,
        UnsupportedParameters = 22,
        MixedEncoding = 23,
        MixedVersion = 24,
    }

    #[repr(u8)]
    enum ShareEncoding {
        Auto = 0,
        Base64url = 1,
        Base58check = 2,
        MnemoWords = 3,
        MnemoBip39 = 4,
    }

    struct OperationOutput {
        generation: u64,
        status: Status,
        threshold: u8,
        share_count: u16,
        supplied_count: u16,
        recovery_batch_count: u16,
        encoding: ShareEncoding,
        passphrase_protected: bool,
        ready: bool,
    }

    struct BytesOutput {
        generation: u64,
        status: Status,
        bytes: Vec<u8>,
    }

    extern "Rust" {
        type Operation;

        fn new_operation() -> Box<Operation>;
        fn destroy_operation(operation: Box<Operation>);
        fn reset(self: &mut Operation, generation: u64) -> OperationOutput;
        fn create(
            self: &mut Operation,
            generation: u64,
            secret: &[u8],
            threshold: u8,
            share_count: u8,
            encoding: ShareEncoding,
        ) -> OperationOutput;
        fn encode_share(self: &mut Operation, generation: u64, share_index: u16) -> BytesOutput;
        fn add_recovery(
            self: &mut Operation,
            generation: u64,
            input: &[u8],
            encoding: ShareEncoding,
        ) -> OperationOutput;
        fn remove_recovery_batch(
            self: &mut Operation,
            generation: u64,
            batch_index: u16,
        ) -> OperationOutput;
        fn recover(self: &mut Operation, generation: u64) -> BytesOutput;
        fn recovered_text(self: &mut Operation, generation: u64) -> BytesOutput;
    }
}

pub use crate::operation::{Operation, destroy_operation, new_operation};
