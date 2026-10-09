//! Bounded UI pack snapshots. Called at activation/settings refresh, never keys.
use crate::{ffi::{boundary, string, Text}, ui_language::UiLanguage};
use std::{ffi::c_char, path::Path, ptr, thread::{self, ThreadId}};
pub struct UiLanguageHandle { owner: ThreadId, language: UiLanguage }
unsafe fn handle<'a>(pointer: *mut UiLanguageHandle) -> Result<&'a UiLanguageHandle, String> {
    let handle = pointer.as_ref().ok_or("Null UI language handle")?;
    if handle.owner != thread::current().id() { return Err("UI language used from another thread".into()); }
    Ok(handle)
}
#[no_mangle]
pub extern "C" fn myime_ui_language_abi_version() -> u32 { 1 }
#[no_mangle]
pub unsafe extern "C" fn myime_ui_language_load(user_root: *const c_char, installed_root: *const c_char, fallback: *const c_char, out: *mut *mut UiLanguageHandle) -> i32 {
    boundary(|| {
        if out.is_null() { return Err("Null UI language output".into()); }
        *out = ptr::null_mut();
        let language = UiLanguage::load(Path::new(string(user_root)?), Path::new(string(installed_root)?), string(fallback)?)?;
        *out = Box::into_raw(Box::new(UiLanguageHandle { owner: thread::current().id(), language }));
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_ui_language_text(pointer: *mut UiLanguageHandle, key: *const c_char, out: *mut Text) -> i32 {
    boundary(|| {
        if out.is_null() { return Err("Null UI text output".into()); }
        ptr::write(out, handle(pointer)?.language.text(string(key)?)?.into());
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_ui_language_destroy(pointer: *mut UiLanguageHandle) -> i32 {
    boundary(|| { handle(pointer)?; drop(Box::from_raw(pointer)); Ok(()) })
}
