//! Extension contracts only; no plugin loader, worker processes or IPC.
use crate::model::InputState;
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
pub trait InputProvider {
    fn process(&mut self, key: KeyEvent) -> Result<bool, String>;
    fn state(&self) -> &InputState;
    fn select(&mut self, page_index: usize) -> Result<(), String>;
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
