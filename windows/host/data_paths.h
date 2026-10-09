#pragma once
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <userenv.h>
#include <sddl.h>
#include <filesystem>
#include <stdexcept>
#include <vector>
#include "effective_token.h"

struct DataPaths {
    std::filesystem::path root;
    bool app_container=false;
    static DataPaths resolve() {
        EffectiveToken token;
        DWORD container=0,bytes=0;
        if (!GetTokenInformation(token.get(),TokenIsAppContainer,&container,sizeof(container),&bytes))
            throw PlatformError("Query AppContainer identity",HRESULT_FROM_WIN32(GetLastError()));
        struct Path { PWSTR value=nullptr; ~Path() { CoTaskMemFree(value); } } path;
        if (container) {
            if(!GetTokenInformation(token.get(),TokenAppContainerSid,nullptr,0,&bytes)) {
                const auto error=GetLastError();
                if(error!=ERROR_INSUFFICIENT_BUFFER)throw PlatformError("Query AppContainer SID size",HRESULT_FROM_WIN32(error));
            }
            if(bytes<sizeof(TOKEN_APPCONTAINER_INFORMATION))throw PlatformError("Missing AppContainer SID",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            std::vector<BYTE> data(bytes);
            if (!GetTokenInformation(token.get(),TokenAppContainerSid,data.data(),bytes,&bytes))
                throw PlatformError("Read AppContainer SID",HRESULT_FROM_WIN32(GetLastError()));
            struct Sid { LPWSTR value=nullptr; ~Sid() { LocalFree(value); } } sid;
            auto info=reinterpret_cast<TOKEN_APPCONTAINER_INFORMATION*>(data.data());
            if (!ConvertSidToStringSidW(info->TokenAppContainer,&sid.value))
                throw PlatformError("Format AppContainer SID",HRESULT_FROM_WIN32(GetLastError()));
            const auto result=GetAppContainerFolderPath(sid.value,&path.value);
            if (FAILED(result)) throw PlatformError("Resolve AppContainer storage",result);
        } else {
            const auto result=SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&path.value);
            if (FAILED(result)) throw PlatformError("Resolve local application data",result);
        }
        // The OS chooses the identity-specific local folder. Do not guess package
        // names, use foreground EXEs, widen ACLs, or fall back to private userdb.
        DataPaths result{std::filesystem::path(path.value)/L"MYIME",container!=0};
        std::filesystem::create_directories(result.root);
        return result;
    }
};
