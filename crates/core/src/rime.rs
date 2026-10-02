//! Rime adapter owns native sessions, the process lock and runtime leases.
//! Session/Runtime stay private: no native call can bypass this boundary.
use crate::{
    config::EffectiveConfig,
    extensions::{InputProvider, InputProviderFactory, KeyEvent, ProcessError},
    model::{Candidate, InputState},
};
use std::{
    ffi::{c_char, c_void, CStr, CString},
    marker::PhantomData,
    rc::Rc,
    sync::{Mutex, MutexGuard},
};
static RUNTIME: Mutex<Runtime> = Mutex::new(Runtime::new());

// Finalize only after all native sessions have been destroyed, including on
// constructor/deployment failure or Rust unwind. The guard still owns the lock.
struct RuntimeLock(MutexGuard<'static, Runtime>);
impl Drop for RuntimeLock {
    fn drop(&mut self) {
        self.0.finalize();
    }
}
fn lock_runtime() -> Result<RuntimeLock, String> {
    RUNTIME.lock().map(RuntimeLock).map_err(|_| "Rime runtime poisoned".into())
}

pub struct RimeFactory {
    shared: String,
    user: String,
}
impl RimeFactory {
    pub fn new(shared: &str, user: &str) -> Self {
        Self { shared: shared.into(), user: user.into() }
    }
}
impl InputProviderFactory for RimeFactory {
    fn create(&self, config: &EffectiveConfig) -> Result<Box<dyn InputProvider>, String> {
        let mut runtime = lock_runtime()?;
        runtime.0.initialize(&self.shared, &self.user)?;
        // Declared after the lock so failed schema/options/snapshot work destroys
        // the provisional session before the guard finalizes/unlocks the runtime.
        let session = Session::create(&config.schema)?;
        for (name, value) in &config.options {
            session.option(name, *value)?;
        }
        let mut state = InputState::default();
        session.snapshot(&mut state)?;
        runtime.0.sessions += 1;
        Ok(Box::new(RimeProvider { session: Some(session), state, affinity: PhantomData }))
    }
}

struct RimeProvider {
    session: Option<Session>, // Taken in Drop while the process lock is held.
    state: InputState,
    affinity: PhantomData<Rc<()>>,
}
impl RimeProvider {
    // Only called under RuntimeLock. Publish a complete snapshot on success;
    // reading state never drains native commits or creates another provider.
    fn refresh(&mut self) -> Result<(), String> {
        let mut state = InputState { commit: self.state.commit.clone(), ..InputState::default() };
        self.session.as_ref().unwrap().snapshot(&mut state)?;
        self.state = state;
        Ok(())
    }
}
impl InputProvider for RimeProvider {
    fn process(&mut self, key: KeyEvent) -> Result<bool, ProcessError> {
        let _runtime = lock_runtime()?;
        let eaten = self.session.as_ref().unwrap().key(key.keysym, key.modifiers);
        self.refresh().map_err(|message| ProcessError { message, eaten })?;
        Ok(eaten)
    }
    fn state(&self) -> &InputState { &self.state }
    fn select(&mut self, page_index: usize) -> Result<(), String> {
        let _runtime = lock_runtime()?;
        if !self.session.as_ref().unwrap().select(page_index) {
            return Err("Candidate selection failed".into());
        }
        self.refresh()
    }
    fn clear(&mut self) -> Result<(), String> {
        let _runtime = lock_runtime()?;
        self.session.as_ref().unwrap().clear();
        self.state.commit.clear();
        self.refresh()?;
        // Also discard a native commit left unread by a failed earlier snapshot.
        self.state.commit.clear();
        Ok(())
    }
    fn ack_commit(&mut self) -> Result<(), String> {
        self.state.commit.clear();
        Ok(())
    }
}
impl Drop for RimeProvider {
    fn drop(&mut self) {
        // Mutations reject a poisoned runtime. Cleanup must still release the
        // session/lease after an unwind, and must never panic in Drop.
        let mut runtime = RuntimeLock(RUNTIME.lock().unwrap_or_else(|e| e.into_inner()));
        drop(self.session.take());
        runtime.0.sessions -= 1;
    }
}

pub fn deploy(shared: &str, user: &str) -> Result<(), String> {
    let mut runtime = lock_runtime()?;
    runtime.0.initialize(shared, user)?;
    runtime.0.deploy()
}
pub fn deploy_schema(shared: &str, user: &str, schema_file: &str) -> Result<(), String> {
    let file = CString::new(schema_file).map_err(|e| e.to_string())?;
    let mut runtime = lock_runtime()?;
    runtime.0.initialize(shared, user)?;
    runtime.0.deploy()?;
    if unsafe { rb_deploy_schema(file.as_ptr()) } == 0 {
        return Err("Explicit schema deployment failed".into());
    }
    Ok(())
}
extern "C" {
    fn rb_initialize(shared: *const c_char, user: *const c_char, first: i32) -> i32;
    fn rb_finalize();
    fn rb_deploy() -> i32;
    fn rb_deploy_schema(file: *const c_char) -> i32;
    fn rb_create() -> usize;
    fn rb_destroy(id: usize);
    fn rb_key(id: usize, key: i32, mask: i32) -> i32;
    fn rb_select(id: usize, index: usize) -> i32;
    fn rb_schema(id: usize, schema: *const c_char) -> i32;
    fn rb_schema_config(schema: *const c_char) -> *mut c_void;
    fn rb_config_string(p: *mut c_void, key: *const c_char) -> *const c_char;
    fn rb_config_list_size(p: *mut c_void, key: *const c_char) -> usize;
    fn rb_free_config(p: *mut c_void);
    fn rb_option(id: usize, name: *const c_char, value: i32);
    fn rb_get_option(id: usize, name: *const c_char) -> i32;
    fn rb_clear(id: usize);
    fn rb_context(id: usize) -> *mut c_void;
    fn rb_free_context(p: *mut c_void);
    fn rb_preedit(p: *mut c_void) -> *const c_char;
    fn rb_caret(p: *mut c_void) -> i32;
    fn rb_count(p: *mut c_void) -> i32;
    fn rb_selected(p: *mut c_void) -> i32;
    fn rb_page(p: *mut c_void) -> i32;
    fn rb_page_size(p: *mut c_void) -> i32;
    fn rb_last_page(p: *mut c_void) -> i32;
    fn rb_candidate(p: *mut c_void, i: usize) -> *const c_char;
    fn rb_comment(p: *mut c_void, i: usize) -> *const c_char;
    fn rb_label(p: *mut c_void, i: usize) -> *const c_char;
    fn rb_select_key(p: *mut c_void, i: usize) -> c_char;
    fn rb_commit(id: usize) -> *mut c_void;
    fn rb_commit_text(p: *mut c_void) -> *const c_char;
    fn rb_free_commit(p: *mut c_void);
}
unsafe fn text(p: *const c_char) -> String {
    if p.is_null() {
        String::new()
    } else {
        CStr::from_ptr(p).to_string_lossy().into_owned()
    }
}
struct Runtime {
    initialized: bool,
    setup: bool,
    sessions: usize,
    paths: Option<(CString, CString)>,
}
impl Runtime {
    const fn new() -> Self {
        Self {
            initialized: false,
            setup: false,
            sessions: 0,
            paths: None,
        }
    }
    fn initialize(&mut self, shared: &str, user: &str) -> Result<(), String> {
        let paths = (
            CString::new(shared).map_err(|e| e.to_string())?,
            CString::new(user).map_err(|e| e.to_string())?,
        );
        if self.initialized {
            return if self.paths.as_ref() == Some(&paths) {
                Ok(())
            } else {
                Err("Rime is already initialized with different directories".into())
            };
        }
        self.paths = Some(paths);
        let (shared, user) = self.paths.as_ref().unwrap();
        if unsafe { rb_initialize(shared.as_ptr(), user.as_ptr(), (!self.setup) as i32) } == 0 {
            return Err("Unsupported librime API".into());
        }
        self.setup = true;
        self.initialized = true;
        Ok(())
    }
    fn deploy(&mut self) -> Result<(), String> {
        if self.sessions != 0 {
            return Err("Deployment requires an offline runtime".into());
        }
        if unsafe { rb_deploy() } == 0 {
            return Err("Rime maintenance failed".into());
        }
        Ok(())
    }
    fn finalize(&mut self) {
        if self.initialized && self.sessions == 0 {
            unsafe { rb_finalize() };
            self.initialized = false;
        }
    }
}
struct Session {
    id: usize,
    schema: String,
}
impl Session {
    fn create(schema: &str) -> Result<Self, String> {
        let schema_c = CString::new(schema).map_err(|e| e.to_string())?;
        // select_schema accepts an unknown id and schema_open can return an
        // empty config. Check Rime's deployed configuration before selection,
        // so a failed profile cannot replace a working session or save a bad id.
        unsafe {
            struct Config(*mut c_void);
            impl Drop for Config {
                fn drop(&mut self) {
                    unsafe { rb_free_config(self.0) }
                }
            }
            let p = rb_schema_config(schema_c.as_ptr());
            if p.is_null() {
                return Err(format!("Cannot open deployed schema: {schema}"));
            }
            let _owner = Config(p);
            let id = rb_config_string(p, b"schema/schema_id\0".as_ptr().cast());
            if id.is_null() || CStr::from_ptr(id) != schema_c.as_c_str() {
                return Err(format!("Schema is not deployed: {schema}"));
            }
            if rb_config_list_size(p, b"engine/processors\0".as_ptr().cast()) == 0 {
                return Err(format!("Schema has no input processors: {schema}"));
            }
        }
        let id = unsafe { rb_create() };
        if id == 0 {
            return Err("Rime session creation failed; deploy data first".into());
        }
        let s = Self {
            id,
            schema: schema.into(),
        };
        if unsafe { rb_schema(id, schema_c.as_ptr()) } == 0 {
            return Err(format!("Schema is not deployed: {schema}"));
        }
        s.option("ascii_mode", false)?;
        Ok(s)
    }
    fn key(&self, key: i32, mask: i32) -> bool {
        unsafe { rb_key(self.id, key, mask) != 0 }
    }
    fn select(&self, index: usize) -> bool {
        unsafe { rb_select(self.id, index) != 0 }
    }
    fn clear(&self) {
        unsafe { rb_clear(self.id) }
    }
    fn option(&self, name: &str, value: bool) -> Result<(), String> {
        let name = CString::new(name).map_err(|e| e.to_string())?;
        unsafe { rb_option(self.id, name.as_ptr(), value as i32) };
        Ok(())
    }
    fn snapshot(&self, state: &mut InputState) -> Result<(), String> {
        unsafe {
            struct Context(*mut c_void);
            impl Drop for Context {
                fn drop(&mut self) {
                    unsafe { rb_free_context(self.0) }
                }
            }
            let p = rb_context(self.id);
            if p.is_null() {
                return Err("Cannot read Rime context".into());
            }
            let _owner = Context(p);
            state.preedit = text(rb_preedit(p));
            state.caret = (rb_caret(p).max(0) as usize).min(state.preedit.len());
            state.active = !state.preedit.is_empty();
            state.selected = rb_selected(p).max(0) as usize;
            state.page = rb_page(p).max(0) as usize;
            state.page_size = rb_page_size(p).max(0) as usize;
            state.last_page = rb_last_page(p) != 0;
            state.candidates.clear();
            for i in 0..rb_count(p).max(0) as usize {
                let mut label = text(rb_label(p, i));
                if label.is_empty() {
                    let key = rb_select_key(p, i) as u8;
                    label = if key != 0 {
                        (key as char).to_string()
                    } else {
                        ((i + 1) % 10).to_string()
                    };
                }
                state.candidates.push(Candidate {
                    text: text(rb_candidate(p, i)),
                    comment: text(rb_comment(p, i)),
                    label,
                    index: state.page * state.page_size + i,
                });
            }
            state.schema.clone_from(&self.schema);
            for name in ["ascii_mode", "full_shape", "simplification", "ascii_punct"] {
                let c = CString::new(name).unwrap();
                state
                    .options
                    .insert(name.into(), rb_get_option(self.id, c.as_ptr()) != 0);
            }
            let commit = rb_commit(self.id);
            if !commit.is_null() {
                struct Commit(*mut c_void);
                impl Drop for Commit {
                    fn drop(&mut self) {
                        unsafe { rb_free_commit(self.0) }
                    }
                }
                let _owner = Commit(commit);
                state.commit.push_str(&text(rb_commit_text(commit)));
            }
        }
        Ok(())
    }
}
impl Drop for Session {
    fn drop(&mut self) {
        unsafe { rb_destroy(self.id) }
    }
}
