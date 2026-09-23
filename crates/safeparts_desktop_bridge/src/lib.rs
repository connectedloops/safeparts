mod bridge;
mod operation;

pub use bridge::ffi::{BytesOutput, OperationOutput, RecoveryBatch, ShareEncoding, Status};
pub use operation::{Operation, destroy_operation, new_operation};
