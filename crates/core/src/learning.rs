//! Product grouping metadata. No dictionary choice or guessed EXE categories.
use serde::{Deserialize, Serialize};
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Group { pub id: String, pub name: String }
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct Application { pub path: String, #[serde(default)] pub name: String, pub group: String }
#[derive(Clone, Debug, Default, Serialize, Deserialize)]
#[serde(default)]
pub struct Learning {
    pub habits_enabled: bool,
    pub ai_enabled: bool,
    pub groups: Vec<Group>,
    pub applications: Vec<Application>,
}
impl Learning {
    pub fn from_document(document: &toml::Table) -> Result<Self, String> {
        let mut value: Self = match document.get("learning") { Some(value) => value.clone().try_into().map_err(|e: toml::de::Error| e.to_string())?, None => Self::default() };
        for (id, name) in [("daily", "日常"), ("work", "工作")] { if !value.groups.iter().any(|g| g.id == id) { value.groups.push(Group { id: id.into(), name: name.into() }); } }
        if value.groups.len() > 32 || value.applications.len() > 512 { return Err("Learning groups/applications exceed limits (32/512)".into()); }
        let mut ids=std::collections::BTreeSet::new();
        for group in &value.groups { if !crate::theme::valid_id(&group.id) || group.id=="global" || group.name.trim().is_empty() || group.name.chars().count()>80 || group.name.chars().any(char::is_control) || !ids.insert(&group.id) { return Err("Invalid or duplicate learning group".into()); } }
        let mut paths=std::collections::BTreeSet::new();
        for app in &value.applications {
            if app.path.len()>32768 || !std::path::Path::new(&app.path).is_absolute() || app.path.chars().any(char::is_control) || !app.path.to_ascii_lowercase().ends_with(".exe") || app.name.chars().count()>128 || app.name.chars().any(char::is_control) || !ids.contains(&app.group) || !paths.insert(app.path.to_ascii_lowercase()) { return Err("Invalid or duplicate learning application mapping".into()); }
        }
        Ok(value)
    }
    pub fn group_for(&self, application: &str, manual: &str) -> String {
        if !manual.is_empty() && self.groups.iter().any(|g|g.id==manual) { return manual.into(); }
        self.applications.iter().find(|a|a.path.eq_ignore_ascii_case(application)).map(|a|a.group.clone()).unwrap_or_else(||"daily".into())
    }
}
