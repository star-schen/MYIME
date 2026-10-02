//! Offline product configuration editing. Never used on the input hot path.
//! Revisions guard optimistic updates; a kernel lock serializes MYIME writers.
//! External editors do not honor that lock, so revision is checked again just
//! before publication. Windows does not expose a general compare-and-swap for
//! arbitrary files: callers must not edit the same file during the final rename.
use crate::config::Config;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value as Json};
use sha2::{Digest, Sha256};
use std::{
    fmt,
    fs::{self, File, OpenOptions},
    io::{Read, Write},
    path::{Path, PathBuf},
    sync::atomic::{AtomicU64, Ordering},
    time::{SystemTime, UNIX_EPOCH},
};
use toml_edit::{DocumentMut, Item, Table, TableLike, Value};

pub const MAX_CONFIG_BYTES: usize = 1024 * 1024;
static NEXT_FILE: AtomicU64 = AtomicU64::new(0);

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Eq)]
#[serde(deny_unknown_fields)]
pub struct FileRevision {
    pub exists: bool,
    pub modified_unix_ms: String,
    pub hash: String,
}
#[derive(Clone, Debug, Serialize)]
pub struct Snapshot {
    pub revision: FileRevision,
    pub text: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub backup_path: Option<String>,
}
#[derive(Clone, Debug)]
pub struct BridgeError {
    pub kind: String,
    pub message: String,
}
impl BridgeError {
    pub fn new(kind: &str, message: impl Into<String>) -> Self {
        Self {
            kind: kind.into(),
            message: message.into(),
        }
    }
    pub fn exit_code(&self) -> i32 {
        match self.kind.as_str() {
            "conflict" | "busy" => 3,
            "io" | "not_found" => 4,
            _ => 2,
        }
    }
}
impl fmt::Display for BridgeError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.message)
    }
}
impl std::error::Error for BridgeError {}
impl From<std::io::Error> for BridgeError {
    fn from(e: std::io::Error) -> Self {
        Self::new("io", e.to_string())
    }
}
fn invalid(message: impl Into<String>) -> BridgeError {
    BridgeError::new("invalid", message)
}
fn conflict() -> BridgeError {
    BridgeError::new(
        "conflict",
        "文件已被其他编辑修改；请重新读取并比较，不会覆盖新内容。",
    )
}

pub fn sha256(bytes: &[u8]) -> String {
    format!("{:x}", Sha256::digest(bytes))
}
fn stamp(metadata: &fs::Metadata) -> Result<String, BridgeError> {
    let time = metadata
        .modified()?
        .duration_since(UNIX_EPOCH)
        .map_err(|_| invalid("File timestamp is before Unix epoch"))?;
    Ok(time.as_millis().to_string())
}
pub fn read_snapshot(path: &Path, limit: usize) -> Result<Snapshot, BridgeError> {
    let mut file = match File::open(path) {
        Ok(file) => file,
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            return Ok(Snapshot {
                revision: FileRevision {
                    exists: false,
                    modified_unix_ms: "0".into(),
                    hash: sha256(&[]),
                },
                text: String::new(),
                backup_path: None,
            })
        }
        Err(e) => return Err(e.into()),
    };
    let before = file.metadata()?;
    if !before.is_file() {
        return Err(invalid("Path must identify a regular file"));
    }
    if before.len() > limit as u64 {
        return Err(BridgeError::new(
            "too_large",
            format!("File exceeds {limit} bytes"),
        ));
    }
    let mut bytes = Vec::new();
    Read::by_ref(&mut file)
        .take(limit as u64 + 1)
        .read_to_end(&mut bytes)?;
    if bytes.len() > limit {
        return Err(BridgeError::new(
            "too_large",
            format!("File exceeds {limit} bytes"),
        ));
    }
    let after = file.metadata()?;
    let current = fs::metadata(path)?;
    if before.len() != after.len()
        || before.modified()? != after.modified()?
        || after.len() != current.len()
        || after.modified()? != current.modified()?
    {
        return Err(conflict());
    }
    let hash = sha256(&bytes);
    let text = String::from_utf8(bytes)
        .map_err(|_| invalid("File must be UTF-8 (UTF-8 BOM is accepted)"))?;
    Ok(Snapshot {
        revision: FileRevision {
            exists: true,
            modified_unix_ms: stamp(&after)?,
            hash,
        },
        text,
        backup_path: None,
    })
}
fn unique_path(parent: &Path, name: &str, suffix: &str) -> PathBuf {
    let now = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_nanos();
    parent.join(format!(
        ".{name}.myime-{now}-{}-{}.{suffix}",
        std::process::id(),
        NEXT_FILE.fetch_add(1, Ordering::Relaxed)
    ))
}
struct TempFile(PathBuf);
impl Drop for TempFile {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.0);
    }
}

/// Validate text before calling this primitive. Temp and backup stay beside the
/// target, preserving same-volume atomic replacement and independent backups.
/// Missing targets are created without replacing a concurrent newly-created file.
pub fn save_text(
    path: &Path,
    expected: &FileRevision,
    text: &str,
    limit: usize,
) -> Result<Snapshot, BridgeError> {
    if text.len() > limit {
        return Err(BridgeError::new(
            "too_large",
            format!("Text exceeds {limit} bytes"),
        ));
    }
    if expected.hash.len() != 64
        || !expected.hash.bytes().all(|b| b.is_ascii_hexdigit())
        || expected.modified_unix_ms.parse::<u128>().is_err()
    {
        return Err(invalid("Invalid expected revision"));
    }
    let name = path
        .file_name()
        .and_then(|s| s.to_str())
        .filter(|s| !s.is_empty())
        .ok_or_else(|| invalid("Path needs a UTF-8 file name"))?;
    let parent = path
        .parent()
        .filter(|p| !p.as_os_str().is_empty())
        .unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(parent)?;
    let parent = fs::canonicalize(parent)?;
    let path = parent.join(name);
    if fs::symlink_metadata(&path).is_ok_and(|m| m.file_type().is_symlink()) {
        return Err(invalid("Will not replace a symbolic link"));
    }
    // Persistent sidecar; kernel lock is automatically released after a crash.
    // Do not remove a sidecar while other processes may be waiting on its inode.
    let lock_path = parent.join(format!(".{name}.myime-write.lock"));
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(lock_path)?;
    lock.try_lock().map_err(|e| {
        BridgeError::new(
            "busy",
            format!("Another MYIME writer is saving this file: {e}"),
        )
    })?;
    let current = read_snapshot(&path, limit)?;
    if &current.revision != expected {
        return Err(conflict());
    }
    if current.revision.exists && current.text == text {
        return Ok(current);
    }
    let temp = TempFile(unique_path(&parent, name, "tmp"));
    let mut replacement = OpenOptions::new()
        .write(true)
        .create_new(true)
        .open(&temp.0)?;
    replacement.write_all(text.as_bytes())?;
    replacement.sync_all()?;
    drop(replacement);
    let backup = current
        .revision
        .exists
        .then(|| unique_path(&parent, name, "bak"));
    if read_snapshot(&path, limit)?.revision != *expected {
        return Err(conflict());
    }
    atomic_publish(&path, &temp.0, backup.as_deref())?;
    let mut saved = read_snapshot(&path, limit)?;
    // An editor replacing the new file after publication is surfaced, not hidden.
    if saved.text != text {
        return Err(conflict());
    }
    saved.backup_path = backup.map(|p| p.to_string_lossy().into_owned());
    drop(lock);
    Ok(saved)
}
#[cfg(windows)]
fn atomic_publish(
    path: &Path,
    replacement: &Path,
    backup: Option<&Path>,
) -> Result<(), BridgeError> {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Storage::FileSystem::{
        MoveFileExW, ReplaceFileW, MOVEFILE_WRITE_THROUGH,
    };
    fn wide(path: &Path) -> Result<Vec<u16>, BridgeError> {
        let mut result: Vec<_> = path.as_os_str().encode_wide().collect();
        if result.contains(&0) {
            return Err(invalid("Path contains NUL"));
        }
        result.push(0);
        Ok(result)
    }
    let path = wide(path)?;
    let replacement = wide(replacement)?;
    let backup_name = backup.map(wide).transpose()?;
    // System boundary only. Buffers are NUL terminated and live through the calls;
    // APIs never retain pointers. No replace flag when initially creating a file.
    let result = unsafe {
        match backup_name.as_ref() {
            Some(backup) => ReplaceFileW(
                path.as_ptr(),
                replacement.as_ptr(),
                backup.as_ptr(),
                0,
                std::ptr::null(),
                std::ptr::null(),
            ),
            None => MoveFileExW(replacement.as_ptr(), path.as_ptr(), MOVEFILE_WRITE_THROUGH),
        }
    };
    if result == 0 {
        let failure = std::io::Error::last_os_error();
        // Official ERROR_UNABLE_TO_MOVE_REPLACEMENT_2 means the original has
        // moved to our backup but the replacement could not take its name.
        // Restore only if the target is still absent: never replace an external
        // file that appeared meanwhile. On failure the original backup is kept.
        if failure.raw_os_error() == Some(1177) {
            if let Some(backup_name) = backup_name.as_ref() {
                let restored = unsafe {
                    MoveFileExW(backup_name.as_ptr(), path.as_ptr(), MOVEFILE_WRITE_THROUGH)
                };
                if restored != 0 {
                    return Err(BridgeError::new(
                        "io",
                        format!("Replacement failed ({failure}); the original file was restored"),
                    ));
                }
                let recovery_failure = std::io::Error::last_os_error();
                return Err(BridgeError::new("io", format!("Replacement failed ({failure}); recovery failed ({recovery_failure}); original content is retained at {}", backup.unwrap().display())));
            }
        }
        return Err(failure.into());
    }
    Ok(())
}
#[cfg(not(windows))]
fn atomic_publish(
    path: &Path,
    replacement: &Path,
    backup: Option<&Path>,
) -> Result<(), BridgeError> {
    if let Some(backup) = backup {
        let mut file = OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(backup)?;
        std::io::copy(&mut File::open(path)?, &mut file)?;
        file.sync_all()?;
        fs::rename(replacement, path)?;
    } else {
        // Same-directory hard link has atomic create-if-absent semantics.
        fs::hard_link(replacement, path)?;
    }
    File::open(path.parent().unwrap())?.sync_all()?;
    Ok(())
}

pub fn validate_product_text(text: &str) -> Result<(), BridgeError> {
    if text.len() > MAX_CONFIG_BYTES {
        return Err(BridgeError::new(
            "too_large",
            "Product config exceeds 1 MiB",
        ));
    }
    Config::parse(text.trim_start_matches('\u{feff}'))
        .map(|_| ())
        .map_err(invalid)
}
/// Source-level replaceable bridge backed by the same production file writer.
/// Native YAML uses its own validator; this implementation validates product TOML.
pub struct ProductConfigBridge;
impl crate::extensions::ConfigBridge for ProductConfigBridge {
    fn apply_patch(
        &self,
        change: crate::extensions::ConfigChange,
    ) -> Result<crate::extensions::BridgeResult, String> {
        validate_product_text(&change.proposed_patch).map_err(|e| e.to_string())?;
        let actual = read_snapshot(&change.path, MAX_CONFIG_BYTES).map_err(|e| e.to_string())?;
        if actual.revision != change.expected {
            return Ok(crate::extensions::BridgeResult::ExternalModification {
                actual: actual.revision,
            });
        }
        match save_text(
            &change.path,
            &change.expected,
            &change.proposed_patch,
            MAX_CONFIG_BYTES,
        ) {
            Ok(_) => Ok(crate::extensions::BridgeResult::Applied),
            Err(e) if e.kind == "conflict" => {
                Ok(crate::extensions::BridgeResult::ExternalModification {
                    actual: read_snapshot(&change.path, MAX_CONFIG_BYTES)
                        .map_err(|e| e.to_string())?
                        .revision,
                })
            }
            Err(e) => Err(e.to_string()),
        }
    }
}
fn ensure_table<'a>(
    parent: &'a mut dyn TableLike,
    key: &str,
) -> Result<&'a mut dyn TableLike, BridgeError> {
    if parent.get(key).is_none() {
        parent.insert(key, Item::Table(Table::new()));
    }
    parent
        .get_mut(key)
        .and_then(Item::as_table_like_mut)
        .ok_or_else(|| invalid(format!("{key} must be a table")))
}
fn apply_changes(
    layer: &mut dyn TableLike,
    changes: &Map<String, Json>,
) -> Result<(), BridgeError> {
    for (key, value) in changes {
        if !matches!(
            key.as_str(),
            "enabled" | "schema" | "theme" | "ascii_mode" | "full_shape" | "ascii_punct"
        ) {
            return Err(invalid(format!("Unknown editable setting: {key}")));
        }
        if !value.is_null() {
            if matches!(key.as_str(), "schema" | "theme") {
                let s = value
                    .as_str()
                    .ok_or_else(|| invalid(format!("{key} must be a string or null")))?;
                if s.is_empty() || s.len() > 128 || s.chars().any(char::is_control) {
                    return Err(invalid(format!("Invalid {key}")));
                }
                if key == "theme" && !crate::theme::valid_id(s) {
                    return Err(invalid("theme must be an ID, not a path"));
                }
            } else if !value.is_boolean() {
                return Err(invalid(format!("{key} must be boolean or null")));
            }
        }
        let (parent, field): (&mut dyn TableLike, &str) = match key.as_str() {
            "theme" => {
                if value.is_null() && layer.get("ui").is_none() {
                    continue;
                }
                (ensure_table(layer, "ui")?, "theme")
            }
            "ascii_mode" | "full_shape" | "ascii_punct" => {
                if value.is_null() && layer.get("options").is_none() {
                    continue;
                }
                (ensure_table(layer, "options")?, key)
            }
            _ => (&mut *layer, key),
        };
        if value.is_null() {
            parent.remove(field);
        } else {
            let item = match value {
                Json::Bool(b) => Item::Value(Value::from(*b)),
                Json::String(s) => Item::Value(Value::from(s.as_str())),
                _ => unreachable!(),
            };
            // Preserve the edited value's own surrounding comments/whitespace.
            let mut item = item;
            if let (Some(old), Some(new)) = (
                parent.get(field).and_then(Item::as_value),
                item.as_value_mut(),
            ) {
                *new.decor_mut() = old.decor().clone();
            }
            parent.insert(field, item);
        }
    }
    Ok(())
}
fn profile_layer<'a>(
    doc: &'a mut DocumentMut,
    executable: &str,
) -> Result<&'a mut dyn TableLike, BridgeError> {
    if executable.is_empty()
        || executable.len() > 260
        || executable.contains(['/', '\\'])
        || executable.chars().any(char::is_control)
    {
        return Err(invalid("Executable must be a basename"));
    }
    if doc.get("profiles").is_none() {
        doc.insert(
            "profiles",
            Item::ArrayOfTables(toml_edit::ArrayOfTables::new()),
        );
    }
    let profiles = doc.get_mut("profiles").unwrap();
    match profiles {
        Item::ArrayOfTables(array) => {
            let index = array.iter().position(|p| {
                p.get("executable")
                    .and_then(Item::as_str)
                    .is_some_and(|e| e.eq_ignore_ascii_case(executable))
            });
            let index = match index {
                Some(index) => index,
                None => {
                    let mut table = Table::new();
                    table.insert("executable", toml_edit::value(executable));
                    array.push(table);
                    array.len() - 1
                }
            };
            return ensure_table(array.get_mut(index).unwrap(), "overrides");
        }
        Item::Value(Value::Array(array)) => {
            let index = array.iter().position(|p| {
                p.as_inline_table()
                    .and_then(|p| p.get("executable"))
                    .and_then(Value::as_str)
                    .is_some_and(|e| e.eq_ignore_ascii_case(executable))
            });
            let index = match index {
                Some(index) => index,
                None => {
                    let mut table = toml_edit::InlineTable::new();
                    table.insert("executable", Value::from(executable));
                    array.push(Value::InlineTable(table));
                    array.len() - 1
                }
            };
            ensure_table(
                array
                    .get_mut(index)
                    .and_then(Value::as_inline_table_mut)
                    .ok_or_else(|| invalid("Profile must be a table"))?,
                "overrides",
            )
        }
        _ => Err(invalid("profiles must be an array of tables")),
    }
}
pub fn update_product_text(
    text: &str,
    scope: &str,
    executable: Option<&str>,
    changes: &Map<String, Json>,
) -> Result<String, BridgeError> {
    validate_product_text(text)?;
    if !matches!(scope, "default" | "windows" | "profile") {
        return Err(invalid("scope must be default, windows or profile"));
    }
    if scope == "profile" {
        let exe = executable.ok_or_else(|| invalid("Profile needs executable"))?;
        if exe.is_empty()
            || exe.len() > 260
            || exe.contains(['/', '\\'])
            || exe.chars().any(char::is_control)
        {
            return Err(invalid("Executable must be a basename"));
        }
    }
    if changes.is_empty() {
        return Ok(text.to_owned());
    }
    let bom = text.starts_with('\u{feff}');
    let mut document = text
        .trim_start_matches('\u{feff}')
        .parse::<DocumentMut>()
        .map_err(|e| invalid(e.to_string()))?;
    let layer = match scope {
        "default" => ensure_table(document.as_table_mut(), "default")?,
        "windows" => ensure_table(
            ensure_table(document.as_table_mut(), "platform")?,
            "windows",
        )?,
        "profile" => profile_layer(
            &mut document,
            executable.ok_or_else(|| invalid("Profile needs executable"))?,
        )?,
        _ => return Err(invalid("scope must be default, windows or profile")),
    };
    apply_changes(layer, changes)?;
    let output = format!("{}{}", if bom { "\u{feff}" } else { "" }, document);
    validate_product_text(&output)?;
    Ok(output)
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Fixture(PathBuf);
    impl Fixture {
        fn new() -> Self {
            let root = std::env::temp_dir().join(format!(
                "myime-config-test-{}-{}",
                std::process::id(),
                NEXT_FILE.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&root).unwrap();
            Self(root)
        }
        fn path(&self) -> PathBuf {
            self.0.join("config.toml")
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = fs::remove_dir_all(&self.0);
        }
    }
    #[test]
    fn preserves_comments_unknown_values_and_inheritance() {
        let source = "# MY settings\n[default]\nschema = 'pinyin_simp' # keep this\nfuture = ['mine']\n[default.options]\nascii_mode = false # mode\n[default.ui]\ntheme = 'default'\n[[profiles]]\nexecutable = 'Editor.exe'\n[profiles.overrides]\nenabled = false\nunknown = 7\n";
        let changes = serde_json::json!({"theme":"dark","ascii_mode":true});
        let out =
            update_product_text(source, "default", None, changes.as_object().unwrap()).unwrap();
        assert!(
            out.contains("# MY settings")
                && out.contains("# keep this")
                && out.contains("# mode")
                && out.contains("future = ['mine']")
        );
        let out = update_product_text(
            &out,
            "profile",
            Some("EDITOR.EXE"),
            serde_json::json!({"enabled":null,"theme":"light"})
                .as_object()
                .unwrap(),
        )
        .unwrap();
        assert!(out.contains("unknown = 7"));
        let effective = Config::parse(&out)
            .unwrap()
            .effective("editor.exe", &toml::Table::new())
            .unwrap();
        assert!(effective.enabled && effective.options["ascii_mode"]);
        assert_eq!(effective.theme_id(), "light");
        assert_eq!(out.matches("executable").count(), 1);
    }
    #[test]
    fn validates_new_profiles_and_edit_types() {
        assert!(update_product_text("", "profile", Some("../evil.exe"), &Map::new()).is_err());
        assert!(update_product_text(
            "",
            "default",
            None,
            serde_json::json!({"theme":"../x"}).as_object().unwrap()
        )
        .is_err());
        assert!(update_product_text(
            "",
            "default",
            None,
            serde_json::json!({"enabled":"yes"}).as_object().unwrap()
        )
        .is_err());
        assert!(update_product_text(
            "",
            "default",
            None,
            serde_json::json!({"unknown":2}).as_object().unwrap()
        )
        .is_err());
        let out = update_product_text(
            "",
            "profile",
            Some("Game.exe"),
            serde_json::json!({"ascii_punct":true}).as_object().unwrap(),
        )
        .unwrap();
        assert!(
            Config::parse(&out)
                .unwrap()
                .effective("game.exe", &toml::Table::new())
                .unwrap()
                .options["ascii_punct"]
        );
    }
    #[test]
    fn inline_tables_and_profiles_remain_inline_and_keep_unknown_fields() {
        let source = "default = { enabled = true, future = 'keep' }\nprofiles = [{ executable = 'Editor.exe', overrides = { unknown = 7 } }] # keep list\n";
        let updated = update_product_text(
            source,
            "default",
            None,
            serde_json::json!({"theme":"dark","ascii_mode":true})
                .as_object()
                .unwrap(),
        )
        .unwrap();
        let updated = update_product_text(
            &updated,
            "profile",
            Some("editor.exe"),
            serde_json::json!({"theme":"light","full_shape":true})
                .as_object()
                .unwrap(),
        )
        .unwrap();
        assert!(
            updated.contains("# keep list")
                && updated.contains("unknown = 7")
                && updated.contains("future = 'keep'")
        );
        assert!(!updated.contains("[[profiles]]"));
        let effective = Config::parse(&updated)
            .unwrap()
            .effective("EDITOR.EXE", &toml::Table::new())
            .unwrap();
        assert_eq!(effective.theme_id(), "light");
        assert!(effective.options["ascii_mode"] && effective.options["full_shape"]);
        assert_eq!(
            update_product_text(source, "profile", Some("new.exe"), &Map::new()).unwrap(),
            source
        );
    }
    #[test]
    fn atomic_save_backups_and_detects_stale_editor() {
        let fixture = Fixture::new();
        let path = fixture.path();
        let missing = read_snapshot(&path, 1024).unwrap();
        assert!(!missing.revision.exists);
        let first = save_text(&path, &missing.revision, "first", 1024).unwrap();
        let second = save_text(&path, &first.revision, "second", 1024).unwrap();
        assert_eq!(
            fs::read_to_string(second.backup_path.unwrap()).unwrap(),
            "first"
        );
        assert_eq!(
            save_text(&path, &first.revision, "stale", 1024)
                .unwrap_err()
                .kind,
            "conflict"
        );
        assert_eq!(fs::read_to_string(&path).unwrap(), "second");
        fs::write(&path, "third!").unwrap();
        assert_eq!(
            save_text(&path, &second.revision, "lost edit", 1024)
                .unwrap_err()
                .kind,
            "conflict"
        );
        assert_eq!(fs::read_to_string(&path).unwrap(), "third!");
    }
    #[test]
    fn hash_and_mtime_both_guard_revision() {
        let fixture = Fixture::new();
        let path = fixture.path();
        fs::write(&path, "old").unwrap();
        let original = read_snapshot(&path, 100).unwrap();
        let metadata = fs::metadata(&path).unwrap();
        fs::write(&path, "new").unwrap();
        File::options()
            .write(true)
            .open(&path)
            .unwrap()
            .set_modified(metadata.modified().unwrap())
            .unwrap();
        assert_eq!(
            read_snapshot(&path, 100).unwrap().revision.modified_unix_ms,
            original.revision.modified_unix_ms
        );
        assert_eq!(
            save_text(&path, &original.revision, "lost", 100)
                .unwrap_err()
                .kind,
            "conflict"
        );
        fs::write(&path, "old").unwrap();
        File::options()
            .write(true)
            .open(&path)
            .unwrap()
            .set_modified(metadata.modified().unwrap() + std::time::Duration::from_secs(5))
            .unwrap();
        assert_eq!(
            read_snapshot(&path, 100).unwrap().revision.hash,
            original.revision.hash
        );
        assert_eq!(
            save_text(&path, &original.revision, "lost", 100)
                .unwrap_err()
                .kind,
            "conflict"
        );
    }
    #[test]
    fn size_guard_and_kernel_lock_do_not_modify_target() {
        let fixture = Fixture::new();
        let path = fixture.path();
        fs::write(&path, "original").unwrap();
        assert_eq!(read_snapshot(&path, 3).unwrap_err().kind, "too_large");
        let snapshot = read_snapshot(&path, 100).unwrap();
        let lock = File::options()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(fixture.0.join(".config.toml.myime-write.lock"))
            .unwrap();
        lock.try_lock().unwrap();
        assert_eq!(
            save_text(&path, &snapshot.revision, "new", 100)
                .unwrap_err()
                .kind,
            "busy"
        );
        drop(lock);
        assert_eq!(fs::read_to_string(&path).unwrap(), "original");
        save_text(&path, &snapshot.revision, "new", 100).unwrap();
    }
    #[test]
    fn source_level_bridge_uses_the_same_revision_writer() {
        use crate::extensions::{BridgeResult, ConfigBridge, ConfigChange};
        let fixture = Fixture::new();
        let path = fixture.path();
        let missing = read_snapshot(&path, MAX_CONFIG_BYTES).unwrap();
        let bridge: Box<dyn ConfigBridge> = Box::new(ProductConfigBridge);
        assert!(matches!(
            bridge
                .apply_patch(ConfigChange {
                    path: path.clone(),
                    expected: missing.revision.clone(),
                    proposed_patch: "[default.ui]\ntheme='dark'".into()
                })
                .unwrap(),
            BridgeResult::Applied
        ));
        assert!(matches!(
            bridge
                .apply_patch(ConfigChange {
                    path: path.clone(),
                    expected: missing.revision,
                    proposed_patch: String::new()
                })
                .unwrap(),
            BridgeResult::ExternalModification { .. }
        ));
        assert!(fs::read_to_string(path).unwrap().contains("dark"));
    }
}
