mod bridge;
mod operation;

pub use bridge::ffi::{BytesOutput, OperationOutput, ShareEncoding, Status};
#[cfg(feature = "capacity-test-hooks")]
pub use operation::CapacityFailpoint;
pub use operation::{Operation, destroy_operation, new_operation};
