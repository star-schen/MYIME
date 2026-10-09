//! Safe, platform-independent input orchestration. No native handles or unsafe.
#![forbid(unsafe_code)]
use crate::{
    config::{Config, EffectiveConfig},
    extensions::{InputProvider, InputProviderFactory, KeyEvent, ProcessError},
    model::{Candidate, InputState},
};
use std::path::Path;
use toml::{Table, Value};

/// Intentionally not Send/Sync: providers and their factory stay on the creating
/// thread. The C handle additionally rejects calls made by another native thread.
pub struct Core {
    provider: Box<dyn InputProvider>,
    factory: Box<dyn InputProviderFactory>,
    config: Config,
    effective: Option<EffectiveConfig>,
    literal: String,
    literal_commit: bool,
    presentation: Option<InputState>,
    executable: String,
    application_path: String,
    manual_group: String,
    learning: crate::learning::Learning,
    group: String,
}
impl Core {
    pub fn new(factory: Box<dyn InputProviderFactory>, schema: &str) -> Result<Self, String> {
        let config = Config::default();
        let mut initial = Table::new();
        initial.insert("schema".into(), Value::String(schema.into()));
        let effective = config.effective("", &initial)?;
        let provider = Self::prepare(factory.as_ref(), &effective)?;
        Ok(Self { provider, factory, config, effective: None, literal: String::new(), literal_commit: false, presentation: None, executable: String::new(), application_path: String::new(), manual_group: String::new(), learning: crate::learning::Learning::from_document(&Table::new())?, group: "daily".into() })
    }

    fn prepare(
        factory: &dyn InputProviderFactory,
        config: &EffectiveConfig,
    ) -> Result<Box<dyn InputProvider>, String> {
        let provider = factory.create(config)?;
        // Read the prepared state before publishing the replacement. The factory
        // is responsible for any fallible native snapshot/configuration work.
        let state = provider.state();
        if state.active || !state.preedit.is_empty() || !state.commit.is_empty() {
            return Err("New provider must have an idle initial state".into());
        }
        Ok(provider)
    }

    pub fn state(&self) -> &InputState {
        self.presentation.as_ref().unwrap_or_else(|| self.provider.state())
    }

    pub fn effective_config(&self) -> Option<&EffectiveConfig> {
        self.effective.as_ref()
    }

    fn require_acknowledged(&self) -> Result<(), String> {
        if !self.state().commit.is_empty() {
            return Err("Acknowledge pending commit before another input".into());
        }
        Ok(())
    }

    pub fn process(&mut self, key: KeyEvent) -> Result<bool, ProcessError> {
        self.require_acknowledged()?;
        let direct = self.input_policy("caps_lock", "compose_uppercase") == "commit_then_direct";
        let caps = key.modifiers & 2 != 0;
        if key.modifiers & (1 << 30) != 0 && (key.keysym == 0xffe5 || !self.literal.is_empty()) { return Ok(false); }
        if key.keysym == 0xffe5 {
            if (direct && self.state().active) || !self.literal.is_empty() {
                self.commit_current().map_err(|message| ProcessError { message, eaten: false })?;
            }
            // Windows must see the key to update its system toggle state.
            return Ok(false);
        }
        if self.provider.state().options.get("ascii_mode") != Some(&true) {
            if !direct && caps && key.modifiers & (4 | 8 | 64 | (1 << 26)) == 0 && (33..=126).contains(&key.keysym) {
                self.literal.push(char::from_u32(key.keysym as u32).unwrap().to_ascii_uppercase()); self.refresh_literal(); return Ok(true);
            }
            if !self.literal.is_empty() {
                match key.keysym {
                    0xff1b => { self.clear()?; return Ok(true); }
                    0xff08 => { self.literal.pop(); self.refresh_literal(); return Ok(true); }
                    32 | 0xff0d => { self.commit_current().map_err(|message| ProcessError { message, eaten: true })?; return Ok(true); }
                    _ => return Ok(true), // Keep navigation/shortcuts from leaking through an owned composition.
                }
            }
            if direct && caps && !self.state().active { return Ok(false); }
        }
        self.provider.process(key)
    }

    pub fn select(&mut self, page_index: usize) -> Result<(), String> {
        self.require_acknowledged()?;
        if page_index >= self.state().candidates.len() {
            return Err("Candidate index out of range".into());
        }
        if !self.literal.is_empty() { return self.commit_current(); }
        self.provider.select(page_index)
    }

    /// Cancellation discards both composition and any unacknowledged commit.
    pub fn clear(&mut self) -> Result<(), String> {
        self.provider.clear()?; self.literal.clear(); self.literal_commit = false; self.presentation = None; self.apply_learning()
    }

    pub fn ack_commit(&mut self) -> Result<(), String> {
        self.provider.ack_commit()?;
        if self.literal_commit { self.literal.clear(); self.literal_commit = false; }
        self.refresh_literal(); if !self.state().active { self.apply_learning()?; } Ok(())
    }
    pub fn input_policy(&self, key: &str, default: &str) -> String { self.effective.as_ref().map(|e| e.input_policy(key, default)).unwrap_or_else(|| default.into()) }
    fn refresh_literal(&mut self) {
        if self.literal.is_empty() { self.presentation = None; return; }
        let native = self.provider.state(); let mut state = native.clone();
        if self.literal_commit { state.commit.push_str(&self.literal); state.active = false; state.preedit.clear(); state.candidates.clear(); state.caret = 0; }
        else {
            state.preedit.push_str(&self.literal); state.caret = state.preedit.len(); state.active = true;
            let prefix = native.candidates.get(native.selected).map(|c| c.text.as_str()).unwrap_or(&native.preedit);
            state.candidates = vec![Candidate { text: format!("{prefix}{}", self.literal), comment: "大写字母".into(), label: "1".into(), index: 0 }];
            state.selected = 0; state.page = 0; state.page_size = 1; state.last_page = true;
        }
        self.presentation = Some(state);
    }
    pub fn commit_current(&mut self) -> Result<(), String> {
        self.require_acknowledged()?;
        if self.provider.state().active { self.provider.commit_current()?; }
        self.literal_commit = !self.literal.is_empty(); self.refresh_literal(); Ok(())
    }
    /// Refresh presentation and policies while deferring input-provider replacement
    /// until its current document transaction has finished.
    pub fn reload(&mut self, path: &Path) -> Result<bool, String> {
        let config = Config::read(path)?; let next = config.effective(&self.executable, &Table::new())?;
        self.learning = config.learning()?; self.config = config;
        if !self.state().active && self.state().commit.is_empty() { self.apply_learning()?; }
        if self.state().active || !self.state().commit.is_empty() {
            if let Some(current) = self.effective.as_mut() { for key in ["ui", "input"] { current.values.remove(key); if let Some(value) = next.values.get(key) { current.values.insert(key.into(), value.clone()); } } }
            return Ok(self.effective.as_ref().is_none_or(|e| e.enabled));
        }
        self.apply_profile(&self.executable.clone())
    }

    /// Loading stages a product document; it does not change the live provider
    /// or invalidate the last successfully applied effective configuration.
    pub fn load_config(&mut self, path: &Path) -> Result<(), String> {
        let config = Config::read(path)?;
        self.set_config(config);
        Ok(())
    }

    pub fn set_config(&mut self, config: Config) {
        if let Ok(value) = config.learning() { self.learning = value; }
        self.config = config;
    }
    pub fn learning(&self) -> &crate::learning::Learning { &self.learning }
    pub fn learning_group(&self) -> &str { &self.group }
    pub fn set_learning_context(&mut self, application: &str, manual: &str) -> Result<(), String> {
        if !manual.is_empty() && !self.learning.groups.iter().any(|g|g.id==manual) { return Err("Unknown learning group".into()); }
        if application.len()>32768 || application.chars().any(char::is_control) { return Err("Invalid application path".into()); }
        self.application_path = application.into(); self.manual_group = manual.into();
        if !self.state().active && self.state().commit.is_empty() { self.apply_learning()?; }
        Ok(())
    }
    fn apply_learning(&mut self) -> Result<(), String> {
        let group = self.learning.group_for(&self.application_path, &self.manual_group);
        self.provider.learning_context(&self.application_path, &group, self.learning.habits_enabled)?;
        self.group = group; Ok(())
    }

    pub fn apply_profile(&mut self, executable: &str) -> Result<bool, String> {
        let effective = self.config.effective(executable, &Table::new())?;
        if self.effective.as_ref() != Some(&effective) {
            let input_changed = self.effective.as_ref()
                .is_none_or(|previous| !previous.same_input_configuration(&effective));
            if input_changed {
                if self.state().active || !self.state().preedit.is_empty() || !self.state().commit.is_empty() {
                    return Err("Finish composition before switching profile".into());
                }
                let provider = Self::prepare(self.factory.as_ref(), &effective)?;
                // No fallible work after this point. The provider releases its
                // native runtime lease. UI-only changes reuse the live provider.
                self.provider = provider;
            }
            self.effective = Some(effective);
        }
        self.executable = executable.into();
        if !self.state().active && self.state().commit.is_empty() { self.apply_learning()?; }
        Ok(self.effective.as_ref().unwrap().enabled)
    }
}

#[cfg(test)]
mod tests;
