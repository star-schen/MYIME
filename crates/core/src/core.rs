//! Safe, platform-independent input orchestration. No native handles or unsafe.
#![forbid(unsafe_code)]
use crate::{
    config::{Config, EffectiveConfig},
    extensions::{InputProvider, InputProviderFactory, KeyEvent, ProcessError},
    model::InputState,
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
}
impl Core {
    pub fn new(factory: Box<dyn InputProviderFactory>, schema: &str) -> Result<Self, String> {
        let config = Config::default();
        let mut initial = Table::new();
        initial.insert("schema".into(), Value::String(schema.into()));
        let effective = config.effective("", &initial)?;
        let provider = Self::prepare(factory.as_ref(), &effective)?;
        Ok(Self { provider, factory, config, effective: None })
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
        self.provider.state()
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
        self.provider.process(key)
    }

    pub fn select(&mut self, page_index: usize) -> Result<(), String> {
        self.require_acknowledged()?;
        if page_index >= self.state().candidates.len() {
            return Err("Candidate index out of range".into());
        }
        self.provider.select(page_index)
    }

    /// Cancellation discards both composition and any unacknowledged commit.
    pub fn clear(&mut self) -> Result<(), String> {
        self.provider.clear()
    }

    pub fn ack_commit(&mut self) -> Result<(), String> {
        self.provider.ack_commit()
    }

    /// Loading stages a product document; it does not change the live provider
    /// or invalidate the last successfully applied effective configuration.
    pub fn load_config(&mut self, path: &Path) -> Result<(), String> {
        let config = Config::read(path)?;
        self.set_config(config);
        Ok(())
    }

    pub fn set_config(&mut self, config: Config) {
        self.config = config;
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
        Ok(self.effective.as_ref().unwrap().enabled)
    }
}

#[cfg(test)]
mod tests;
