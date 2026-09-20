//! All unsafe librime calls are contained here; callers hold the process lock.
use crate::model::{Candidate, InputState};
use std::ffi::{c_char, c_void, CStr, CString};
extern "C" {
    fn rb_initialize(shared: *const c_char, user: *const c_char, first: i32) -> i32;
    fn rb_finalize();
    fn rb_deploy() -> i32;
    fn rb_create() -> usize;
    fn rb_destroy(id: usize);
    fn rb_key(id: usize, key: i32, mask: i32) -> i32;
    fn rb_select(id: usize, index: usize) -> i32;
    fn rb_schema(id: usize, schema: *const c_char) -> i32;
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
pub struct Runtime {
    initialized: bool,
    setup: bool,
    pub sessions: usize,
    paths: Option<(CString, CString)>,
}
impl Runtime {
    pub const fn new() -> Self {
        Self {
            initialized: false,
            setup: false,
            sessions: 0,
            paths: None,
        }
    }
    pub fn initialize(&mut self, shared: &str, user: &str) -> Result<(), String> {
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
    pub fn deploy(&mut self) -> Result<(), String> {
        if self.sessions != 0 {
            return Err("Deployment requires an offline runtime".into());
        }
        if unsafe { rb_deploy() } == 0 {
            return Err("Rime maintenance failed".into());
        }
        Ok(())
    }
    pub fn finalize(&mut self) {
        if self.initialized && self.sessions == 0 {
            unsafe { rb_finalize() };
            self.initialized = false;
        }
    }
}
pub struct Session {
    id: usize,
    pub schema: String,
}
impl Session {
    pub fn create(schema: &str) -> Result<Self, String> {
        let schema_c = CString::new(schema).map_err(|e| e.to_string())?;
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
    pub fn key(&self, key: i32, mask: i32) -> bool {
        unsafe { rb_key(self.id, key, mask) != 0 }
    }
    pub fn select(&self, index: usize) -> bool {
        unsafe { rb_select(self.id, index) != 0 }
    }
    pub fn clear(&self) {
        unsafe { rb_clear(self.id) }
    }
    pub fn option(&self, name: &str, value: bool) -> Result<(), String> {
        let name = CString::new(name).map_err(|e| e.to_string())?;
        unsafe { rb_option(self.id, name.as_ptr(), value as i32) };
        Ok(())
    }
    pub fn snapshot(&self, state: &mut InputState) -> Result<(), String> {
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
