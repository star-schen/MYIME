#pragma once
#include <windows.h>
#include <myime/core.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include "compatibility.h"
#include "user_data.h"
#include "diagnostics.h"
#include "encoding.h"
#include "workspace_paths.h"
class Engine {
    HMODULE dll_=nullptr;
    UserDataLease user_data_;
public:
    MyimeCore* handle=nullptr;
    bool enabled=true;
#define API(name) decltype(&myime_##name) name=nullptr
    API(create); API(destroy); API(key); API(select); API(clear); API(state); API(candidate); API(ack_commit); API(last_error);
    API(load_config); API(apply_profile); API(ui_theme_id);
#undef API
    ~Engine() { close(); }
    bool profile() {
        uint32_t value=0;
        if (apply_profile(handle,utf8(CompatibilityLayer::executable()).c_str(),&value)) { enabled=false; return false; }
        enabled=value!=0; return true;
    }
    void close() { if (handle) { destroy(handle); handle=nullptr; } user_data_.release(); if (dll_) { FreeLibrary(dll_); dll_=nullptr; } }
    void open(const std::filesystem::path& module_dir,const std::filesystem::path& data_root,const Diagnostics& diagnostics) {
        diagnostics.event(L"Loading Core and native dependencies");
        dll_=LoadLibraryExW((module_dir/L"myime_core.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll_) { diagnostics.event(L"Core DLL load failed",HRESULT_FROM_WIN32(GetLastError())); throw std::runtime_error("Cannot load Core"); }
        diagnostics.module(L"myime_core.dll"); diagnostics.module(L"rime.dll");
#define LOAD(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(dll_,"myime_" #name)); if (!name) throw std::runtime_error("Core ABI export missing")
        LOAD(create); LOAD(destroy); LOAD(key); LOAD(select); LOAD(clear); LOAD(state); LOAD(candidate); LOAD(ack_commit); LOAD(last_error);
        LOAD(load_config); LOAD(apply_profile); LOAD(ui_theme_id);
#undef LOAD
        auto abi=reinterpret_cast<decltype(&myime_abi_version)>(GetProcAddress(dll_,"myime_abi_version"));
        if (!abi || abi()!=1) throw std::runtime_error("Core ABI version mismatch");
        diagnostics.event(L"Acquiring user directory lease");
        const auto user_dir=user_data_.acquire(data_root/L"rime"/L"slots");
        diagnostics.event(user_dir.c_str());
        const auto shared=user_data_.shared_data(module_dir,data_root);
        diagnostics.event(shared.c_str());
        if (!std::filesystem::exists(shared/L"build"/L"pinyin_simp.table.bin")) throw std::runtime_error("Deploy and stage Rime data before activation");
        diagnostics.event(L"Creating Rime session from deployed data");
        if (create(utf8(shared.wstring()).c_str(),utf8(user_dir.wstring()).c_str(),"pinyin_simp",&handle)) throw std::runtime_error("Rime session creation failed");
        const auto config=data_root/L"config.toml";
        diagnostics.event(L"Loading product config and applying AppProfile");
        if (load_config(handle,utf8(config.wstring()).c_str()) || !profile()) throw std::runtime_error("Product config or AppProfile failed");
    }
};
