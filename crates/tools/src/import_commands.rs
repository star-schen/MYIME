//! Offline import commands. Input files and fresh packages only; never Rime DBs.
use myime_core::{
    config_bridge::{self, BridgeError, FileRevision},
    importer::{self, Format},
};
use serde_json::Value;
use std::path::{Path, PathBuf};

fn field<'a>(request: &'a Value, key: &str) -> Result<&'a str, BridgeError> {
    request
        .get(key)
        .and_then(Value::as_str)
        .filter(|value| !value.contains('\0'))
        .ok_or_else(|| BridgeError::new("invalid", format!("{key} must be a string without NUL")))
}
fn path(request: &Value, key: &str) -> Result<PathBuf, BridgeError> {
    let value = PathBuf::from(field(request, key)?);
    if !value.is_absolute() {
        return Err(BridgeError::new(
            "invalid",
            format!("{key} must be absolute"),
        ));
    }
    Ok(value)
}
fn import_error(error: importer::ImportError) -> BridgeError {
    BridgeError::new(&error.kind, error.to_string())
}
fn source<'a>(request: &'a Value, input: &'a Path) -> Result<&'a str, BridgeError> {
    if request.get("source").is_some() {
        field(request, "source")
    } else {
        Ok(input
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("import"))
    }
}

pub fn dispatch(request: &Value) -> Result<Value, BridgeError> {
    let command = field(request, "command")?;
    if command == "dictionary.combine" {
        let combined =
            importer::combine_dictionaries(&path(request, "shared_dir")?).map_err(import_error)?;
        return serde_json::to_value(combined)
            .map_err(|error| BridgeError::new("json", error.to_string()));
    }
    if command == "package.validate" {
        let validated =
            importer::validate_package(&path(request, "path")?).map_err(import_error)?;
        return serde_json::to_value(validated)
            .map_err(|error| BridgeError::new("json", error.to_string()));
    }
    if !matches!(command, "import.preview" | "import.write") {
        return Err(BridgeError::new("invalid", "Unknown import command"));
    }
    let input = path(request, "input_path")?;
    let format = Format::parse(field(request, "format")?).map_err(import_error)?;
    let snapshot = config_bridge::read_snapshot(&input, importer::MAX_INPUT_BYTES)?;
    if !snapshot.revision.exists {
        return Err(BridgeError::new("not_found", "Import input does not exist"));
    }
    let imported = importer::import(snapshot.text.as_bytes(), format, source(request, &input)?)
        .map_err(import_error)?;
    if command == "import.preview" {
        let mut result = serde_json::to_value(&imported.preview)
            .map_err(|error| BridgeError::new("json", error.to_string()))?;
        result.as_object_mut().unwrap().insert(
            "input_revision".into(),
            serde_json::to_value(&snapshot.revision).unwrap(),
        );
        return Ok(result);
    }
    let expected: FileRevision =
        serde_json::from_value(request.get("input_revision").cloned().ok_or_else(|| {
            BridgeError::new(
                "invalid",
                "Preview the input first; import.write requires input_revision",
            )
        })?)
        .map_err(|error| BridgeError::new("invalid", format!("Invalid input_revision: {error}")))?;
    if expected != snapshot.revision
        || config_bridge::read_snapshot(&input, importer::MAX_INPUT_BYTES)?.revision != expected
    {
        return Err(BridgeError::new(
            "conflict",
            "Import input changed after preview; preview again before writing a package",
        ));
    }
    let root = path(request, "packages_root")?;
    let id = field(request, "id")?;
    let name = field(request, "name")?;
    let version = if request.get("version_name").is_some() {
        field(request, "version_name")?
    } else {
        "1.0.0"
    };
    let written =
        importer::write_package(&root, id, name, version, &imported).map_err(import_error)?;
    let mut result = serde_json::to_value(written)
        .map_err(|error| BridgeError::new("json", error.to_string()))?;
    result.as_object_mut().unwrap().insert(
        "input_revision".into(),
        serde_json::to_value(snapshot.revision).unwrap(),
    );
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::{
        fs,
        sync::atomic::{AtomicU64, Ordering},
        time::{SystemTime, UNIX_EPOCH},
    };
    struct Fixture(PathBuf);
    impl Fixture {
        fn new() -> Self {
            static SERIAL: AtomicU64 = AtomicU64::new(0);
            let stamp = SystemTime::now()
                .duration_since(UNIX_EPOCH)
                .unwrap()
                .as_nanos();
            let root = std::env::temp_dir().join(format!(
                "myime-import-cli-{}-{stamp}-{}",
                std::process::id(),
                SERIAL.fetch_add(1, Ordering::Relaxed)
            ));
            fs::create_dir(&root).unwrap();
            Self(root)
        }
        fn preview(&self) -> Value {
            json!({"command":"import.preview","input_path":self.0.join("words.csv"),"format":"csv"})
        }
    }
    impl Drop for Fixture {
        fn drop(&mut self) {
            let _ = fs::remove_file(self.0.join("words.csv"));
            for filename in [
                "fixture.dict.yaml",
                "myime_fixture.dict.yaml",
                "myime_fixture.schema.yaml",
                "manifest.json",
                "metadata.json",
                "README.md",
            ] {
                let _ = fs::remove_file(self.0.join("packages/fixture").join(filename));
            }
            let _ = fs::remove_dir(self.0.join("packages/fixture"));
            let _ = fs::remove_dir(self.0.join("packages"));
            let _ = fs::remove_dir(&self.0);
        }
    }
    #[test]
    fn preview_then_write_requires_unchanged_input_and_does_not_overwrite() {
        let fixture = Fixture::new();
        fs::write(
            fixture.0.join("words.csv"),
            "word,code,frequency\n你好,ni hao,7\n",
        )
        .unwrap();
        let preview = dispatch(&fixture.preview()).unwrap();
        assert_eq!(preview["summary"]["exportable_rows"], 1);
        let write = json!({"command":"import.write","input_path":fixture.0.join("words.csv"),"format":"csv","input_revision":preview["input_revision"],"packages_root":fixture.0.join("packages"),"id":"fixture","name":"测试"});
        fs::write(
            fixture.0.join("words.csv"),
            "word,code,frequency\n你好,ni hao,8\n",
        )
        .unwrap();
        assert_eq!(dispatch(&write).unwrap_err().kind, "conflict");
        assert!(!fixture.0.join("packages").exists());
        let new_preview = dispatch(&fixture.preview()).unwrap();
        let mut write = write;
        write["input_revision"] = new_preview["input_revision"].clone();
        let result = dispatch(&write).unwrap();
        assert_eq!(result["manifest"]["dictionary"], "fixture.dict.yaml");
        assert_eq!(dispatch(&write).unwrap_err().kind, "exists");
    }
    #[test]
    fn missing_inputs_relative_paths_and_write_without_preview_fail() {
        let fixture = Fixture::new();
        assert_eq!(dispatch(&fixture.preview()).unwrap_err().kind, "not_found");
        assert!(dispatch(
            &json!({"command":"import.preview","input_path":"words.txt","format":"txt"})
        )
        .is_err());
        fs::write(fixture.0.join("words.csv"), "word,code\n你,ni").unwrap();
        let mut request = fixture.preview();
        request["command"] = json!("import.write");
        assert!(dispatch(&request)
            .unwrap_err()
            .message
            .contains("input_revision"));
    }
}
