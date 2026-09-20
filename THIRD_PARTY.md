# Third-party components

The repository's MPL-2.0 license applies to MYIME source, not to downloaded dependencies.

- [librime 1.17.0](https://github.com/rime/librime/tree/1.17.0): official C++ engine and C API, BSD-3-Clause. The downloaded official MSVC package also contains upstream plugins and statically linked dependencies; see its `version-info.txt` and upstream build/license material.
- [rime-pinyin-simp](https://github.com/rime/rime-pinyin-simp): Apache-2.0 data; exact revision in `dependencies.lock.json`.
- [rime-prelude](https://github.com/rime/rime-prelude), [rime-stroke](https://github.com/rime/rime-stroke), [rime-essay](https://github.com/rime/rime-essay): upstream LICENSE files are LGPL-3.0; preserve AUTHORS and data notices.
- [CMake](https://github.com/Kitware/CMake): BSD-3-Clause; downloaded as a development tool, not included in the IME package.
- Rust TOML parser and transitive crates: exact versions and checksums in `Cargo.lock`; source licenses accompany the crates in Cargo's registry cache.

`bootstrap.ps1` fetches dependencies from their official repositories and checks the recorded binary hashes. Third-party source/data stays in ignored `.deps`; MYIME does not modify librime internals. `package.ps1` copies data LICENSE/AUTHORS into notices and preserves dependency revision metadata.

The generated package is for local development. A public binary release still needs a complete upstream/transitive notice bundle, corresponding-source arrangements for included data, versioned build provenance, signing and installer/runtime checks. This task does not publish binary releases.
