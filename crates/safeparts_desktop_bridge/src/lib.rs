mod bridge;
mod operation;

pub use bridge::ffi::{BytesOutput, OperationOutput, ShareEncoding, Status};
pub use operation::{Operation, destroy_operation, new_operation};
