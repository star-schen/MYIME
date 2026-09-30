use crate::{
    core::Core as InputCore,
    extensions::KeyEvent,
    rime::{self, RimeFactory},
};
use std::{
    cell::RefCell,
    ffi::{c_char, CStr, CString},
    panic::{catch_unwind, AssertUnwindSafe},
    ptr,
    thread::{self, ThreadId},
};
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
    inner: InputCore,
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
        let factory = Box::new(RimeFactory::new(string(shared)?, string(user)?));
        let c = Box::new(Core {
            owner: thread::current().id(),
            inner: InputCore::new(factory, string(schema)?)?,
        });
        *out = Box::into_raw(c);
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_destroy(p: *mut Core) -> i32 {
    boundary(|| {
        core(p)?;
        drop(Box::from_raw(p));
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_deploy(shared: *const c_char, user: *const c_char) -> i32 {
    boundary(|| rime::deploy(string(shared)?, string(user)?))
}
#[no_mangle]
pub unsafe extern "C" fn myime_key(p: *mut Core, key: i32, mask: i32, eaten: *mut u32) -> i32 {
    boundary(|| {
        if eaten.is_null() {
            return Err("Null eaten output".into());
        }
        *eaten = 0;
        *eaten = core(p)?.inner.process(KeyEvent { keysym: key, modifiers: mask })
            .map_err(|error| { *eaten = error.eaten as u32; error.message })? as u32;
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_select(p: *mut Core, index: usize) -> i32 {
    boundary(|| core(p)?.inner.select(index))
}
#[no_mangle]
pub unsafe extern "C" fn myime_clear(p: *mut Core) -> i32 {
    boundary(|| core(p)?.inner.clear())
}
#[no_mangle]
pub unsafe extern "C" fn myime_ack_commit(p: *mut Core) -> i32 {
    boundary(|| core(p)?.inner.ack_commit())
}
#[no_mangle]
pub unsafe extern "C" fn myime_load_config(p: *mut Core, path: *const c_char) -> i32 {
    boundary(|| core(p)?.inner.load_config(std::path::Path::new(string(path)?)))
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
        *enabled = core(p)?.inner.apply_profile(string(executable)?)? as u32;
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_state(p: *mut Core, out: *mut State) -> i32 {
    boundary(|| {
        if out.is_null() {
            return Err("Null state output".into());
        }
        let s = core(p)?.inner.state();
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
            .inner.state()
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
