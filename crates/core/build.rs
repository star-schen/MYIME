use std::{env, path::PathBuf, process::Command};
fn main() {
    if env::var_os("CARGO_FEATURE_RIME").is_none() {
        return;
    }
    let root = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap()).join("../..");
    let rime = env::var_os("RIME_ROOT")
        .map(PathBuf::from)
        .unwrap_or_else(|| root.join(".deps/rime-shared/dist"));
    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let obj = out.join("rime_bridge.obj");
    assert!(Command::new("cl.exe")
        .args(["/nologo", "/c", "/EHsc", "/std:c++17", "/MD", "/utf-8"])
        .arg(format!("/I{}", rime.join("include").display()))
        .arg(format!("/Fo{}", obj.display()))
        .arg("native/rime_bridge.cpp")
        .status()
        .expect("Run scripts/build.ps1 to load MSVC")
        .success());
    assert!(Command::new("lib.exe")
        .arg("/nologo")
        .arg(format!("/OUT:{}", out.join("rime_bridge.lib").display()))
        .arg(obj)
        .status()
        .unwrap()
        .success());
    println!("cargo:rustc-link-search=native={}", out.display());
    println!(
        "cargo:rustc-link-search=native={}",
        rime.join("lib").display()
    );
    println!("cargo:rustc-link-lib=static=rime_bridge");
    println!("cargo:rustc-link-lib=dylib=rime");
    println!("cargo:rerun-if-changed=native/rime_bridge.cpp");
    // A rebuilt SDK stays at the same path; do not reuse a stale bridge/link.
    println!("cargo:rerun-if-changed={}", rime.join("include").display());
    println!("cargo:rerun-if-changed={}", rime.join("lib/rime.lib").display());
    println!("cargo:rerun-if-env-changed=RIME_ROOT");
}
