//! Data-only interface translations, separate from Rime/input language.
#![forbid(unsafe_code)]
use serde::Deserialize;
use std::{collections::BTreeMap, fs::File, io::Read, path::Path};

const LIMIT: u64 = 256 * 1024;
const HANS: &str = include_str!("../../../windows/settings/languages/zh-Hans.json");
const HANT: &str = include_str!("../../../windows/settings/languages/zh-Hant.json");
#[derive(Deserialize)]
struct Package {
    format_version: u32,
    id: String,
    name: String,
    messages: BTreeMap<String, String>,
}
pub struct UiLanguage { messages: BTreeMap<String, String> }
pub fn valid_id(id: &str) -> bool {
    !id.is_empty() && id.len() <= 32 && id.bytes().all(|c| c.is_ascii_alphanumeric() || c == b'-')
}
fn parse(text: &str, id: &str) -> Result<Package, String> {
    if text.len() as u64 > LIMIT { return Err("Language pack exceeds 256 KiB".into()); }
    let pack: Package = serde_json::from_str(text).map_err(|e| e.to_string())?;
    if pack.format_version != 1 || !valid_id(&pack.id) || !pack.id.eq_ignore_ascii_case(id)
        || pack.name.is_empty() || pack.name.chars().count() > 80 || pack.messages.len() > 2048
        || pack.messages.iter().any(|(key, value)| key.is_empty() || key.chars().count() > 512 || value.chars().count() > 4096) {
        return Err("Invalid language pack".into());
    }
    Ok(pack)
}
fn read(root: &Path, id: &str) -> Result<Package, String> {
    let file = crate::catalog_file::open(root, &root.join(format!("{id}.json")))?;
    let mut text = String::new();
    file.take(LIMIT + 1).read_to_string(&mut text).map_err(|e| e.to_string())?;
    parse(&text, id)
}
impl UiLanguage {
    pub fn load(user_root: &Path, installed_root: &Path, fallback: &str) -> Result<Self, String> {
        let mut preference = String::new();
        if let Ok(file) = File::open(user_root.join("ui-language.txt")) {
            let _ = file.take(129).read_to_string(&mut preference);
        }
        let id = if valid_id(preference.trim()) { preference.trim() }
            else if valid_id(fallback) { fallback } else { "zh-Hans" };
        let mut messages = parse(HANS, "zh-Hans")?.messages;
        if id.eq_ignore_ascii_case("zh-Hant") { messages.extend(parse(HANT, "zh-Hant")?.messages); }
        // Installed and user packs override embedded entries; missing keys fall back.
        for root in [installed_root.join("languages"), user_root.join("languages")] {
            if let Ok(pack) = read(&root, id) { messages.extend(pack.messages); }
        }
        Ok(Self { messages })
    }
    pub fn text(&self, key: &str) -> Result<&str, String> {
        self.messages.get(key).map(String::as_str).ok_or_else(|| "Unknown interface message".into())
    }
}
