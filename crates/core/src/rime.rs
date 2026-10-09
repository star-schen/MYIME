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
    sync::atomic::{AtomicBool, AtomicI32, AtomicUsize, Ordering},
};
static RUNTIME: Mutex<Runtime> = Mutex::new(Runtime::new());
static SESSION_SEQUENCE: AtomicUsize = AtomicUsize::new(1);
static STORAGE_STATUS: AtomicI32 = AtomicI32::new(0);
static LEARNING_AVAILABLE: AtomicBool = AtomicBool::new(false);
pub fn storage_status() -> i32 { STORAGE_STATUS.swap(0, Ordering::Relaxed) }
pub fn learning_available() -> i32 {
    LEARNING_AVAILABLE.load(Ordering::Relaxed) as i32
}
struct NativeEvent { finished: bool }
impl NativeEvent {
    fn start(id: usize, application: &CStr, group: &CStr, habits: bool) -> Self {
        unsafe { rb_begin_event(id, application.as_ptr(), group.as_ptr(), habits as i32) };
        Self { finished: false }
    }
    fn finish(mut self) -> i32 {
        let status = unsafe { rb_end_event(1) };
        self.finished = true;
        if status != 0 { STORAGE_STATUS.store(status, Ordering::Relaxed); LEARNING_AVAILABLE.store(false, Ordering::Relaxed); }
        status
    }
}
impl Drop for NativeEvent {
    fn drop(&mut self) {
        if !self.finished { let status=unsafe { rb_end_event(0) }; if status!=0 { STORAGE_STATUS.store(status,Ordering::Relaxed); LEARNING_AVAILABLE.store(false,Ordering::Relaxed); } }
    }
}

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
    learning: String,
    create_learning: bool,
}
impl RimeFactory {
    pub fn new(shared: &str, user: &str) -> Self {
        Self { shared: shared.into(), user: user.into(), learning: std::path::Path::new(user).join("learning").to_string_lossy().into_owned(), create_learning: true }
    }
    pub fn shared(shared: &str, user: &str, learning: &str) -> Self {
        Self { shared: shared.into(), user: user.into(), learning: learning.into(), create_learning: false }
    }
}
impl InputProviderFactory for RimeFactory {
    fn create(&self, config: &EffectiveConfig) -> Result<Box<dyn InputProvider>, String> {
        let mut runtime = lock_runtime()?;
        if self.create_learning { std::fs::create_dir_all(&self.learning).map_err(|e|e.to_string())?; }
        runtime.0.initialize(&self.shared, &self.user, &self.learning, !self.create_learning)?;
        // Declared after the lock so failed schema/options/snapshot work destroys
        // the provisional session before the guard finalizes/unlocks the runtime.
        let (session, learning_ready) = Session::create(&config.schema, !self.create_learning)?;
        let event = session.event();
        for (name, value) in &config.options {
            session.option(name, *value)?;
        }
        let mut state = InputState::default();
        session.snapshot(&mut state)?;
        let status = event.finish();
        if status != 0 && self.create_learning { return Err("Learning storage initialization failed".into()); }
        if !learning_ready || status != 0 { LEARNING_AVAILABLE.store(false, Ordering::Relaxed); }
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
        let event = self.session.as_ref().unwrap().event();
        let eaten = self.session.as_ref().unwrap().key(key.keysym, key.modifiers);
        self.refresh().map_err(|message| ProcessError { message, eaten })?;
        // Input/commit delivery remains authoritative even if storage reports a
        // busy/error condition; the Host logs the status without replaying keys.
        event.finish();
        Ok(eaten)
    }
    fn state(&self) -> &InputState { &self.state }
    fn select(&mut self, page_index: usize) -> Result<(), String> {
        let _runtime = lock_runtime()?;
        let event = self.session.as_ref().unwrap().event();
        if !self.session.as_ref().unwrap().select(page_index) {
            return Err("Candidate selection failed".into());
        }
        self.refresh()?; event.finish(); Ok(())
    }
    fn clear(&mut self) -> Result<(), String> {
        let _runtime = lock_runtime()?;
        let event = self.session.as_ref().unwrap().event();
        self.session.as_ref().unwrap().clear();
        self.state.commit.clear();
        self.refresh()?;
        // Also discard a native commit left unread by a failed earlier snapshot.
        self.state.commit.clear();
        event.finish();
        Ok(())
    }
    fn ack_commit(&mut self) -> Result<(), String> {
        self.state.commit.clear();
        Ok(())
    }
    fn commit_current(&mut self) -> Result<(), String> {
        let _runtime = lock_runtime()?;
        let event = self.session.as_ref().unwrap().event();
        if unsafe { rb_commit_current(self.session.as_ref().unwrap().id) } == 0 { return Err("Rime composition commit failed".into()); }
        self.refresh()?; event.finish(); Ok(())
    }
    fn learning_context(&mut self, application: &str, group: &str, habits: bool) -> Result<(), String> {
        let application=CString::new(application).map_err(|e|e.to_string())?;
        let group=CString::new(group).map_err(|e|e.to_string())?;
        let session=self.session.as_mut().unwrap();
        session.application=application; session.group=group; session.habits=habits; Ok(())
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
    let learning=std::path::Path::new(user).join("learning");
    std::fs::create_dir_all(&learning).map_err(|e|e.to_string())?;
    runtime.0.initialize(shared, user, &learning.to_string_lossy(), false)?;
    runtime.0.deploy()
}
pub fn deploy_schema(shared: &str, user: &str, schema_file: &str) -> Result<(), String> {
    let file = CString::new(schema_file).map_err(|e| e.to_string())?;
    let mut runtime = lock_runtime()?;
    let learning=std::path::Path::new(user).join("learning");
    std::fs::create_dir_all(&learning).map_err(|e|e.to_string())?;
    runtime.0.initialize(shared, user, &learning.to_string_lossy(), false)?;
    runtime.0.deploy()?;
    if unsafe { rb_deploy_schema(file.as_ptr()) } == 0 {
        return Err("Explicit schema deployment failed".into());
    }
    Ok(())
}
extern "C" {
    fn rb_initialize(shared: *const c_char, user: *const c_char, learning: *const c_char, optional_learning: i32, first: i32) -> i32;
    fn rb_begin_event(id: usize, application: *const c_char, group: *const c_char, habits: i32);
    fn rb_end_event(success: i32) -> i32;
    fn rb_maintain_learning(operation: i32, argument: *const u16) -> i32;
    fn rb_finalize();
    fn rb_deploy() -> i32;
    fn rb_deploy_schema(file: *const c_char) -> i32;
    fn rb_create() -> usize;
    fn rb_destroy(id: usize);
    fn rb_key(id: usize, key: i32, mask: i32) -> i32;
    fn rb_select(id: usize, index: usize) -> i32;
    fn rb_commit_current(id: usize) -> i32;
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
    paths: Option<(CString, CString, CString, bool)>,
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
    fn initialize(&mut self, shared: &str, user: &str, learning: &str, optional_learning: bool) -> Result<(), String> {
        let paths = (
            CString::new(shared).map_err(|e| e.to_string())?,
            CString::new(user).map_err(|e| e.to_string())?,
            CString::new(learning).map_err(|e| e.to_string())?,
            optional_learning,
        );
        if self.initialized {
            return if self.paths.as_ref() == Some(&paths) {
                Ok(())
            } else {
                Err("Rime is already initialized with different directories".into())
            };
        }
        LEARNING_AVAILABLE.store(false, Ordering::Relaxed);
        STORAGE_STATUS.store(0, Ordering::Relaxed);
        self.paths = Some(paths);
        let (shared, user, learning, optional_learning) = self.paths.as_ref().unwrap();
        let result = unsafe { rb_initialize(shared.as_ptr(), user.as_ptr(), learning.as_ptr(), *optional_learning as i32, (!self.setup) as i32) };
        if result == 0 { return Err("Cannot initialize product librime or required learning directory".into()); }
        LEARNING_AVAILABLE.store(result == 1, Ordering::Relaxed);
        if result != 1 { STORAGE_STATUS.store(-1, Ordering::Relaxed); }
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
            LEARNING_AVAILABLE.store(false, Ordering::Relaxed);
        }
    }
}
struct Session {
    id: usize,
    schema: String,
    storage_id: usize,
    application: CString,
    group: CString,
    habits: bool,
}
impl Session {
    fn event(&self) -> NativeEvent { NativeEvent::start(self.storage_id,&self.application,&self.group,self.habits) }
    fn create(schema: &str, optional_learning: bool) -> Result<(Self, bool), String> {
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
        let storage_id=SESSION_SEQUENCE.fetch_add(1,Ordering::Relaxed);
        let application=CString::new("").unwrap(); let group=CString::new("daily").unwrap();
        let event=NativeEvent::start(storage_id,&application,&group,false);
        let id = unsafe { rb_create() };
        if id == 0 {
            return Err("Rime session creation failed; deploy data first".into());
        }
        let s = Self {
            id,
            schema: schema.into(),
            storage_id, application, group, habits: false,
        };
        if unsafe { rb_schema(id, schema_c.as_ptr()) } == 0 {
            return Err(format!("Schema is not deployed: {schema}"));
        }
        s.option("ascii_mode", false)?;
        let learning_ready = event.finish() == 0;
        if !learning_ready && !optional_learning { return Err("Cannot open required learning storage".into()); }
        Ok((s, learning_ready))
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
        let event=self.event(); unsafe { rb_destroy(self.id) }; event.finish();
    }
}
pub fn maintain_learning(shared: &str, user: &str, learning: &str, operation: i32, argument: &str) -> Result<i32,String> {
    let mut runtime=lock_runtime()?;
    if runtime.0.sessions!=0 { return Err("Learning maintenance requires an offline runtime".into()); }
    runtime.0.initialize(shared,user,learning,false)?;
    let argument:Vec<u16>=argument.encode_utf16().chain(std::iter::once(0)).collect();
    let application=CString::new("").unwrap(); let group=CString::new("daily").unwrap();
    let event=NativeEvent::start(0,&application,&group,false);
    let result=unsafe { rb_maintain_learning(operation,argument.as_ptr()) };
    let status=event.finish();
    if result < -2 || result==-1 || status!=0 { return Err("Shared learning maintenance failed; existing files were retained".into()); }
    Ok(result)
}
