#pragma once
#include <windows.h>
#include <sddl.h>
#include <string>
#include <vector>
#include "platform_error.h"

// Both local and shared storage use the effective identity, including impersonation.
class EffectiveToken {
    HANDLE value_=nullptr;
public:
    EffectiveToken() {
        if (!OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&value_)) {
            const auto error=GetLastError();
            if (error!=ERROR_NO_TOKEN) throw PlatformError("Open effective thread token",HRESULT_FROM_WIN32(error));
            if (!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&value_))
                throw PlatformError("Open effective process token",HRESULT_FROM_WIN32(GetLastError()));
        }
    }
    ~EffectiveToken() { if(value_)CloseHandle(value_); }
    EffectiveToken(const EffectiveToken&)=delete;
    EffectiveToken& operator=(const EffectiveToken&)=delete;
    HANDLE get() const noexcept { return value_; }
    std::wstring user_sid() const {
        DWORD bytes=0;
        if (!GetTokenInformation(value_,TokenUser,nullptr,0,&bytes)) {
            const auto error=GetLastError();
            if (error!=ERROR_INSUFFICIENT_BUFFER) throw PlatformError("Query effective user SID size",HRESULT_FROM_WIN32(error));
        }
        if (bytes<sizeof(TOKEN_USER)) throw PlatformError("Invalid effective user SID size",HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        std::vector<BYTE> data(bytes);
        if (!GetTokenInformation(value_,TokenUser,data.data(),bytes,&bytes))
            throw PlatformError("Read effective user SID",HRESULT_FROM_WIN32(GetLastError()));
        struct Sid { LPWSTR value=nullptr; ~Sid(){LocalFree(value);} } sid;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid.value))
            throw PlatformError("Format effective user SID",HRESULT_FROM_WIN32(GetLastError()));
        return sid.value;
    }
};
