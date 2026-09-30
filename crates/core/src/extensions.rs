//! Source-level contracts; InputProvider is used by Core in production.
//! No stable binary plugin ABI, plugin loader, worker processes or IPC.
use crate::{config::EffectiveConfig, model::InputState};
use std::{collections::BTreeMap, path::PathBuf};
pub struct ImportedWord {
    pub word: String,
    pub code: Option<String>,
    pub frequency: Option<u64>,
    pub source: String,
}
pub trait Importer {
    fn import(&self, input: &[u8]) -> Result<Vec<ImportedWord>, String>;
}
pub trait Converter {
    fn convert(&self, words: Vec<ImportedWord>) -> Result<Vec<ImportedWord>, String>;
}
pub struct DictionaryPackage {
    pub id: String,
    pub version: String,
    pub files: Vec<PathBuf>,
    pub metadata: BTreeMap<String, String>,
}
pub trait DictionaryProvider {
    fn packages(&self) -> Result<Vec<DictionaryPackage>, String>;
}
pub struct KeyEvent {
    pub keysym: i32,
    pub modifiers: i32,
}
/// A native key may already be consumed when its subsequent snapshot fails.
/// Preserve this fact so the Host does not forward that key a second time.
#[derive(Debug)]
pub struct ProcessError {
    pub message: String,
    pub eaten: bool,
}
impl From<String> for ProcessError {
    fn from(message: String) -> Self { Self { message, eaten: false } }
}
/// Thread-affine input session. The provider owns the sole current snapshot.
/// Successful mutations refresh it; state() itself never consumes a commit.
/// Keep commit text until ack_commit (successful document write) or clear
/// (explicit cancellation). select takes a CURRENT-PAGE index.
/// Drop must release its resources without panicking or invoking Core callbacks.
pub trait InputProvider {
    fn process(&mut self, key: KeyEvent) -> Result<bool, ProcessError>;
    fn state(&self) -> &InputState;
    fn select(&mut self, page_index: usize) -> Result<(), String>;
    fn clear(&mut self) -> Result<(), String>;
    fn ack_commit(&mut self) -> Result<(), String>;
}
/// Creates a configured, idle provider with its initial state already read.
/// Failures must release provisional resources without disturbing live providers.
/// Factories run only at creation/profile replacement, never on each key.
pub trait InputProviderFactory {
    fn create(&self, config: &EffectiveConfig) -> Result<Box<dyn InputProvider>, String>;
}
pub struct SyncObject {
    pub path: String,
    pub bytes: Vec<u8>,
    pub etag: Option<String>,
}
pub enum SyncWrite {
    Written { etag: Option<String> },
    Conflict,
}
/// Transport Rime sync exports, never live LevelDB userdb files.
pub trait SyncProvider {
    fn fetch(&self, path: &str) -> Result<Option<SyncObject>, String>;
    fn put(&self, object: &SyncObject, if_match: Option<&str>) -> Result<SyncWrite, String>;
}
pub struct FileRevision {
    pub modified_unix_ms: u128,
    pub hash: String,
}
pub struct ConfigChange {
    pub path: PathBuf,
    pub expected: FileRevision,
    pub proposed_patch: String,
}
pub enum BridgeResult {
    Applied,
    ExternalModification { actual: FileRevision },
}
/// Preserve unknown native YAML, compare revisions, deploy outside the hot path.
pub trait ConfigBridge {
    fn apply_patch(&self, change: ConfigChange) -> Result<BridgeResult, String>;
}
pub trait CompatibilityProvider {
    fn overrides(&self, executable: &str) -> toml::Table;
}
