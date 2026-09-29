#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <cstdio>

// Opt-in, bounded per-process file. Never call from DllMain or log input text.
// A short OS file lock also serializes independent TSF apartments in this process.
class Diagnostics {
    std::filesystem::path file_;
public:
    void configure(const std::filesystem::path& root) noexcept {
        file_.clear();
        wchar_t value[2]{};
        if (GetEnvironmentVariableW(L"MYIME_DIAGNOSTICS",value,2)!=1 || value[0]!=L'1') return;
        try {
            auto directory=root/L"logs";
            std::filesystem::create_directories(directory);
            file_=directory/(L"host-"+std::to_wstring(GetCurrentProcessId())+L".log");
        } catch (...) { OutputDebugStringW(L"MYIME: diagnostic directory unavailable\n"); }
    }
    void event(const wchar_t* stage,HRESULT result=S_OK) const noexcept {
        wchar_t line[1024]{}; SYSTEMTIME time{}; GetSystemTime(&time);
        _snwprintf_s(line,_countof(line),_TRUNCATE,
            L"MYIME: %04u-%02u-%02uT%02u:%02u:%02uZ pid=%lu tid=%lu %s hr=0x%08lX\r\n",
            time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,
            GetCurrentProcessId(),GetCurrentThreadId(),stage,static_cast<unsigned long>(result));
        OutputDebugStringW(line);
        if (file_.empty()) return;
        HANDLE file=CreateFileW(file_.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (file==INVALID_HANDLE_VALUE) return;
        OVERLAPPED lock{};
        if (LockFileEx(file,LOCKFILE_EXCLUSIVE_LOCK|LOCKFILE_FAIL_IMMEDIATELY,0,MAXDWORD,MAXDWORD,&lock)) {
            LARGE_INTEGER size{},zero{};
            if (GetFileSizeEx(file,&size)) {
                if (size.QuadPart>=1024*1024) { SetFilePointerEx(file,zero,nullptr,FILE_BEGIN); SetEndOfFile(file); }
                SetFilePointerEx(file,zero,nullptr,FILE_END);
                char bytes[4096]{};
                int n=WideCharToMultiByte(CP_UTF8,0,line,-1,bytes,sizeof(bytes),nullptr,nullptr);
                DWORD written=0; if (n>1) WriteFile(file,bytes,static_cast<DWORD>(n-1),&written,nullptr);
            }
            UnlockFileEx(file,0,MAXDWORD,MAXDWORD,&lock);
        }
        CloseHandle(file);
    }
    void module(const wchar_t* name) const noexcept {
        wchar_t path[32768]{};
        if (auto module=GetModuleHandleW(name)) {
            if (GetModuleFileNameW(module,path,_countof(path))) event(path);
        } else event(name,HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
    }
};
