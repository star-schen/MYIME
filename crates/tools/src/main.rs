//! Offline JSON bridge used by settings. No native Rime link or input-path IPC.
use myime_core::{
    config::Config,
    config_bridge::{self, BridgeError, FileRevision, Snapshot, MAX_CONFIG_BYTES},
};
use serde_json::{json, Value};
use std::{
    io::{Read, Write},
    path::Path,
};
mod import_commands;
const MAX_REQUEST_BYTES: usize = 4 * 1024 * 1024;
fn error(kind: &str, message: impl Into<String>) -> BridgeError {
    BridgeError::new(kind, message)
}
fn field<'a>(request: &'a Value, name: &str) -> Result<&'a str, BridgeError> {
    request
        .get(name)
        .and_then(Value::as_str)
        .filter(|s| !s.contains('\0'))
        .ok_or_else(|| error("protocol", format!("{name} must be a string without NUL")))
}
fn expected(request: &Value) -> Result<FileRevision, BridgeError> {
    serde_json::from_value(
        request
            .get("expected")
            .cloned()
            .ok_or_else(|| error("protocol", "expected revision is required"))?,
    )
    .map_err(|e| error("protocol", e.to_string()))
}
fn config_response(snapshot: Snapshot, executable: &str) -> Value {
    let text = snapshot.text.as_str();
    match Config::parse(text) {
        Ok(config) => {
            let effective = config
                .effective(executable, &toml::Table::new())
                .expect("Validated config");
            let document = text.parse::<toml::Table>().unwrap_or_default();
            let profiles = document
                .get("profiles")
                .map(|p| serde_json::to_value(p).unwrap_or(Value::Null))
                .unwrap_or_else(|| json!([]));
            let default = document
                .get("default")
                .map(|p| serde_json::to_value(p).unwrap_or(Value::Null))
                .unwrap_or_else(|| json!({}));
            let windows = document
                .get("platform")
                .and_then(|p| p.get("windows"))
                .map(|p| serde_json::to_value(p).unwrap_or(Value::Null))
                .unwrap_or_else(|| json!({}));
            json!({"revision":snapshot.revision,"text":snapshot.text,"backup_path":snapshot.backup_path,"valid":true,
                "effective":{"schema":effective.schema,"enabled":effective.enabled,"theme":effective.theme_id(),"options":effective.options},"profiles":profiles,"layers":{"default":default,"windows":windows}})
        }
        Err(message) => {
            json!({"revision":snapshot.revision,"text":snapshot.text,"backup_path":snapshot.backup_path,"valid":false,"validation_error":message,"effective":null,"profiles":[],"layers":{"default":{},"windows":{}}})
        }
    }
}
fn yaml_path(request: &Value) -> Result<&Path, BridgeError> {
    let path = Path::new(field(request, "path")?);
    if !path
        .file_name()
        .and_then(|s| s.to_str())
        .is_some_and(|s| s.to_ascii_lowercase().ends_with(".custom.yaml"))
    {
        return Err(error(
            "invalid",
            "YAML bridge only edits *.custom.yaml; schemas and dictionaries remain unchanged",
        ));
    }
    Ok(path)
}
fn validate_yaml(text: &str) -> Result<(), BridgeError> {
    if text.len() > MAX_CONFIG_BYTES {
        return Err(error("too_large", "YAML exceeds 1 MiB"));
    }
    let text = text.trim_start_matches('\u{feff}');
    if text.trim().is_empty() {
        return Ok(());
    }
    let value: serde_yaml_ng::Value =
        serde_yaml_ng::from_str(text).map_err(|e| error("invalid", e.to_string()))?;
    if !value.is_mapping() {
        return Err(error("invalid", "Rime custom YAML root must be a mapping"));
    }
    Ok(())
}
fn yaml_response(snapshot: Snapshot) -> Value {
    let validation = validate_yaml(&snapshot.text);
    json!({"revision":snapshot.revision,"text":snapshot.text,"backup_path":snapshot.backup_path,"valid":validation.is_ok(),"validation_error":validation.err().map(|e| e.message)})
}
fn theme_path(request: &Value) -> Result<&Path, BridgeError> {
    let path = Path::new(field(request, "path")?);
    let id = path
        .parent()
        .and_then(Path::file_name)
        .and_then(|s| s.to_str());
    let catalog = path
        .parent()
        .and_then(Path::parent)
        .and_then(Path::file_name)
        .and_then(|s| s.to_str());
    if path.file_name().and_then(|s| s.to_str()) != Some("theme.toml")
        || !id.is_some_and(myime_core::theme::valid_id)
        || catalog != Some("themes")
    {
        return Err(error(
            "invalid",
            "Theme path must end in themes/<theme-id>/theme.toml",
        ));
    }
    Ok(path)
}
fn validate_theme(path: &Path, text: &str) -> Result<myime_core::theme::Theme, BridgeError> {
    let theme = myime_core::theme::Theme::parse(text).map_err(|e| error("invalid", e))?;
    if path
        .parent()
        .and_then(Path::file_name)
        .and_then(|s| s.to_str())
        != Some(theme.id.as_str())
    {
        return Err(error(
            "invalid",
            "Theme metadata id must match its directory name",
        ));
    }
    Ok(theme)
}
fn theme_response(path: &Path, snapshot: Snapshot) -> Value {
    match validate_theme(path, &snapshot.text) {
        Ok(theme) => {
            json!({"revision":snapshot.revision,"text":snapshot.text,"backup_path":snapshot.backup_path,"valid":true,"id":theme.id,"name":theme.name})
        }
        Err(e) => {
            json!({"revision":snapshot.revision,"text":snapshot.text,"backup_path":snapshot.backup_path,"valid":false,"validation_error":e.message})
        }
    }
}
fn check_schemas(request: &Value) -> Result<Value, BridgeError> {
    let schemas = request
        .get("schemas")
        .and_then(Value::as_array)
        .filter(|a| !a.is_empty() && a.len() <= 512)
        .ok_or_else(|| {
            error(
                "protocol",
                "schemas must be a nonempty array of at most 512 IDs",
            )
        })?;
    let mut available = std::collections::BTreeSet::new();
    for value in schemas {
        let id = value
            .as_str()
            .filter(|s| !s.is_empty() && s.len() <= 128 && !s.chars().any(char::is_control))
            .ok_or_else(|| error("protocol", "schemas entries must be bounded strings"))?;
        available.insert(id);
    }
    let snapshot =
        config_bridge::read_snapshot(Path::new(field(request, "path")?), MAX_CONFIG_BYTES)?;
    let text = snapshot.text.as_str();
    let config = Config::parse(text).map_err(|e| error("invalid", e))?;
    let document = text
        .parse::<toml::Table>()
        .map_err(|e| error("invalid", e.to_string()))?;
    let mut targets = vec![String::new()];
    if let Some(profiles) = document.get("profiles").and_then(toml::Value::as_array) {
        targets.extend(
            profiles
                .iter()
                .filter_map(|p| p.get("executable").and_then(toml::Value::as_str))
                .map(str::to_owned),
        );
    }
    let mut checked = Vec::new();
    let mut missing = Vec::new();
    for exe in targets {
        let effective = config
            .effective(&exe, &toml::Table::new())
            .map_err(|e| error("invalid", e))?;
        let app = if exe.is_empty() {
            "<default Windows>"
        } else {
            exe.as_str()
        };
        if !available.contains(effective.schema.as_str()) {
            missing.push(format!(
                "{app} (enabled={}): {}",
                effective.enabled, effective.schema
            ));
        }
        checked
            .push(json!({"executable":app,"schema":effective.schema,"enabled":effective.enabled}));
    }
    if !missing.is_empty() {
        return Err(error(
            "missing_schema",
            format!(
                "目标数据不包含已配置的输入方案；请先调整配置再发布：{}",
                missing.join("; ")
            ),
        ));
    }
    Ok(json!({"valid":true,"checked":checked}))
}
fn dispatch(request: &Value) -> Result<Value, BridgeError> {
    if request.get("version").and_then(Value::as_u64) != Some(1) {
        return Err(error(
            "protocol",
            "Unsupported or absent request version; expected 1",
        ));
    }
    let command = request
        .get("command")
        .or_else(|| request.get("op"))
        .and_then(Value::as_str)
        .ok_or_else(|| error("protocol", "command is required"))?;
    let executable = request
        .get("executable")
        .and_then(Value::as_str)
        .unwrap_or("");
    match command {
        "config.check-schemas" => check_schemas(request),
        "config.read" => Ok(config_response(
            config_bridge::read_snapshot(Path::new(field(request, "path")?), MAX_CONFIG_BYTES)?,
            executable,
        )),
        "config.update" => {
            let path = Path::new(field(request, "path")?);
            let expected = expected(request)?;
            let snapshot = config_bridge::read_snapshot(path, MAX_CONFIG_BYTES)?;
            if snapshot.revision != expected {
                return Err(error(
                    "conflict",
                    "文件已修改；请重新读取并比较，不会覆盖新内容。",
                ));
            }
            let changes = request
                .get("changes")
                .and_then(Value::as_object)
                .ok_or_else(|| error("protocol", "changes must be an object"))?;
            let text = config_bridge::update_product_text(
                &snapshot.text,
                field(request, "scope")?,
                request.get("executable").and_then(Value::as_str),
                changes,
            )?;
            Ok(config_response(
                config_bridge::save_text(path, &expected, &text, MAX_CONFIG_BYTES)?,
                executable,
            ))
        }
        "config.write" => {
            let path = Path::new(field(request, "path")?);
            let text = field(request, "text")?;
            config_bridge::validate_product_text(text)?;
            Ok(config_response(
                config_bridge::save_text(path, &expected(request)?, text, MAX_CONFIG_BYTES)?,
                executable,
            ))
        }
        "yaml.read" => Ok(yaml_response(config_bridge::read_snapshot(
            yaml_path(request)?,
            MAX_CONFIG_BYTES,
        )?)),
        "yaml.write" => {
            let path = yaml_path(request)?;
            let text = field(request, "text")?;
            validate_yaml(text)?;
            Ok(yaml_response(config_bridge::save_text(
                path,
                &expected(request)?,
                text,
                MAX_CONFIG_BYTES,
            )?))
        }
        "yaml.validate" => {
            validate_yaml(field(request, "text")?)?;
            Ok(json!({"valid":true}))
        }
        "yaml.patch.preview" => {
            let size = request
                .get("page_size")
                .and_then(Value::as_u64)
                .filter(|size| (1..=100).contains(size))
                .ok_or_else(|| {
                    error("invalid", "page_size must be an integer between 1 and 100")
                })?;
            Ok(
                json!({"text":format!("# Generated preview by MYIME; save in the isolated patch directory.\npatch:\n  menu/page_size: {size}\n"),"requires_deploy":true}),
            )
        }
        "theme.read" => {
            let path = theme_path(request)?;
            Ok(theme_response(
                path,
                config_bridge::read_snapshot(path, myime_core::theme::MAX_MANIFEST_BYTES as usize)?,
            ))
        }
        "theme.write" => {
            let path = theme_path(request)?;
            let text = field(request, "text")?;
            validate_theme(path, text)?;
            Ok(theme_response(
                path,
                config_bridge::save_text(
                    path,
                    &expected(request)?,
                    text,
                    myime_core::theme::MAX_MANIFEST_BYTES as usize,
                )?,
            ))
        }
        "theme.template" => {
            let id = field(request, "id")?;
            if !myime_core::theme::valid_id(id) {
                return Err(error("invalid", "Invalid theme ID"));
            }
            let name = request.get("name").and_then(Value::as_str).unwrap_or(id);
            let mut table = include_str!("../../../themes/default/theme.toml")
                .parse::<toml::Table>()
                .expect("Bundled theme must parse");
            table.insert("id".into(), toml::Value::String(id.into()));
            table.insert("name".into(), toml::Value::String(name.into()));
            let text =
                toml::to_string_pretty(&table).map_err(|e| error("invalid", e.to_string()))?;
            myime_core::theme::Theme::parse(&text).map_err(|e| error("invalid", e))?;
            Ok(json!({"text":text,"valid":true,"id":id,"name":name}))
        }
        "import.preview" | "import.write" | "package.validate" | "dictionary.combine" => {
            if request.get("command").is_none() {
                let mut normalized = request.clone();
                normalized["command"] = json!(command);
                import_commands::dispatch(&normalized).map(|result| json!({"result":result}))
            } else {
                import_commands::dispatch(request).map(|result| json!({"result":result}))
            }
        }
        _ => Err(error("protocol", format!("Unsupported command: {command}"))),
    }
}
fn read_request(mut input: impl Read) -> Result<Value, BridgeError> {
    let mut bytes = Vec::new();
    input
        .by_ref()
        .take(MAX_REQUEST_BYTES as u64 + 1)
        .read_to_end(&mut bytes)
        .map_err(BridgeError::from)?;
    if bytes.len() > MAX_REQUEST_BYTES {
        return Err(error("too_large", "Request exceeds 4 MiB"));
    }
    let bytes = bytes.strip_prefix(&[0xef, 0xbb, 0xbf]).unwrap_or(&bytes);
    serde_json::from_slice(bytes).map_err(|e| error("protocol", e.to_string()))
}
fn response(result: Result<Value, BridgeError>) -> (Value, i32) {
    match result {
        Ok(mut value) => {
            let map = value
                .as_object_mut()
                .expect("Command response must be an object");
            map.insert("version".into(), json!(1));
            map.insert("ok".into(), json!(true));
            (value, 0)
        }
        Err(e) => {
            let code = e.exit_code();
            (
                json!({"version":1,"ok":false,"error":e.message,"kind":e.kind}),
                code,
            )
        }
    }
}
fn main() {
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args == ["--help"] {
        println!("myime-tool --request\nReads one bounded UTF-8 JSON request from stdin and writes one JSON response. See docs/maintenance-protocol.md.");
        return;
    }
    let result = if args != ["--request"] {
        Err(error(
            "protocol",
            "Use myime-tool --request with one JSON request on stdin",
        ))
    } else {
        std::panic::catch_unwind(|| {
            read_request(std::io::stdin().lock()).and_then(|request| dispatch(&request))
        })
        .unwrap_or_else(|_| {
            Err(error(
                "internal",
                "Unexpected maintenance error; no input text was logged",
            ))
        })
    };
    let (value, code) = response(result);
    let mut output = std::io::stdout().lock();
    if serde_json::to_writer(&mut output, &value).is_err()
        || output.write_all(b"\n").is_err()
        || output.flush().is_err()
    {
        std::process::exit(4);
    }
    std::process::exit(code);
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn protocol_is_bounded_and_versioned() {
        assert_eq!(read_request(&b"no json"[..]).unwrap_err().kind, "protocol");
        assert_eq!(
            read_request(std::io::repeat(b' ').take(MAX_REQUEST_BYTES as u64 + 1))
                .unwrap_err()
                .kind,
            "too_large"
        );
        assert_eq!(
            dispatch(&json!({"version":2,"command":"config.read"}))
                .unwrap_err()
                .kind,
            "protocol"
        );
        let (response, code) = response(Err(error("conflict", "changed")));
        assert_eq!(code, 3);
        assert_eq!(response["ok"], false);
        assert_eq!(response["kind"], "conflict");
    }
    #[test]
    fn yaml_validation_preserves_unknown_native_patch_data() {
        assert!(validate_yaml("patch:\n  menu/page_size: 7\n  future/value: [one, two]\n").is_ok());
        assert!(validate_yaml("patch: [").is_err());
        assert!(validate_yaml("- scalar").is_err());
        assert!(yaml_path(&json!({"path":"thirdparty.schema.yaml"})).is_err());
        assert!(yaml_path(&json!({"path":"default.custom.yaml"})).is_ok());
        assert_eq!(
            dispatch(&json!({"version":1,"command":"yaml.patch.preview","page_size":0}))
                .unwrap_err()
                .kind,
            "invalid"
        );
    }
}
