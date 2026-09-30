//! Platform-independent input engine and stable C boundary.
pub mod config;
pub mod core;
pub mod extensions;
#[cfg(feature = "rime")]
mod ffi;
pub mod model;
#[cfg(feature = "rime")]
mod rime;
