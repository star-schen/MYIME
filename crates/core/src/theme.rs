//! Data-only presentation packages. No platform APIs or input-provider calls.
#![forbid(unsafe_code)]
use crate::config::merge;
use std::{fs::File, io::Read, path::Path};
use toml::{Table, Value};

const DEFAULT: &str = include_str!("../../../themes/default/theme.toml");
pub const MAX_MANIFEST_BYTES: u64 = 64 * 1024;

pub fn valid_id(id: &str) -> bool {
    !id.is_empty() && id.len() <= 64
        && id.bytes().all(|c| c.is_ascii_lowercase() || c.is_ascii_digit() || c == b'-' || c == b'_')
}

#[derive(Clone, Debug, PartialEq)]
pub struct Theme {
    pub id: String, pub name: String, pub version: String, pub description: String,
    pub font_family: String, pub font_size: u32, pub font_weight: u32,
    pub horizontal: bool,
    pub padding_x: u32, pub padding_y: u32,
    pub candidate_padding_x: u32, pub candidate_padding_y: u32,
    pub spacing: u32, pub border_width: u32, pub border_radius: u32,
    pub min_width: u32, pub max_width: u32, pub caret_gap: u32,
    pub background: u32, pub text: u32, pub muted: u32, pub label: u32,
    pub highlight: u32, pub highlight_text: u32, pub border: u32,
    pub shadow: bool, pub shadow_size: u32, pub shadow_opacity: u32,
    pub preedit: bool, pub comments: bool, pub labels: bool, pub page_controls: bool,
}

fn item<'a>(table: &'a Table, section: &str, key: &str) -> Result<&'a Value, String> {
    table.get(section).and_then(Value::as_table).and_then(|t| t.get(key))
        .ok_or_else(|| format!("Missing theme field {section}.{key}"))
}
fn number(table: &Table, section: &str, key: &str, low: i64, high: i64) -> Result<u32, String> {
    let value = item(table, section, key)?.as_integer()
        .ok_or_else(|| format!("{section}.{key} must be an integer"))?;
    if !(low..=high).contains(&value) { return Err(format!("{section}.{key} must be {low}..{high}")); }
    Ok(value as u32)
}
fn flag(table: &Table, section: &str, key: &str) -> Result<bool, String> {
    item(table, section, key)?.as_bool().ok_or_else(|| format!("{section}.{key} must be boolean"))
}
fn bounded_text(value: &Value, key: &str, max: usize) -> Result<String, String> {
    let value = value.as_str().ok_or_else(|| format!("{key} must be a string"))?;
    if value.trim().is_empty() || value.chars().count() > max || value.chars().any(char::is_control) {
        return Err(format!("Invalid text for {key}"));
    }
    Ok(value.into())
}
fn metadata(table: &Table, key: &str, max: usize) -> Result<String, String> {
    bounded_text(table.get(key).ok_or_else(|| format!("Missing {key}"))?, key, max)
}
fn color(table: &Table, key: &str) -> Result<u32, String> {
    let value = item(table, "colors", key)?.as_str().ok_or("Colors must be #RRGGBB strings")?;
    if value.len() != 7 || !value.starts_with('#') || !value.as_bytes()[1..].iter().all(u8::is_ascii_hexdigit) {
        return Err(format!("colors.{key} must be #RRGGBB"));
    }
    u32::from_str_radix(&value[1..], 16).map_err(|e| e.to_string())
}
impl Theme {
    pub fn builtin() -> Self { Self::parse(DEFAULT).expect("Bundled theme must be valid") }
    pub fn parse(text: &str) -> Result<Self, String> {
        if text.len() as u64 > MAX_MANIFEST_BYTES { return Err("Theme manifest exceeds 64 KiB".into()); }
        let custom = text.parse::<Table>().map_err(|e| e.to_string())?;
        if custom.get("format_version").and_then(Value::as_integer) != Some(1) {
            return Err("Unsupported or missing theme format_version".into());
        }
        let id = metadata(&custom, "id", 64)?;
        if !valid_id(&id) { return Err("Theme id must contain lowercase ASCII letters, digits, - or _".into()); }
        let name = metadata(&custom, "name", 80)?;
        let version = metadata(&custom, "version", 32)?;
        let description = custom.get("description").map(|v| bounded_text(v, "description", 256))
            .transpose()?.unwrap_or_default();
        // One authoritative default asset, shared by runtime and preview.
        let mut values = DEFAULT.parse::<Table>().map_err(|e| e.to_string())?;
        merge(&mut values, &custom);
        let horizontal = match item(&values, "layout", "orientation")?.as_str() {
            Some("vertical") => false, Some("horizontal") => true,
            _ => return Err("layout.orientation must be vertical or horizontal".into()),
        };
        let min_width = number(&values, "layout", "min_width", 80, 1200)?;
        let max_width = number(&values, "layout", "max_width", 80, 1600)?;
        if min_width > max_width { return Err("min_width exceeds max_width".into()); }
        Ok(Self {
            id, name, version, description,
            font_family: bounded_text(item(&values, "font", "family")?, "font.family", 128)?,
            font_size: number(&values, "font", "size", 10, 48)?,
            font_weight: number(&values, "font", "weight", 100, 900)?, horizontal,
            padding_x: number(&values, "layout", "padding_x", 0, 64)?,
            padding_y: number(&values, "layout", "padding_y", 0, 64)?,
            candidate_padding_x: number(&values, "layout", "candidate_padding_x", 0, 64)?,
            candidate_padding_y: number(&values, "layout", "candidate_padding_y", 0, 32)?,
            spacing: number(&values, "layout", "spacing", 0, 32)?,
            border_width: number(&values, "layout", "border_width", 0, 4)?,
            border_radius: number(&values, "layout", "border_radius", 0, 32)?,
            min_width, max_width, caret_gap: number(&values, "layout", "caret_gap", 0, 32)?,
            background: color(&values, "background")?, text: color(&values, "text")?,
            muted: color(&values, "muted")?, label: color(&values, "label")?,
            highlight: color(&values, "highlight")?, highlight_text: color(&values, "highlight_text")?,
            border: color(&values, "border")?,
            shadow: flag(&values, "shadow", "enabled")?,
            shadow_size: number(&values, "shadow", "size", 0, 24)?,
            shadow_opacity: number(&values, "shadow", "opacity", 0, 160)?,
            preedit: flag(&values, "display", "preedit")?, comments: flag(&values, "display", "comments")?,
            labels: flag(&values, "display", "labels")?, page_controls: flag(&values, "display", "page_controls")?,
        })
    }
    pub fn read(path: &Path) -> Result<Self, String> {
        let mut text = String::new();
        File::open(path).map_err(|e| e.to_string())?.take(MAX_MANIFEST_BYTES + 1)
            .read_to_string(&mut text).map_err(|e| e.to_string())?;
        Self::parse(&text)
    }
}

pub struct ResolvedTheme { pub theme: Theme, pub warning: String }
/// User packages override installed packages. Invalid overrides fall back to
/// embedded default; no Rime configuration, deployment or userdb is touched.
pub fn resolve(id: &str, user_root: &Path, installed_root: &Path) -> ResolvedTheme {
    let load = || -> Result<Theme, String> {
        if !valid_id(id) { return Err("Invalid theme selector".into()); }
        for root in [user_root, installed_root] {
            let file = root.join(id).join("theme.toml");
            match file.try_exists() {
                Ok(false) => continue,
                Err(e) => return Err(e.to_string()),
                Ok(true) => {},
            }
            let canonical_root = root.canonicalize().map_err(|e| e.to_string())?;
            let canonical_file = file.canonicalize().map_err(|e| e.to_string())?;
            if !canonical_file.starts_with(&canonical_root) { return Err("Theme path leaves its catalog".into()); }
            let theme = Theme::read(&canonical_file)?;
            if theme.id != id { return Err("Manifest id differs from its directory".into()); }
            return Ok(theme);
        }
        Err("Theme package not found".into())
    };
    match load() {
        Ok(theme) => ResolvedTheme { theme, warning: String::new() },
        Err(warning) => ResolvedTheme { theme: Theme::builtin(), warning },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn builtin_packages_and_partial_inheritance() {
        let gray = Theme::builtin();
        assert_eq!(gray.id, "default");
        let dark = Theme::parse(include_str!("../../../themes/dark/theme.toml")).unwrap();
        let light = Theme::parse(include_str!("../../../themes/light/theme.toml")).unwrap();
        let ribbon = Theme::parse(include_str!("../../../themes/ribbon/theme.toml")).unwrap();
        assert_eq!(dark.font_size, gray.font_size);
        assert_ne!(dark.background, light.background);
        assert!(ribbon.horizontal && !ribbon.comments);
    }
    #[test]
    fn unsupported_format_bad_geometry_and_colors_are_rejected() {
        for suffix in ["\n[layout]\norientation='diagonal'", "\n[layout]\nmin_width=700\nmax_width=200",
            "\n[font]\nsize=100", "\n[colors]\ntext='#GG0000'", "\n[shadow]\nopacity=-1"] {
            assert!(Theme::parse(&format!("format_version=1\nid='sample'\nname='sample'\nversion='1'{suffix}")).is_err());
        }
        assert!(Theme::parse("format_version=2\nid='sample'\nname='sample'\nversion='1'").is_err());
        assert!(Theme::parse(&"x".repeat(MAX_MANIFEST_BYTES as usize + 1)).is_err());
    }
    #[test]
    fn selectors_cannot_be_paths_and_missing_packages_fall_back() {
        for id in ["", "../dark", "C:\\dark", "\\server", "Dark", "a/b"] { assert!(!valid_id(id)); }
        let result = resolve("missing", Path::new("__missing_user_themes__"), Path::new("__missing_installed_themes__"));
        assert_eq!(result.theme, Theme::builtin());
        assert!(!result.warning.is_empty());
    }
    #[test]
    fn unknown_metadata_does_not_require_code_or_modify_input_config() {
        let theme = Theme::parse("format_version=1\nid='sample'\nname='自定义'\nversion='1'\n[future]\nvalue=7").unwrap();
        assert_eq!(theme.font_family, Theme::builtin().font_family);
    }
}
