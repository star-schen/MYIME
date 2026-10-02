//! Test the actual settings-facing process protocol, including UTF-8 and revisions.
use serde_json::{json, Value};
use std::{
    fs,
    io::Write,
    path::PathBuf,
    process::{Command, Stdio},
    time::{SystemTime, UNIX_EPOCH},
};

struct Fixture(PathBuf);
impl Fixture {
    fn new() -> Self {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = std::env::temp_dir().join(format!(
            "myime-import-process-{}-{stamp}",
            std::process::id()
        ));
        fs::create_dir(&path).unwrap();
        Self(path)
    }
}
impl Drop for Fixture {
    fn drop(&mut self) {
        let _ = fs::remove_file(self.0.join("words.csv"));
        for file in [
            "fixture.dict.yaml",
            "myime_fixture.dict.yaml",
            "myime_fixture.schema.yaml",
            "metadata.json",
            "manifest.json",
            "README.md",
        ] {
            let _ = fs::remove_file(self.0.join("packages/fixture").join(file));
        }
        let _ = fs::remove_dir(self.0.join("packages/fixture"));
        let _ = fs::remove_dir(self.0.join("packages"));
        let _ = fs::remove_dir(&self.0);
    }
}
fn call(request: &Value) -> (i32, Value) {
    let mut child = Command::new(env!("CARGO_BIN_EXE_myime-tool"))
        .arg("--request")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(&serde_json::to_vec(request).unwrap())
        .unwrap();
    let output = child.wait_with_output().unwrap();
    assert!(
        output.stderr.is_empty(),
        "CLI must not emit user words to stderr: {:?}",
        output.stderr
    );
    (
        output.status.code().unwrap(),
        serde_json::from_slice(&output.stdout).unwrap(),
    )
}
#[test]
fn unicode_csv_preview_package_and_conflicts_cross_the_process_boundary() {
    let fixture = Fixture::new();
    let input = fixture.0.join("words.csv");
    fs::write(&input, "\u{feff}word,code,frequency,source\r\n你好,ni hao,9,中文来源\r\n你好,ni hao,2,另一来源\r\n无编码,,,待补充\r\n").unwrap();
    let (status, preview) =
        call(&json!({"version":1,"command":"import.preview","input_path":input,"format":"csv"}));
    assert_eq!(status, 0);
    assert_eq!(preview["ok"], true);
    assert_eq!(preview["result"]["summary"]["duplicates"], 1);
    assert!(preview["result"]["sample"]
        .as_array()
        .unwrap()
        .iter()
        .any(|word| word["word"] == "你好"));
    let request = json!({"version":1,"command":"import.write","input_path":input,"format":"csv","input_revision":preview["result"]["input_revision"],"packages_root":fixture.0.join("packages"),"id":"fixture","name":"中文词库"});
    let (status, package) = call(&request);
    assert_eq!(status, 0);
    assert_eq!(package["result"]["manifest"]["name"], "中文词库");
    let dictionary =
        fs::read_to_string(fixture.0.join("packages/fixture/fixture.dict.yaml")).unwrap();
    assert!(dictionary.contains("你好\tni hao\t9"));
    assert!(!dictionary.contains("无编码"));
    let (status, validation) = call(
        &json!({"version":1,"command":"package.validate","path":fixture.0.join("packages/fixture")}),
    );
    assert_eq!(status, 0);
    assert_eq!(
        validation["result"]["manifest"]["schema_id"],
        "myime_fixture"
    );
    let (status, duplicate) = call(&request);
    assert_ne!(status, 0);
    assert_eq!(duplicate["kind"], "exists");
    fs::write(input, "word,code\n再见,zai jian\n").unwrap();
    let (status, conflict) = call(&request);
    assert_eq!(status, 3);
    assert_eq!(conflict["kind"], "conflict");
    assert_eq!(
        fs::read_to_string(fixture.0.join("packages/fixture/fixture.dict.yaml")).unwrap(),
        dictionary
    );
}
