//! Platform-independent input engine and stable C boundary.
pub mod config;
pub mod config_bridge;
pub mod core;
pub mod extensions;
pub mod importer;
#[cfg(feature = "rime")]
mod ffi;
pub mod model;
pub mod theme;
#[cfg(feature = "rime")]
mod theme_ffi;
#[cfg(feature = "rime")]
mod rime;
