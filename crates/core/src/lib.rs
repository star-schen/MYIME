//! Platform-independent input engine and stable C boundary.
pub mod config;
pub mod config_bridge;
pub mod core;
pub mod extensions;
pub mod importer;
pub mod learning;
#[cfg(feature = "rime")]
mod ffi;
pub mod model;
pub mod theme;
mod catalog_file;
pub mod theme_editor;
pub mod ui_language;
#[cfg(feature = "rime")]
mod ui_language_ffi;
#[cfg(feature = "rime")]
mod theme_ffi;
#[cfg(feature = "rime")]
mod rime;
