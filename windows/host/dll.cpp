#include "service.h"
#include <new>
#include <string>
HMODULE g_module=nullptr;
long g_objects=0;
class Factory final : public IClassFactory {
    long refs_=1;
public:
    Factory() { InterlockedIncrement(&g_objects); }
    ~Factory() { InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_IClassFactory) return E_NOINTERFACE;
        *out=this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID iid, void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (outer) return CLASS_E_NOAGGREGATION;
        try {
            auto p=new(std::nothrow) WindowsInputAdapter;
            if (!p) return E_OUTOFMEMORY;
            auto hr=p->QueryInterface(iid,out); p->Release(); return hr;
        } catch (...) { return E_OUTOFMEMORY; }
    }
    STDMETHODIMP LockServer(BOOL lock) override { if (lock) InterlockedIncrement(&g_objects); else InterlockedDecrement(&g_objects); return S_OK; }
};
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason==DLL_PROCESS_ATTACH) { g_module=module; DisableThreadLibraryCalls(module); }
    return TRUE; // No COM, Rime, UI or file I/O under loader lock.
}
extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID iid, void** out) {
    if (!out) return E_POINTER; *out=nullptr;
    if (clsid!=kService) return CLASS_E_CLASSNOTAVAILABLE;
    auto p=new(std::nothrow) Factory;
    if (!p) return E_OUTOFMEMORY;
    auto hr=p->QueryInterface(iid,out); p->Release(); return hr;
}
extern "C" HRESULT __stdcall DllCanUnloadNow() { return InterlockedCompareExchange(&g_objects,0,0)==0 ? S_OK : S_FALSE; }
static std::wstring clsid_path() {
    wchar_t guid[40]; StringFromGUID2(kService,guid,40);
    return std::wstring(L"Software\\Classes\\CLSID\\")+guid;
}
extern "C" HRESULT __stdcall DllUnregisterServer() {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(init)) return init;
    ComPtr<ITfInputProcessorProfiles> profiles;
    HRESULT hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
    if (SUCCEEDED(hr)) hr=profiles->Unregister(kService);
    ComPtr<ITfCategoryMgr> category;
    if (SUCCEEDED(CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category))))
        category->UnregisterCategory(kService,GUID_TFCAT_TIP_KEYBOARD,kService);
    try {
        const auto rc=RegDeleteTreeW(HKEY_LOCAL_MACHINE,clsid_path().c_str());
        if (rc!=ERROR_SUCCESS && rc!=ERROR_FILE_NOT_FOUND && SUCCEEDED(hr)) hr=HRESULT_FROM_WIN32(rc);
    } catch (...) { hr=E_OUTOFMEMORY; }
    category.Reset(); profiles.Reset();
    CoUninitialize(); return hr;
}
extern "C" HRESULT __stdcall DllRegisterServer() {
    const HRESULT init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    if (FAILED(init)) return init;
    HRESULT hr=E_FAIL;
    try {
        wchar_t path[32768]; const DWORD len=GetModuleFileNameW(g_module,path,32768);
        if (!len || len>=32768) { CoUninitialize(); return E_FAIL; }
        HKEY key=nullptr; DWORD disposition=0;
        auto rc=RegCreateKeyExW(HKEY_LOCAL_MACHINE,(clsid_path()+L"\\InprocServer32").c_str(),0,nullptr,0,KEY_WRITE,nullptr,&key,&disposition);
        if (rc!=ERROR_SUCCESS) { CoUninitialize(); return HRESULT_FROM_WIN32(rc); }
        if (disposition==REG_OPENED_EXISTING_KEY) { RegCloseKey(key); CoUninitialize(); return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS); }
        rc=RegSetValueExW(key,nullptr,0,REG_SZ,reinterpret_cast<const BYTE*>(path),(len+1)*sizeof(wchar_t));
        if (rc==ERROR_SUCCESS) rc=RegSetValueExW(key,L"ThreadingModel",0,REG_SZ,reinterpret_cast<const BYTE*>(L"Apartment"),sizeof(L"Apartment"));
        RegCloseKey(key);
        hr=HRESULT_FROM_WIN32(rc);
        ComPtr<ITfInputProcessorProfiles> profiles;
        if (SUCCEEDED(hr)) hr=CoCreateInstance(CLSID_TF_InputProcessorProfiles,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&profiles));
        if (SUCCEEDED(hr)) hr=profiles->Register(kService);
        if (SUCCEEDED(hr)) hr=profiles->AddLanguageProfile(kService,kLanguage,kProfile,L"MYIME Rime",10,path,len,0);
        ComPtr<ITfCategoryMgr> category;
        if (SUCCEEDED(hr)) hr=CoCreateInstance(CLSID_TF_CategoryMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&category));
        if (SUCCEEDED(hr)) hr=category->RegisterCategory(kService,GUID_TFCAT_TIP_KEYBOARD,kService);
        if (SUCCEEDED(hr)) hr=profiles->EnableLanguageProfile(kService,kLanguage,kProfile,TRUE);
    } catch (...) { hr=E_OUTOFMEMORY; }
    CoUninitialize();
    if (FAILED(hr)) DllUnregisterServer();
    return hr;
}
