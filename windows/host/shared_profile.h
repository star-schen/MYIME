#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <optional>
#include <string>
#include "diagnostics.h"
#include "effective_token.h"
#include "encoding.h"

// The installer publishes only the machine storage base, never a user identity.
// Effective TokenUser remains authoritative even inside an AppContainer.
struct SharedProfile {
    static std::optional<std::string> metadata(const std::filesystem::path& file,DWORD maximum) {
        HANDLE raw=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(raw==INVALID_HANDLE_VALUE) {
            const auto error=GetLastError();
            if(error==ERROR_FILE_NOT_FOUND || error==ERROR_PATH_NOT_FOUND)return std::nullopt;
            throw PlatformError("Open shared storage metadata",HRESULT_FROM_WIN32(error));
        }
        struct File { HANDLE value; ~File(){CloseHandle(value);} } owned{raw};
        BY_HANDLE_FILE_INFORMATION information{};
        if(!GetFileInformationByHandle(raw,&information))throw PlatformError("Inspect shared storage metadata",HRESULT_FROM_WIN32(GetLastError()));
        if(information.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))
            throw PlatformError("Shared storage metadata must be a regular file",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        if(information.nFileSizeHigh || information.nFileSizeLow>maximum)
            throw PlatformError("Shared storage metadata too large",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        std::string text(information.nFileSizeLow,'\0'); DWORD read=0;
        if(!ReadFile(raw,text.data(),static_cast<DWORD>(text.size()),&read,nullptr))
            throw PlatformError("Read shared storage metadata",HRESULT_FROM_WIN32(GetLastError()));
        if(read!=text.size())throw PlatformError("Shared storage metadata changed during read",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        return text;
    }
    static std::filesystem::path root(const std::filesystem::path& module_directory,const Diagnostics* diagnostics=nullptr) {
        if(diagnostics)diagnostics->event(L"Resolving effective shared-profile user identity");
        EffectiveToken token;
        const auto sid=token.user_sid();
        if(diagnostics)diagnostics->event(L"Effective shared-profile user identity resolved");
        if(diagnostics)diagnostics->event(L"Reading installer-published shared storage base");
        const auto published=metadata(module_directory/L"shared-storage-root.txt",65536);
        std::filesystem::path base;
        if(published) {
            if(published->empty() || published->find_first_of("\r\n\0",0,3)!=std::string::npos)
                throw PlatformError("Invalid published shared storage base",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            base=wide(MyimeText{published->data(),published->size()});
            if(!base.is_absolute() || base!=base.lexically_normal())
                throw PlatformError("Published shared storage base must be an absolute normalized path",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
            if(diagnostics)diagnostics->event(L"Using installer-published shared storage base");
        } else {
            if(diagnostics)diagnostics->event(L"Resolving ProgramData through Windows (legacy installation)");
            struct Folder { PWSTR value=nullptr; ~Folder(){CoTaskMemFree(value);} } folder;
            const auto result=SHGetKnownFolderPath(FOLDERID_ProgramData,0,nullptr,&folder.value);
            if(FAILED(result))throw PlatformError("Resolve ProgramData shared storage base",result);
            base=folder.value;
        }
        return base/L"MYIME"/L"users"/sid;
    }
    static std::filesystem::path presentation(const std::filesystem::path& root) {
        if(root.empty())return {};
        const auto selector=metadata(root/L"presentation"/L"current",34);
        if(!selector)return {};
        if(selector->size()!=34 || (*selector)[0]!='p' || (*selector)[1]!='-')
            throw PlatformError("Invalid shared presentation selector",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        for(size_t index=2;index<34;index++)if(!(((*selector)[index]>='0'&&(*selector)[index]<='9') || ((*selector)[index]>='a'&&(*selector)[index]<='f')))
            throw PlatformError("Invalid shared presentation generation",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        return root/L"presentation"/(*selector);
    }
};
