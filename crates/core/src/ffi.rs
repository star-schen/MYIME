use crate::{
    config::{Config, EffectiveConfig},
    model::InputState,
    rime::{Runtime, Session},
};
use std::{
    cell::RefCell,
    ffi::{c_char, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    sync::Mutex,
    thread::{self, ThreadId},
};
static RUNTIME: Mutex<Runtime> = Mutex::new(Runtime::new());
thread_local! { static ERROR: RefCell<CString> = RefCell::new(CString::new("").unwrap()); }
fn boundary(f: impl FnOnce() -> Result<(), String>) -> i32 {
    match catch_unwind(AssertUnwindSafe(f)) {
        Ok(Ok(())) => 0,
        result => {
            let message = match result {
                Ok(Err(e)) => e,
                _ => "Rust panic contained at ABI boundary".into(),
            };
            ERROR.with(|e| *e.borrow_mut() = CString::new(message.replace('\0', "?")).unwrap());
            -1
        }
    }
}
unsafe fn string<'a>(p: *const c_char) -> Result<&'a str, String> {
    if p.is_null() {
        return Err("Null string argument".into());
    }
    CStr::from_ptr(p).to_str().map_err(|e| e.to_string())
}
pub struct Core {
    owner: ThreadId,
    session: Session,
    state: InputState,
    config: Config,
    effective: Option<EffectiveConfig>,
}
impl Core {
    fn refresh(&mut self) -> Result<(), String> {
        self.session.snapshot(&mut self.state)
    }
}
unsafe fn core<'a>(p: *mut Core) -> Result<&'a mut Core, String> {
    let c = p.as_mut().ok_or("Null handle")?;
    if c.owner != thread::current().id() {
        return Err("Session used from a different thread".into());
    }
    Ok(c)
}
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Text {
    data: *const u8,
    len: usize,
}
impl From<&str> for Text {
    fn from(s: &str) -> Self {
        Self {
            data: s.as_ptr(),
            len: s.len(),
        }
    }
}
#[repr(C)]
pub struct State {
    preedit: Text,
    commit: Text,
    schema: Text,
    caret: usize,
    count: usize,
    selected: usize,
    page: usize,
    page_size: usize,
    active: u32,
    last_page: u32,
    options: u32,
}
#[repr(C)]
pub struct Candidate {
    text: Text,
    comment: Text,
    label: Text,
    index: usize,
}
#[no_mangle]
pub extern "C" fn myime_abi_version() -> u32 {
    1
}
#[no_mangle]
pub extern "C" fn myime_last_error() -> *const c_char {
    ERROR.with(|e| e.borrow().as_ptr())
}
#[no_mangle]
pub unsafe extern "C" fn myime_create(
    shared: *const c_char,
    user: *const c_char,
    schema: *const c_char,
    out: *mut *mut Core,
) -> i32 {
    boundary(|| {
        if out.is_null() {
            return Err("Null output handle".into());
        }
        *out = ptr::null_mut();
        let mut runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
        runtime.initialize(string(shared)?, string(user)?)?;
        let result = (|| {
            let mut c = Box::new(Core {
                owner: thread::current().id(),
                session: Session::create(string(schema)?)?,
                state: InputState::default(),
                config: Config::default(),
                effective: None,
            });
            c.refresh()?;
            runtime.sessions += 1;
            *out = Box::into_raw(c);
            Ok(())
        })();
        if result.is_err() {
            runtime.finalize();
        }
        result
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_destroy(p: *mut Core) -> i32 {
    boundary(|| {
        core(p)?;
        let mut runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
        drop(Box::from_raw(p));
        runtime.sessions -= 1;
        runtime.finalize();
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_deploy(shared: *const c_char, user: *const c_char) -> i32 {
    boundary(|| {
        let mut runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
        runtime.initialize(string(shared)?, string(user)?)?;
        let result = runtime.deploy();
        runtime.finalize();
        result
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_key(p: *mut Core, key: i32, mask: i32, eaten: *mut u32) -> i32 {
    boundary(|| {
        if eaten.is_null() {
            return Err("Null eaten output".into());
        }
        *eaten = 0;
        let c = core(p)?;
        if !c.state.commit.is_empty() {
            return Err("Acknowledge pending commit before another key".into());
        }
        let _runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
        *eaten = c.session.key(key, mask) as u32;
        c.refresh()
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_select(p: *mut Core, index: usize) -> i32 {
    boundary(|| {
        let c = core(p)?;
        if index >= c.state.candidates.len() || !c.state.commit.is_empty() {
            return Err("Invalid candidate or pending commit".into());
        }
        let _runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
        if !c.session.select(index) {
            return Err("Candidate selection failed".into());
        }
        c.refresh()
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_clear(p: *mut Core) -> i32 {
    boundary(|| {
        let c = core(p)?;
        let _runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
        c.session.clear();
        c.state.commit.clear();
        c.refresh()
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_ack_commit(p: *mut Core) -> i32 {
    boundary(|| {
        core(p)?.state.commit.clear();
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_load_config(p: *mut Core, path: *const c_char) -> i32 {
    boundary(|| {
        let c = core(p)?;
        c.config = Config::read(std::path::Path::new(string(path)?))?;
        c.effective = None;
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_apply_profile(
    p: *mut Core,
    executable: *const c_char,
    enabled: *mut u32,
) -> i32 {
    boundary(|| {
        if enabled.is_null() {
            return Err("Null enabled output".into());
        }
        let c = core(p)?;
        let effective = c
            .config
            .effective(string(executable)?, &toml::Table::new())?;
        if c.effective.as_ref() != Some(&effective) {
            if c.state.active || !c.state.commit.is_empty() {
                return Err("Finish composition before switching profile".into());
            }
            let _runtime = RUNTIME.lock().map_err(|_| "Rime runtime poisoned")?;
            let session = Session::create(&effective.schema)?;
            for (name, value) in &effective.options {
                session.option(name, *value)?;
            }
            c.session = session;
            c.refresh()?;
        }
        *enabled = effective.enabled as u32;
        c.effective = Some(effective);
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_state(p: *mut Core, out: *mut State) -> i32 {
    boundary(|| {
        if out.is_null() {
            return Err("Null state output".into());
        }
        let s = &core(p)?.state;
        let mut options = 0;
        for (i, name) in ["ascii_mode", "full_shape", "simplification", "ascii_punct"]
            .iter()
            .enumerate()
        {
            if s.options.get(*name).copied().unwrap_or(false) {
                options |= 1 << i;
            }
        }
        ptr::write(
            out,
            State {
                preedit: s.preedit.as_str().into(),
                commit: s.commit.as_str().into(),
                schema: s.schema.as_str().into(),
                caret: s.caret,
                count: s.candidates.len(),
                selected: s.selected,
                page: s.page,
                page_size: s.page_size,
                active: s.active as u32,
                last_page: s.last_page as u32,
                options,
            },
        );
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_candidate(p: *mut Core, index: usize, out: *mut Candidate) -> i32 {
    boundary(|| {
        if out.is_null() {
            return Err("Null candidate output".into());
        }
        let c = core(p)?
            .state
            .candidates
            .get(index)
            .ok_or("Candidate index out of range")?;
        ptr::write(
            out,
            Candidate {
                text: c.text.as_str().into(),
                comment: c.comment.as_str().into(),
                label: c.label.as_str().into(),
                index: c.index,
            },
        );
        Ok(())
    })
}
