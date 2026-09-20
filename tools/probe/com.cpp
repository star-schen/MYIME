#include <windows.h>
#include <msctf.h>
#include <wrl/client.h>
#include "ids.h"
#include <cstdio>
using Microsoft::WRL::ComPtr;
int main() {
    if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
    HMODULE dll=LoadLibraryW(L"myime_host.dll");
    if (!dll) return 2;
    auto create=reinterpret_cast<HRESULT(WINAPI*)(REFCLSID,REFIID,void**)>(GetProcAddress(dll,"DllGetClassObject"));
    auto unload=reinterpret_cast<HRESULT(WINAPI*)()>(GetProcAddress(dll,"DllCanUnloadNow"));
    bool passed=create && unload && unload()==S_OK;
    if (passed) {
        ComPtr<IClassFactory> factory;
        passed=SUCCEEDED(create(kService,IID_PPV_ARGS(&factory)));
        ComPtr<ITfTextInputProcessorEx> service;
        if (passed) passed=SUCCEEDED(factory->CreateInstance(nullptr,IID_PPV_ARGS(&service))) && unload()==S_FALSE;
        ComPtr<ITfKeyEventSink> keys;
        if (passed) passed=SUCCEEDED(service.As(&keys));
        if (passed) passed=service->ActivateEx(nullptr,0,0)==E_INVALIDARG;
    }
    passed=passed && unload()==S_OK;
    FreeLibrary(dll); CoUninitialize();
    std::puts(passed ? "COM factory/lifetime: PASS" : "COM factory/lifetime: FAIL");
    return passed ? 0 : 3;
}
