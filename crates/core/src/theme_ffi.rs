//! Presentation-only C ABI. Unsafe is limited to pointers and ownership here.
use crate::{ffi::{boundary, string, Text}, theme::{self, ResolvedTheme}};
use std::{ffi::c_char, path::Path, ptr, thread::{self, ThreadId}};
pub struct ThemeHandle { owner: ThreadId, resolved: ResolvedTheme }
unsafe fn handle<'a>(p: *mut ThemeHandle) -> Result<&'a ThemeHandle, String> {
    let handle = p.as_ref().ok_or("Null theme handle")?;
    if handle.owner != thread::current().id() { return Err("Theme used from another thread".into()); }
    Ok(handle)
}
#[repr(C)]
pub struct ThemeView {
    id: Text, name: Text, version: Text, description: Text, font_family: Text, warning: Text,
    font_size: u32, font_weight: u32, horizontal: u32,
    padding_x: u32, padding_y: u32, candidate_padding_x: u32, candidate_padding_y: u32,
    spacing: u32, border_width: u32, border_radius: u32, min_width: u32, max_width: u32, caret_gap: u32,
    background: u32, text: u32, muted: u32, label: u32, highlight: u32, highlight_text: u32, border: u32,
    shadow: u32, shadow_size: u32, shadow_opacity: u32,
    preedit: u32, comments: u32, labels: u32, page_controls: u32,
}
#[no_mangle]
pub extern "C" fn myime_theme_abi_version() -> u32 { 1 }
#[no_mangle]
pub unsafe extern "C" fn myime_theme_resolve(
    id: *const c_char, user_root: *const c_char, installed_root: *const c_char,
    out: *mut *mut ThemeHandle,
) -> i32 {
    boundary(|| {
        if out.is_null() { return Err("Null theme output handle".into()); }
        *out = ptr::null_mut();
        let resolved = theme::resolve(string(id)?, Path::new(string(user_root)?), Path::new(string(installed_root)?));
        *out = Box::into_raw(Box::new(ThemeHandle { owner: thread::current().id(), resolved }));
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_theme_view(p: *mut ThemeHandle, out_size: usize, out: *mut ThemeView) -> i32 {
    boundary(|| {
        if out.is_null() || out_size != std::mem::size_of::<ThemeView>() { return Err("Theme view size/pointer mismatch".into()); }
        let resolved = &handle(p)?.resolved; let t = &resolved.theme;
        ptr::write(out, ThemeView {
            id: t.id.as_str().into(), name: t.name.as_str().into(), version: t.version.as_str().into(),
            description: t.description.as_str().into(), font_family: t.font_family.as_str().into(),
            warning: resolved.warning.as_str().into(), font_size: t.font_size, font_weight: t.font_weight,
            horizontal: t.horizontal as u32, padding_x: t.padding_x, padding_y: t.padding_y,
            candidate_padding_x: t.candidate_padding_x, candidate_padding_y: t.candidate_padding_y,
            spacing: t.spacing, border_width: t.border_width, border_radius: t.border_radius,
            min_width: t.min_width, max_width: t.max_width, caret_gap: t.caret_gap,
            background: t.background, text: t.text, muted: t.muted, label: t.label,
            highlight: t.highlight, highlight_text: t.highlight_text, border: t.border,
            shadow: t.shadow as u32, shadow_size: t.shadow_size, shadow_opacity: t.shadow_opacity,
            preedit: t.preedit as u32, comments: t.comments as u32, labels: t.labels as u32,
            page_controls: t.page_controls as u32,
        });
        Ok(())
    })
}
#[no_mangle]
pub unsafe extern "C" fn myime_theme_destroy(p: *mut ThemeHandle) -> i32 {
    boundary(|| { handle(p)?; drop(Box::from_raw(p)); Ok(()) })
}
