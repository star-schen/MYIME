# Third-party components

MYIME source is MPL-2.0; downloaded dependencies and data retain their own licenses.

- [librime 1.17.0](https://github.com/rime/librime/tree/33e78140250125871856cdc5b42ddc6a5fcd3cd4): BSD-3-Clause. MYIME builds this pinned official source with its shared user-storage module. The guarded patch adds session-specific storage handles and scopes around the existing UserDictionary implementation; the original ranking/learning algorithm body is retained. This is a product source build, not the unmodified official prebuilt SDK.
- Static libraries obtained through pinned [vcpkg](https://github.com/microsoft/vcpkg/tree/ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0): Boost components (BSL-1.0), yaml-cpp (MIT), LevelDB and marisa-trie (BSD-3-Clause), OpenCC (Apache-2.0), and the ports' transitive dependencies. Exact port set and revision are in dependencies.lock.json. package.ps1 copies installed port copyright bundles into notices/dependencies.
- Windows winsqlite3 is an operating-system component used through the SDK/import library; no separate SQLite binary is bundled. The implementation uses its [WAL](https://www.sqlite.org/wal.html) and online backup interfaces.
- [rime-pinyin-simp](https://github.com/rime/rime-pinyin-simp): Apache-2.0 data.
- [rime-prelude](https://github.com/rime/rime-prelude), [rime-stroke](https://github.com/rime/rime-stroke), [rime-essay](https://github.com/rime/rime-essay): upstream LICENSE files identify LGPL-3.0. Data commits are pinned; preserve LICENSE/AUTHORS and relevant notices.
- [CMake](https://github.com/Kitware/CMake): BSD-3-Clause. Downloaded as a development tool, not included in the IME package.
- Rust TOML parser/editor, Serde/JSON, CSV, SHA-256, YAML parser, Windows bindings and transitive crates: exact versions and checksums are in Cargo.lock. The independent JSON tool does not link the Rime runtime.

bootstrap.ps1 checks recorded CMake hashes and pinned data revisions. build-rime.ps1 fetches exact source/dependency commits into ignored .deps and applies only exact-match source patches; upstream mismatch stops the build. The local package includes librime LICENSE, data LICENSE/AUTHORS, and available installed vcpkg notice bundles.

The generated package remains for local development. A public binary release requires a complete Rust/transitive notice bundle, corresponding-source arrangements for included data, versioned provenance, signing and installer/runtime validation. This task publishes source changes only, not public binaries.
