#pragma once
#include <msctf.h>
#include <wrl/client.h>
#include <myime/core.h>
#include <memory>
#include <string>
#include "ids.h"
struct ContextSession;
class ContextObserver final : public ITfTextEditSink {
    long refs_=1;
    void* owner_;
    HRESULT (*changed_)(void*,ContextSession*,TfEditCookie,ITfEditRecord*);
    ContextSession* session_;
public:
    ContextObserver(void* owner,ContextSession* session,HRESULT (*changed)(void*,ContextSession*,TfEditCookie,ITfEditRecord*)) : owner_(owner),changed_(changed),session_(session) { InterlockedIncrement(&g_objects); }
    ~ContextObserver(){InterlockedDecrement(&g_objects);}
    void detach(){owner_=nullptr;session_=nullptr;changed_=nullptr;}
    STDMETHODIMP QueryInterface(REFIID id,void** out) override { if(!out)return E_POINTER; *out=nullptr; if(id!=IID_IUnknown&&id!=IID_ITfTextEditSink)return E_NOINTERFACE; *out=this;AddRef();return S_OK; }
    STDMETHODIMP_(ULONG) AddRef() override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release() override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    STDMETHODIMP OnEndEdit(ITfContext*,TfEditCookie ec,ITfEditRecord* record) override{try{return owner_?changed_(owner_,session_,ec,record):S_OK;}catch(...){return E_FAIL;}}
};
struct ContextSession {
    enum class Phase { Active, Detaching, AwaitingEndEdit, Suspended, Idle, Invalid };
    Microsoft::WRL::ComPtr<ITfContext> context;
    Microsoft::WRL::ComPtr<IUnknown> identity;
    Microsoft::WRL::ComPtr<IUnknown> document_identity;
    Microsoft::WRL::ComPtr<ITfComposition> composition;
    Microsoft::WRL::ComPtr<ITfRange> anchor;
    Microsoft::WRL::ComPtr<ContextObserver> observer;
    DWORD edit_cookie=TF_INVALID_COOKIE;
    MyimeCore* core=nullptr;
    Phase phase=Phase::Idle;
    std::wstring expected;
    ULONGLONG touched=GetTickCount64();
    unsigned long long serial=0;
    uint32_t focus_policy=0;
    bool focused=false,valid=true;
};
