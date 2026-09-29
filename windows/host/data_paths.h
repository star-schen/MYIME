#pragma once
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <userenv.h>
#include <sddl.h>
#include <filesystem>
#include <stdexcept>
#include <vector>

struct DataPaths {
    std::filesystem::path root;
    bool app_container=false;
    static DataPaths resolve() {
        struct Token { HANDLE value=nullptr; ~Token() { if (value) CloseHandle(value); } } token;
        if (!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token.value)) {
            if (GetLastError()!=ERROR_NO_TOKEN || !OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.value))
                throw std::runtime_error("Cannot query effective token");
        }
        DWORD container=0,bytes=0;
        if (!GetTokenInformation(token.value,TokenIsAppContainer,&container,sizeof(container),&bytes))
            throw std::runtime_error("Cannot query AppContainer identity");
        struct Path { PWSTR value=nullptr; ~Path() { CoTaskMemFree(value); } } path;
        if (container) {
            GetTokenInformation(token.value,TokenAppContainerSid,nullptr,0,&bytes);
            if (!bytes) throw std::runtime_error("Missing AppContainer SID");
            std::vector<BYTE> data(bytes);
            if (!GetTokenInformation(token.value,TokenAppContainerSid,data.data(),bytes,&bytes))
                throw std::runtime_error("Cannot read AppContainer SID");
            struct Sid { LPWSTR value=nullptr; ~Sid() { LocalFree(value); } } sid;
            auto info=reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(data.data());
            if (!ConvertSidToStringSidW(info->TokenAppContainer,&sid.value) ||
                FAILED(GetAppContainerFolderPath(sid.value,&path.value)))
                throw std::runtime_error("Cannot resolve AppContainer storage");
        } else if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path.value))) {
            throw std::runtime_error("Cannot resolve local application data");
        }
        // The OS chooses the identity-specific local folder. Do not guess package
        // names, use foreground EXEs, widen ACLs, or fall back to private userdb.
        DataPaths result{std::filesystem::path(path.value)/L"MYIME",container!=0};
        std::filesystem::create_directories(result.root);
        return result;
    }
};
