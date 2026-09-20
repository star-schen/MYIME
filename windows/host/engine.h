#pragma once
#include <windows.h>
#include <myime/core.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include "compatibility.h"
#include "user_data.h"
inline std::wstring wide(MyimeText text) {
    if (!text.len) return {};
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data,static_cast<int>(text.len),nullptr,0);
    if (!count) throw std::runtime_error("Invalid UTF-8 from core");
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data,static_cast<int>(text.len),result.data(),count);
    return result;
}
inline std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if (!count) throw std::runtime_error("Invalid UTF-16 path");
    std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count,nullptr,nullptr);
    return result;
}
class Engine {
    HMODULE dll_=nullptr;
    UserDataLease user_data_;
public:
    MyimeCore* handle=nullptr;
    bool enabled=true;
#define API(name) decltype(&myime_##name) name=nullptr
    API(create); API(destroy); API(key); API(select); API(clear); API(state); API(candidate); API(ack_commit); API(last_error);
    API(load_config); API(apply_profile);
#undef API
    ~Engine() { close(); }
    bool profile() {
        uint32_t value=0;
        if (apply_profile(handle,utf8(CompatibilityLayer::executable()).c_str(),&value)) { enabled=false; return false; }
        enabled=value!=0; return true;
    }
    void close() { if (handle) { destroy(handle); handle=nullptr; } user_data_.release(); if (dll_) { FreeLibrary(dll_); dll_=nullptr; } }
    void open(const std::filesystem::path& module_dir) {
        dll_=LoadLibraryExW((module_dir/L"myime_core.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!dll_) throw std::runtime_error("Cannot load myime_core.dll or rime.dll");
#define LOAD(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(dll_,"myime_" #name)); if (!name) throw std::runtime_error("Core ABI export missing")
        LOAD(create); LOAD(destroy); LOAD(key); LOAD(select); LOAD(clear); LOAD(state); LOAD(candidate); LOAD(ack_commit); LOAD(last_error);
        LOAD(load_config); LOAD(apply_profile);
#undef LOAD
        auto abi=reinterpret_cast<decltype(&myime_abi_version)>(GetProcAddress(dll_,"myime_abi_version"));
        if (!abi || abi()!=1) throw std::runtime_error("Core ABI version mismatch");
        wchar_t local[32768]; auto n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
        if (!n || n>=32768) throw std::runtime_error("LOCALAPPDATA unavailable");
        const auto user_dir=user_data_.acquire(std::filesystem::path(local)/L"MYIME"/L"rime"/L"slots");
        const auto shared=module_dir/L"data"/L"shared";
        if (!std::filesystem::exists(shared/L"build"/L"pinyin_simp.table.bin")) throw std::runtime_error("Deploy and stage Rime data before activation");
        if (create(utf8(shared.wstring()).c_str(),utf8(user_dir.wstring()).c_str(),"pinyin_simp",&handle)) throw std::runtime_error(last_error());
        const auto config=std::filesystem::path(local)/L"MYIME"/L"config.toml";
        if (load_config(handle,utf8(config.wstring()).c_str()) || !profile()) throw std::runtime_error(last_error());
    }
};
