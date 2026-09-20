#pragma once
#include <windows.h>
#include <filesystem>
#include <mutex>
#include <stdexcept>
// Share one lease across all TSF threads in this process. Each other process
// gets a different persistent slot, avoiding concurrent LevelDB opens.
class UserDataLease {
    struct Shared {
        std::mutex mutex;
        HANDLE file=INVALID_HANDLE_VALUE;
        size_t users=0;
        std::filesystem::path path;
    };
    static Shared& shared() { static Shared value; return value; }
    bool acquired_=false;
public:
    ~UserDataLease() { release(); }
    std::filesystem::path acquire(const std::filesystem::path& root) {
        auto& state=shared(); std::lock_guard guard(state.mutex);
        if (acquired_) return state.path;
        if (!state.users) {
            for (unsigned i=0;i<256;++i) {
                auto path=root/std::to_wstring(i);
                std::filesystem::create_directories(path);
                HANDLE file=CreateFileW((path/L".host.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                if (file!=INVALID_HANDLE_VALUE) { state.path=std::move(path); state.file=file; break; }
                const auto error=GetLastError();
                if (error!=ERROR_SHARING_VIOLATION && error!=ERROR_LOCK_VIOLATION) throw std::runtime_error("Cannot lease a Rime user directory");
            }
            if (state.file==INVALID_HANDLE_VALUE) throw std::runtime_error("No free Rime user directory slot");
        }
        ++state.users; acquired_=true; return state.path;
    }
    void release() {
        if (!acquired_) return;
        auto& state=shared(); std::lock_guard guard(state.mutex);
        acquired_=false;
        if (--state.users==0) { CloseHandle(state.file); state.file=INVALID_HANDLE_VALUE; state.path.clear(); }
    }
};
