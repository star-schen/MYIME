//! Resolve containment using the opened object, then read that same handle.
//! NT volume paths avoid AppContainer denial while resolving a DOS drive name.
use std::{fs::{File, OpenOptions}, path::Path};
#[cfg(windows)]
fn final_path(file: &File) -> Result<std::path::PathBuf, String> {
    use std::os::windows::{ffi::OsStringExt, io::AsRawHandle};
    use windows_sys::Win32::Storage::FileSystem::GetFinalPathNameByHandleW;
    let mut buffer = vec![0u16; 32768];
    let count = unsafe { GetFinalPathNameByHandleW(file.as_raw_handle(), buffer.as_mut_ptr(), buffer.len() as u32, 2) };
    if count == 0 { return Err(std::io::Error::last_os_error().to_string()); }
    if count as usize >= buffer.len() { return Err("Catalog object path is too long".into()); }
    Ok(std::ffi::OsString::from_wide(&buffer[..count as usize]).into())
}
pub fn open(root: &Path, file: &Path) -> Result<File, String> {
    let opened = File::open(file).map_err(|e| e.to_string())?;
    if !opened.metadata().map_err(|e| e.to_string())?.is_file() { return Err("Catalog entry is not a regular file".into()); }
    #[cfg(windows)] {
        use std::os::windows::fs::OpenOptionsExt;
        let directory = OpenOptions::new().access_mode(0x80).custom_flags(0x02000000).open(root).map_err(|e| e.to_string())?;
        if !final_path(&opened)?.starts_with(final_path(&directory)?) { return Err("Theme path leaves its catalog".into()); }
    }
    #[cfg(not(windows))] {
        if !file.canonicalize().map_err(|e| e.to_string())?.starts_with(root.canonicalize().map_err(|e| e.to_string())?) { return Err("Theme path leaves its catalog".into()); }
    }
    Ok(opened)
}
