#pragma once
#include <msctf.h>
#include <wrl/client.h>

// Observe the real TSF UI manager without installing a system input profile.
class UiObserver final : public ITfUIElementSink {
    long refs_=1;
    Microsoft::WRL::ComPtr<ITfUIElementMgr> manager_;
public:
    BOOL allow_host=TRUE;
    int begins=0,updates=0,ends=0;
    DWORD active_id=0;
    bool valid=true,active=false;
    Microsoft::WRL::ComPtr<ITfCandidateListUIElementBehavior> current;
    explicit UiObserver(ITfUIElementMgr* manager):manager_(manager) {}
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfUIElementSink) return E_NOINTERFACE;
        *out=static_cast<ITfUIElementSink*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP BeginUIElement(DWORD id,BOOL* show) override {
        if (!show) return E_POINTER;
        valid &= !active; active=true; active_id=id; ++begins; *show=allow_host;
        return S_OK;
    }
    STDMETHODIMP UpdateUIElement(DWORD id) override {
        valid &= active && id==active_id; ++updates;
        Microsoft::WRL::ComPtr<ITfUIElement> element;
        if (FAILED(manager_->GetUIElement(id,&element))) { valid=false; return E_FAIL; }
        Microsoft::WRL::ComPtr<ITfCandidateListUIElementBehavior> next;
        if (FAILED(element.As(&next))) { valid=false; return E_FAIL; }
        if (current) valid &= current.Get()==next.Get();
        current=next;
        UINT count=0; valid &= SUCCEEDED(current->GetCount(&count)) && count>0;
        return S_OK;
    }
    STDMETHODIMP EndUIElement(DWORD id) override {
        valid &= active && id==active_id; active=false; ++ends;
        if (current) valid &= current->Finalize()==S_FALSE;
        current.Reset(); return S_OK;
    }
};
