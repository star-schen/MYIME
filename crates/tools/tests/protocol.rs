//! Runs the real CLI only against newly-created fixture directories.
use serde_json::{json, Value};
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
    sync::atomic::{AtomicU64, Ordering},
};
static NEXT: AtomicU64 = AtomicU64::new(0);
struct Fixture(PathBuf);
impl Fixture {
    fn new() -> Self {
        let path = std::env::temp_dir().join(format!(
            "myime-cli-test-{}-{}",
            std::process::id(),
            NEXT.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }
    fn path(&self, name: &str) -> PathBuf {
        self.0.join(name)
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}
fn call(request: Value) -> (Value, i32) {
    let mut process = Command::new(env!("CARGO_BIN_EXE_myime-tool"))
        .arg("--request")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    process
        .stdin
        .take()
        .unwrap()
        .write_all(&serde_json::to_vec(&request).unwrap())
        .unwrap();
    let output = process.wait_with_output().unwrap();
    assert!(
        output.stderr.is_empty(),
        "unexpected diagnostics: {}",
        String::from_utf8_lossy(&output.stderr)
    );
    let response: Value = serde_json::from_slice(&output.stdout).unwrap();
    assert_eq!(response["version"], 1);
    (response, output.status.code().unwrap())
}
#[test]
fn configuration_roundtrip_preserves_comments_and_reports_conflicts() {
    let fixture = Fixture::new();
    let path = fixture.path("config.toml");
    fs::write(
        &path,
        "# preserve me\n[default]\nschema='pinyin_simp'\nfuture=7\n",
    )
    .unwrap();
    let (first, code) = call(json!({"version":1,"command":"config.read","path":path}));
    assert_eq!(code, 0);
    assert_eq!(first["effective"]["theme"], "default");
    let (saved, code) = call(
        json!({"version":1,"command":"config.update","path":path,"expected":first["revision"],"scope":"profile","executable":"Game.exe","changes":{"theme":"dark","ascii_mode":true}}),
    );
    assert_eq!(code, 0);
    assert_eq!(saved["effective"]["theme"], "dark");
    assert!(saved["text"].as_str().unwrap().contains("# preserve me"));
    assert_eq!(
        fs::read_to_string(saved["backup_path"].as_str().unwrap()).unwrap(),
        first["text"].as_str().unwrap()
    );
    let (conflict, code) = call(
        json!({"version":1,"command":"config.write","path":path,"expected":first["revision"],"text":""}),
    );
    assert_eq!(code, 3);
    assert_eq!(conflict["kind"], "conflict");
    let (inherit, code) = call(
        json!({"version":1,"command":"config.update","path":path,"expected":saved["revision"],"scope":"profile","executable":"GAME.EXE","changes":{"theme":null}}),
    );
    assert_eq!(code, 0);
    assert_eq!(inherit["effective"]["theme"], "default");
    assert_eq!(inherit["profiles"].as_array().unwrap().len(), 1);
}
#[test]
fn invalid_full_text_cannot_damage_existing_file_and_bad_source_can_be_repaired() {
    let fixture = Fixture::new();
    let path = fixture.path("config.toml");
    fs::write(&path, "[default]\nenabled=true\n").unwrap();
    let (original, _) = call(json!({"version":1,"command":"config.read","path":path}));
    let (bad, code) = call(
        json!({"version":1,"command":"config.write","path":path,"expected":original["revision"],"text":"[default]\nenabled='wrong'"}),
    );
    assert_eq!(code, 2);
    assert_eq!(bad["kind"], "invalid");
    assert_eq!(
        fs::read_to_string(&path).unwrap(),
        original["text"].as_str().unwrap()
    );
    fs::write(&path, "not valid TOML = [").unwrap();
    let (broken, code) = call(json!({"version":1,"command":"config.read","path":path}));
    assert_eq!(code, 0);
    assert_eq!(broken["valid"], false);
    assert!(broken["effective"].is_null());
    let (repaired, code) = call(
        json!({"version":1,"command":"config.write","path":path,"expected":broken["revision"],"text":"[default.ui]\ntheme='light'"}),
    );
    assert_eq!(code, 0);
    assert_eq!(repaired["valid"], true);
    assert_eq!(repaired["effective"]["theme"], "light");
}
#[test]
fn yaml_editor_keeps_unknown_patch_and_rejects_schema_files() {
    let fixture = Fixture::new();
    let path = fixture.path("default.custom.yaml");
    let (missing, code) = call(json!({"version":1,"command":"yaml.read","path":path}));
    assert_eq!(code, 0);
    assert_eq!(missing["revision"]["exists"], false);
    let text =
        "# user's comment\npatch:\n  menu/page_size: 9\n  future/native: {value: unchanged}\n";
    let (saved, code) = call(
        json!({"version":1,"command":"yaml.write","path":path,"expected":missing["revision"],"text":text}),
    );
    assert_eq!(code, 0);
    assert_eq!(saved["text"], text);
    let (bad, code) = call(
        json!({"version":1,"command":"yaml.write","path":path,"expected":saved["revision"],"text":"patch: ["}),
    );
    assert_eq!(code, 2);
    assert_eq!(bad["kind"], "invalid");
    assert_eq!(fs::read_to_string(&path).unwrap(), text);
    let (bad, code) = call(
        json!({"version":1,"command":"yaml.read","path":fixture.path("thirdparty.schema.yaml")}),
    );
    assert_eq!(code, 2);
    assert_eq!(bad["kind"], "invalid");
}
#[test]
fn theme_editor_requires_matching_id_keeps_text_and_guards_revisions() {
    let fixture = Fixture::new();
    let path = fixture.0.join("themes/mytheme/theme.toml");
    let (template, code) =
        call(json!({"version":1,"command":"theme.template","id":"mytheme","name":"我的主题"}));
    assert_eq!(code, 0);
    let (missing, code) = call(json!({"version":1,"command":"theme.read","path":path}));
    assert_eq!(code, 0);
    assert_eq!(missing["revision"]["exists"], false);
    let text = format!(
        "# keep my theme comment\n{}",
        template["text"].as_str().unwrap()
    );
    let (saved, code) = call(
        json!({"version":1,"command":"theme.write","path":path,"expected":missing["revision"],"text":text}),
    );
    assert_eq!(code, 0);
    assert_eq!(saved["valid"], true);
    assert_eq!(fs::read_to_string(&path).unwrap(), text);
    let (bad, code) = call(
        json!({"version":1,"command":"theme.write","path":path,"expected":saved["revision"],"text":"format_version=1\nid='other'\nname='Other'\nversion='1'"}),
    );
    assert_eq!(code, 2);
    assert_eq!(bad["kind"], "invalid");
    fs::write(&path, format!("{text}\n# external edit\n")).unwrap();
    let (conflict, code) = call(
        json!({"version":1,"command":"theme.write","path":path,"expected":saved["revision"],"text":text}),
    );
    assert_eq!(code, 3);
    assert_eq!(conflict["kind"], "conflict");
    let (_, code) =
        call(json!({"version":1,"command":"theme.read","path":fixture.path("theme.toml")}));
    assert_eq!(code, 2);
}
#[test]
fn schema_check_uses_windows_inheritance_and_checks_disabled_profiles_without_writing() {
    let fixture = Fixture::new();
    let path = fixture.path("config.toml");
    let text="[default]\nschema='base'\n[platform.windows]\nschema='windows'\n[[profiles]]\nexecutable='Editor.exe'\n[profiles.overrides]\nschema='editor'\n[[profiles]]\nexecutable='Disabled.exe'\n[profiles.overrides]\nenabled=false\nschema='disabled'\n";
    fs::write(&path, text).unwrap();
    let (missing, code) = call(
        json!({"version":1,"command":"config.check-schemas","path":path,"schemas":["base","windows","editor"]}),
    );
    assert_eq!(code, 2);
    assert_eq!(missing["kind"], "missing_schema");
    assert!(missing["error"]
        .as_str()
        .unwrap()
        .contains("Disabled.exe (enabled=false): disabled"));
    let (valid, code) = call(
        json!({"version":1,"command":"config.check-schemas","path":path,"schemas":["windows","editor","disabled"]}),
    );
    assert_eq!(code, 0);
    assert_eq!(valid["valid"], true);
    assert_eq!(valid["checked"][0]["schema"], "windows");
    assert_eq!(valid["checked"][2]["enabled"], false);
    assert_eq!(fs::read_to_string(&path).unwrap(), text);
    let missingpath = fixture.path("missing.toml");
    let (valid, code) = call(
        json!({"version":1,"command":"config.check-schemas","path":missingpath,"schemas":["pinyin_simp"]}),
    );
    assert_eq!(code, 0);
    assert_eq!(valid["valid"], true);
    assert!(!missingpath.exists());
    let (invalid, code) =
        call(json!({"version":1,"command":"config.check-schemas","path":path,"schemas":[]}));
    assert_eq!(code, 2);
    assert_eq!(invalid["kind"], "protocol");
}
#[test]
fn dictionary_combine_command_keeps_both_package_sources() {
    let fixture = Fixture::new();
    let shared = fixture.0.join("shared");
    fs::create_dir(&shared).unwrap();
    for id in ["second", "first"] {
        for name in [
            format!("{id}.dict.yaml"),
            format!("myime_{id}.dict.yaml"),
            format!("myime_{id}.schema.yaml"),
        ] {
            fs::write(shared.join(name), "fixture source").unwrap();
        }
    }
    let (reply, code) =
        call(json!({"version":1,"command":"dictionary.combine","shared_dir":shared}));
    assert_eq!(code, 0);
    assert_eq!(reply["result"]["schema"], "myime_global");
    assert_eq!(reply["result"]["packages"], json!(["first", "second"]));
    let text = fs::read_to_string(shared.join("myime_global.dict.yaml")).unwrap();
    assert!(text.contains("  - first\n  - second\n"));
    assert!(fs::read_to_string(shared.join("myime_global.schema.yaml"))
        .unwrap()
        .contains("myime_global.custom:/patch?"));
    let schema: serde_yaml_ng::Value = serde_yaml_ng::from_str(
        &fs::read_to_string(shared.join("myime_global.schema.yaml")).unwrap(),
    )
    .unwrap();
    assert_eq!(schema["schema"]["schema_id"].as_str(), Some("myime_global"));
    assert_eq!(
        schema["schema"]["__include"].as_str(),
        Some("pinyin_simp.schema:/schema")
    );
    let (bad, code) =
        call(json!({"version":1,"command":"dictionary.combine","shared_dir":"relative"}));
    assert_eq!(code, 2);
    assert_eq!(bad["kind"], "invalid");
}
