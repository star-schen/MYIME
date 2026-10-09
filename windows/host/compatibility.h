#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
// Platform facts only. Product policy comes from the Rust AppProfile resolver.
struct CompatibilityLayer {
    static std::wstring executable() {
        wchar_t path[32768]; auto n=GetModuleFileNameW(nullptr,path,32768);
        if (!n || n>=32768) return {};
        return std::filesystem::path(path).filename().wstring();
    }
    static std::wstring executable_path() { wchar_t path[32768]{}; auto n=GetModuleFileNameW(nullptr,path,_countof(path)); return n && n<_countof(path)?std::wstring(path,n):std::wstring(); }
    // Future adapters must explicitly negotiate application-rendered TSF UI
    // and IMM32 composition ownership. No EXE-specific branches belong here.
};
