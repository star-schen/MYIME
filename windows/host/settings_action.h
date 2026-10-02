#pragma once
#include <windows.h>
#include <filesystem>
#include <string>

// Platform action invoked only by an explicit menu selection. No setting or
// helper process is needed for input; AppContainer cannot launch desktop UI.
class SettingsAction {
    std::filesystem::path executable_,data_root_;
    bool enabled_=false;
public:
    void configure(const std::filesystem::path& module_directory,
                   const std::filesystem::path& data_root,bool restricted) {
        executable_=module_directory/L"myime-settings.exe"; data_root_=data_root;
        std::error_code error;
        enabled_=!restricted && std::filesystem::is_regular_file(executable_,error);
    }
    void clear() noexcept { enabled_=false; executable_.clear(); data_root_.clear(); }
    bool available() const noexcept { return enabled_; }
    HRESULT open() const noexcept {
        if (!enabled_) return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
        try {
            // Paths come from Windows, not product config or foreground EXEs.
            std::wstring command=L"\""+executable_.wstring()+L"\" --data-root \""+data_root_.wstring()+L"\"";
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
            const auto directory=executable_.parent_path();
            if (!CreateProcessW(executable_.c_str(),command.data(),nullptr,nullptr,FALSE,0,
                                nullptr,directory.c_str(),&startup,&process))
                return HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(process.hThread); CloseHandle(process.hProcess); return S_OK;
        } catch (...) { return E_OUTOFMEMORY; }
    }
};
