//! Scripted provider only for Core contract tests. No Rime or platform calls.
use super::*;
use crate::model::Candidate;
use std::{cell::RefCell, rc::Rc};

#[derive(Clone, Copy)]
enum Failure { Create, Configure, Snapshot, NonIdle }

#[derive(Default)]
struct Trace {
    attempts: usize,
    allocated: usize,
    dropped: Vec<usize>,
    live_at_create: Vec<usize>,
    keys: Vec<(i32, i32)>,
    selections: Vec<usize>,
    clears: usize,
    acknowledgements: usize,
    failure: Option<Failure>,
    operation_failure: Option<&'static str>,
}
struct Factory(Rc<RefCell<Trace>>);
struct Provider {
    id: usize,
    trace: Rc<RefCell<Trace>>,
    state: InputState,
}
impl Drop for Provider {
    fn drop(&mut self) { self.trace.borrow_mut().dropped.push(self.id); }
}
impl InputProviderFactory for Factory {
    fn create(&self, config: &EffectiveConfig) -> Result<Box<dyn InputProvider>, String> {
        let mut trace = self.0.borrow_mut();
        trace.attempts += 1;
        let live = trace.allocated - trace.dropped.len();
        trace.live_at_create.push(live);
        let failure = trace.failure.take();
        if matches!(failure, Some(Failure::Create)) { return Err("create failure".into()); }
        trace.allocated += 1;
        let id = trace.allocated;
        drop(trace);
        let mut provider = Box::new(Provider {
            id, trace: self.0.clone(), state: InputState::default(),
        });
        provider.state.schema.clone_from(&config.schema);
        if matches!(failure, Some(Failure::Configure)) { return Err("configure failure".into()); }
        provider.state.options.clone_from(&config.options);
        if matches!(failure, Some(Failure::Snapshot)) { return Err("snapshot failure".into()); }
        if matches!(failure, Some(Failure::NonIdle)) {
            provider.state.commit = "unacknowledged initial commit".into();
        }
        Ok(provider)
    }
}
impl Provider {
    fn operation(&self, name: &str) -> Result<(), String> {
        if self.trace.borrow().operation_failure == Some(name) {
            return Err(format!("{name} failure"));
        }
        Ok(())
    }
    fn page(&mut self, page: usize) {
        self.state.active = true;
        self.state.preedit = "mock composition".into();
        self.state.caret = self.state.preedit.len();
        self.state.page = page;
        self.state.page_size = 2;
        self.state.last_page = page == 1;
        self.state.selected = 0;
        self.state.candidates = (0..2).map(|local| Candidate {
            text: format!("word-{}", page * 2 + local),
            label: (local + 1).to_string(),
            comment: String::new(), index: page * 2 + local,
        }).collect();
    }
    fn cancel(&mut self) {
        self.state.active = false;
        self.state.preedit.clear();
        self.state.caret = 0;
        self.state.candidates.clear();
        self.state.commit.clear();
    }
}
impl InputProvider for Provider {
    fn process(&mut self, key: KeyEvent) -> Result<bool, ProcessError> {
        self.operation("process")?;
        self.trace.borrow_mut().keys.push((key.keysym, key.modifiers));
        match key.keysym {
            0x6e => self.page(0),
            0xff56 => self.page(1),
            0xff55 => self.page(0),
            0xff1b => self.cancel(),
            0x20 if self.state.active => self.select(0)?,
            _ => return Ok(false),
        }
        self.operation("snapshot").map_err(|message| ProcessError { message, eaten: true })?;
        Ok(true)
    }
    fn state(&self) -> &InputState { &self.state }
    fn select(&mut self, page_index: usize) -> Result<(), String> {
        self.operation("select")?;
        let commit = self.state.candidates.get(page_index).ok_or("mock index out of range")?.text.clone();
        self.trace.borrow_mut().selections.push(page_index);
        self.cancel();
        self.state.commit = commit;
        Ok(())
    }
    fn clear(&mut self) -> Result<(), String> {
        self.operation("clear")?;
        self.trace.borrow_mut().clears += 1;
        self.cancel();
        Ok(())
    }
    fn ack_commit(&mut self) -> Result<(), String> {
        self.operation("ack_commit")?;
        self.trace.borrow_mut().acknowledgements += 1;
        self.state.commit.clear();
        Ok(())
    }
}
fn factory(trace: &Rc<RefCell<Trace>>) -> Box<dyn InputProviderFactory> {
    Box::new(Factory(trace.clone()))
}
fn setup() -> (Core, Rc<RefCell<Trace>>) {
    let trace = Rc::new(RefCell::new(Trace::default()));
    (Core::new(factory(&trace), "initial").unwrap(), trace)
}
fn key(core: &mut Core, keysym: i32) -> Result<bool, ProcessError> {
    core.process(KeyEvent { keysym, modifiers: 0 })
}
fn profiles() -> Config {
    Config::parse("[default]\nschema='base'\n[[profiles]]\nexecutable='Editor.exe'\n[profiles.overrides]\nschema='editor'\nenabled=false\n[profiles.overrides.options]\nfull_shape=true").unwrap()
}

#[test]
fn theme_only_profile_does_not_recreate_provider_or_lose_input() {
    let (mut core, trace) = setup();
    core.set_config(Config::parse("[default.ui]\ntheme='light'\n[[profiles]]\nexecutable='Editor.exe'\n[profiles.overrides.ui]\ntheme='dark'").unwrap());
    core.apply_profile("Other.exe").unwrap();
    let attempts = trace.borrow().attempts;
    key(&mut core, 0x6e).unwrap();
    let preedit = core.state().preedit.clone();
    let candidate = core.state().candidates[0].text.clone();
    core.apply_profile("Editor.exe").unwrap();
    assert_eq!(core.effective_config().unwrap().theme_id(), "dark");
    assert_eq!(trace.borrow().attempts, attempts);
    assert_eq!(core.state().preedit, preedit);
    assert_eq!(core.state().candidates[0].text, candidate);
    core.select(0).unwrap();
    core.apply_profile("Other.exe").unwrap();
    assert_eq!(core.state().commit, "word-0");
    assert_eq!(trace.borrow().attempts, attempts);
}

#[test]
fn factory_applies_initial_schema_and_input_uses_one_live_provider() {
    let (mut core, trace) = setup();
    assert_eq!(core.state().schema, "initial");
    assert_eq!(core.state().options["ascii_mode"], false);
    assert!(core.effective_config().is_none());
    assert!(core.process(KeyEvent { keysym: 0x6e, modifiers: 4 }).unwrap());
    assert!(core.state().active);
    assert_eq!(core.state().caret, core.state().preedit.len());
    assert!(!key(&mut core, 0x78).unwrap());
    assert_eq!(trace.borrow().keys, vec![(0x6e, 4), (0x78, 0)]);
    assert_eq!(trace.borrow().attempts, 1);
    assert!(trace.borrow().dropped.is_empty());
}

#[test]
fn commit_is_retained_until_ack_and_blocks_keys_and_selection() {
    let (mut core, trace) = setup();
    key(&mut core, 0x6e).unwrap();
    key(&mut core, 0x20).unwrap();
    assert_eq!(core.state().commit, "word-0");
    assert!(!core.state().active);
    assert_eq!(core.state().commit, "word-0"); // Repeated reads do not consume it.
    assert!(key(&mut core, 0x6e).is_err());
    assert!(core.select(0).is_err());
    assert_eq!(trace.borrow().keys.len(), 2);
    assert_eq!(trace.borrow().selections, vec![0]);
    core.ack_commit().unwrap();
    core.ack_commit().unwrap(); // Idempotent, with no refresh/recreation.
    assert!(core.state().commit.is_empty());
    assert!(key(&mut core, 0x6e).unwrap());
    assert_eq!(trace.borrow().acknowledgements, 2);
    assert_eq!(trace.borrow().attempts, 1);
}

#[test]
fn paging_uses_global_snapshot_indices_but_local_selection() {
    let (mut core, trace) = setup();
    key(&mut core, 0x6e).unwrap();
    assert!(!core.state().last_page);
    key(&mut core, 0xff56).unwrap();
    assert_eq!(core.state().page, 1);
    assert_eq!(core.state().page_size, 2);
    assert!(core.state().last_page);
    assert_eq!(core.state().candidates[0].index, 2);
    assert!(core.select(2).is_err()); // Global index is not a page-local index.
    assert!(trace.borrow().selections.is_empty());
    key(&mut core, 0xff55).unwrap();
    assert_eq!(core.state().page, 0);
    key(&mut core, 0xff56).unwrap();
    core.select(1).unwrap();
    assert_eq!(trace.borrow().selections, vec![1]);
    assert_eq!(core.state().commit, "word-3");
}

#[test]
fn clear_discards_composition_and_pending_commit_without_ack() {
    let (mut core, trace) = setup();
    key(&mut core, 0x6e).unwrap();
    core.clear().unwrap();
    assert!(!core.state().active);
    assert!(core.state().candidates.is_empty());
    key(&mut core, 0x6e).unwrap();
    core.select(0).unwrap();
    core.clear().unwrap();
    assert!(core.state().commit.is_empty());
    assert!(key(&mut core, 0x6e).unwrap());
    key(&mut core, 0xff1b).unwrap();
    assert!(!core.state().active);
    assert_eq!(trace.borrow().clears, 2);
    assert_eq!(trace.borrow().acknowledgements, 0);
}

#[test]
fn initial_factory_failures_release_provisional_resources() {
    for failure in [Failure::Create, Failure::Configure, Failure::Snapshot, Failure::NonIdle] {
        let trace = Rc::new(RefCell::new(Trace::default()));
        trace.borrow_mut().failure = Some(failure);
        assert!(Core::new(factory(&trace), "initial").is_err());
        assert_eq!(trace.borrow().allocated, trace.borrow().dropped.len());
    }
    let trace = Rc::new(RefCell::new(Trace::default()));
    assert!(Core::new(factory(&trace), "").is_err());
    assert_eq!(trace.borrow().attempts, 0);
}

#[test]
fn failed_replacement_preserves_live_state_and_effective_config() {
    let (mut core, trace) = setup();
    core.set_config(profiles());
    core.apply_profile("Other.exe").unwrap();
    let effective = core.effective_config().unwrap().clone();
    let live_id = trace.borrow().allocated;
    for failure in [Failure::Create, Failure::Configure, Failure::Snapshot, Failure::NonIdle] {
        trace.borrow_mut().failure = Some(failure);
        assert!(core.apply_profile("EDITOR.EXE").is_err());
        assert_eq!(core.effective_config(), Some(&effective));
        assert_eq!(core.state().schema, "base");
        assert!(!trace.borrow().dropped.contains(&live_id));
        assert_eq!(trace.borrow().allocated - trace.borrow().dropped.len(), 1);
    }
    assert!(key(&mut core, 0x6e).unwrap()); // Old provider is still usable.
    core.clear().unwrap();
    assert!(!core.apply_profile("Editor.exe").unwrap());
    assert_eq!(core.state().schema, "editor");
    assert!(core.state().options["full_shape"]);
    assert!(trace.borrow().dropped.contains(&live_id));
    assert_eq!(trace.borrow().live_at_create.last(), Some(&1));
    let attempts = trace.borrow().attempts;
    core.apply_profile("EDITOR.EXE").unwrap();
    assert_eq!(trace.borrow().attempts, attempts);
    drop(core);
    assert_eq!(trace.borrow().allocated, trace.borrow().dropped.len());
}

#[test]
fn composition_and_pending_commit_prevent_changed_profiles() {
    let (mut core, trace) = setup();
    core.set_config(profiles());
    core.apply_profile("Other.exe").unwrap();
    let attempts = trace.borrow().attempts;
    key(&mut core, 0x6e).unwrap();
    assert!(core.apply_profile("Other.exe").unwrap()); // Unchanged profile is a no-op.
    assert!(core.apply_profile("Editor.exe").is_err());
    core.select(0).unwrap();
    assert!(core.apply_profile("Editor.exe").is_err());
    assert_eq!(core.state().commit, "word-0");
    assert_eq!(trace.borrow().attempts, attempts);
    core.ack_commit().unwrap();
    assert!(!core.apply_profile("Editor.exe").unwrap());
}

#[test]
fn staged_config_failure_retains_the_previous_effective_config() {
    let (mut core, trace) = setup();
    core.apply_profile("Other.exe").unwrap();
    let previous = core.effective_config().unwrap().clone();
    core.set_config(profiles());
    assert_eq!(core.effective_config(), Some(&previous));
    trace.borrow_mut().failure = Some(Failure::Snapshot);
    assert!(core.apply_profile("Editor.exe").is_err());
    assert_eq!(core.effective_config(), Some(&previous));
    assert_eq!(core.state().schema, "pinyin_simp");
    assert!(!core.apply_profile("Editor.exe").unwrap()); // Retry staged document.
}

#[test]
fn provider_errors_propagate_without_core_editing_the_snapshot() {
    let (mut core, trace) = setup();
    trace.borrow_mut().operation_failure = Some("process");
    assert!(!key(&mut core, 0x6e).unwrap_err().eaten);
    assert!(!core.state().active);
    trace.borrow_mut().operation_failure = None;
    key(&mut core, 0x6e).unwrap();
    trace.borrow_mut().operation_failure = Some("select");
    assert!(core.select(0).is_err());
    assert!(core.state().active);
    trace.borrow_mut().operation_failure = Some("clear");
    assert!(core.clear().is_err());
    assert!(core.state().active);
    trace.borrow_mut().operation_failure = None;
    core.select(0).unwrap();
    trace.borrow_mut().operation_failure = Some("ack_commit");
    assert!(core.ack_commit().is_err());
    assert_eq!(core.state().commit, "word-0");
    assert!(key(&mut core, 0x6e).is_err());
    trace.borrow_mut().operation_failure = None;
    core.ack_commit().unwrap();
    assert!(core.state().commit.is_empty());
}

#[test]
fn consumed_key_is_reported_when_a_provider_snapshot_fails() {
    let (mut core, trace) = setup();
    trace.borrow_mut().operation_failure = Some("snapshot");
    assert!(key(&mut core, 0x6e).unwrap_err().eaten);
    assert_eq!(trace.borrow().keys, vec![(0x6e, 0)]);
    trace.borrow_mut().operation_failure = None;
    core.clear().unwrap();
}

#[test]
fn destroying_one_core_does_not_release_another_providers_resources() {
    let trace = Rc::new(RefCell::new(Trace::default()));
    let first = Core::new(factory(&trace), "first").unwrap();
    let mut second = Core::new(factory(&trace), "second").unwrap();
    drop(first);
    assert_eq!(trace.borrow().dropped, vec![1]);
    assert_eq!(second.state().schema, "second");
    assert!(key(&mut second, 0x6e).unwrap());
    drop(second);
    assert_eq!(trace.borrow().dropped, vec![1, 2]);
}
