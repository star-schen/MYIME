#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <cstdio>
#include <array>
#include <cwchar>

// Opt-in, bounded per-process file. Never call from DllMain or log input text.
// A short OS file lock also serializes independent TSF apartments in this process.
class Diagnostics {
    std::filesystem::path file_;
    bool candidate_trace_=false;
    struct CandidateLimit {
        wchar_t previous[512]{};
        HRESULT result=S_OK;
        ULONGLONG emitted=0,window_start=0;
        unsigned count=0,suppressed=0;
        bool initialized=false;
    };
    std::array<CandidateLimit,7> candidate_limits_{};
public:
    enum class CandidateChannel { Negotiation, Show, Context, Owner, Layout, Visibility, Lifecycle };
    bool candidate_enabled() const noexcept { return candidate_trace_; }
    void configure(const std::filesystem::path& root) noexcept {
        file_.clear();
        candidate_limits_={};
        wchar_t value[2]{};
        candidate_trace_=GetEnvironmentVariableW(L"MYIME_DIAGNOSTICS",value,2)==1 && value[0]==L'1';
        if (!candidate_trace_) return;
        try {
            auto directory=root/L"logs";
            std::filesystem::create_directories(directory);
            file_=directory/(L"host-"+std::to_wstring(GetCurrentProcessId())+L".log");
        } catch (...) { OutputDebugStringW(L"MYIME: diagnostic directory unavailable\n"); }
    }
    // Metadata only. Consecutive identical stage/results are suppressed for five seconds;
    // changing events are capped at 12 per second per channel. Report omissions
    // on the next emitted event so an absent line is not mistaken for no callback.
    // This instance belongs to one TSF apartment; no extra input-path lock/IPC.
    void candidate_event(CandidateChannel channel,const wchar_t* stage,HRESULT result=S_OK) noexcept {
        if (!candidate_trace_) return;
        auto& limit=candidate_limits_[static_cast<size_t>(channel)];
        const auto now=GetTickCount64();
        if (now-limit.window_start>=1000) { limit.window_start=now; limit.count=0; }
        const bool duplicate=limit.initialized && limit.result==result && std::wcscmp(limit.previous,stage)==0;
        if ((duplicate && now-limit.emitted<5000) || limit.count>=12) {
            if (limit.suppressed<MAXDWORD) ++limit.suppressed;
            return;
        }
        wchar_t message[640]{};
        _snwprintf_s(message,_countof(message),_TRUNCATE,L"%s suppressed=%u",stage,limit.suppressed);
        wcsncpy_s(limit.previous,stage,_TRUNCATE);
        limit.result=result; limit.emitted=now; limit.initialized=true;
        ++limit.count; limit.suppressed=0;
        event(message,result);
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
        } else event(name?name:L"Process module unavailable",HRESULT_FROM_WIN32(ERROR_MOD_NOT_FOUND));
    }
};
