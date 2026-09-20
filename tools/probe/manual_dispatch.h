#pragma once
#include <msctf.h>
#include <wrl/client.h>
// Test-only seam: no installed TIP is required. Only sink registration is replaced;
// all document locks, ranges, compositions and layout are real Windows TSF.
// This does NOT test system registration, activation or keyboard routing.
class ManualDispatch final : public ITfThreadMgr, public ITfKeystrokeMgr {
    long refs_=1;
    Microsoft::WRL::ComPtr<ITfThreadMgr> real_;
    Microsoft::WRL::ComPtr<ITfKeyEventSink> sink_;
public:
    explicit ManualDispatch(ITfThreadMgr* manager):real_(manager) {}
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid==IID_IUnknown || iid==IID_ITfThreadMgr) *out=static_cast<ITfThreadMgr*>(this);
        else if (iid==IID_ITfKeystrokeMgr) *out=static_cast<ITfKeystrokeMgr*>(this);
        else return real_->QueryInterface(iid,out);
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP Activate(TfClientId* id) override { return real_->Activate(id); }
    STDMETHODIMP Deactivate() override { return real_->Deactivate(); }
    STDMETHODIMP CreateDocumentMgr(ITfDocumentMgr** p) override { return real_->CreateDocumentMgr(p); }
    STDMETHODIMP EnumDocumentMgrs(IEnumTfDocumentMgrs** p) override { return real_->EnumDocumentMgrs(p); }
    STDMETHODIMP GetFocus(ITfDocumentMgr** p) override { return real_->GetFocus(p); }
    STDMETHODIMP SetFocus(ITfDocumentMgr* p) override { return real_->SetFocus(p); }
    STDMETHODIMP AssociateFocus(HWND h,ITfDocumentMgr* p,ITfDocumentMgr** old) override { return real_->AssociateFocus(h,p,old); }
    STDMETHODIMP IsThreadFocus(BOOL* p) override { return real_->IsThreadFocus(p); }
    STDMETHODIMP GetFunctionProvider(REFCLSID c,ITfFunctionProvider** p) override { return real_->GetFunctionProvider(c,p); }
    STDMETHODIMP EnumFunctionProviders(IEnumTfFunctionProviders** p) override { return real_->EnumFunctionProviders(p); }
    STDMETHODIMP GetGlobalCompartment(ITfCompartmentMgr** p) override { return real_->GetGlobalCompartment(p); }
    STDMETHODIMP AdviseKeyEventSink(TfClientId,ITfKeyEventSink* sink,BOOL) override { if (sink_) return E_UNEXPECTED; sink_=sink; return S_OK; }
    STDMETHODIMP UnadviseKeyEventSink(TfClientId) override { sink_.Reset(); return S_OK; }
    STDMETHODIMP GetForeground(CLSID*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyDown(WPARAM,LPARAM,BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP TestKeyUp(WPARAM,LPARAM,BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyDown(WPARAM,LPARAM,BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP KeyUp(WPARAM,LPARAM,BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKey(ITfContext*,const TF_PRESERVEDKEY*,GUID*) override { return E_NOTIMPL; }
    STDMETHODIMP IsPreservedKey(REFGUID,const TF_PRESERVEDKEY*,BOOL*) override { return E_NOTIMPL; }
    STDMETHODIMP PreserveKey(TfClientId,REFGUID,const TF_PRESERVEDKEY*,const WCHAR*,ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP UnpreserveKey(REFGUID,const TF_PRESERVEDKEY*) override { return E_NOTIMPL; }
    STDMETHODIMP SetPreservedKeyDescription(REFGUID,const WCHAR*,ULONG) override { return E_NOTIMPL; }
    STDMETHODIMP GetPreservedKeyDescription(REFGUID,BSTR*) override { return E_NOTIMPL; }
    STDMETHODIMP SimulatePreservedKey(ITfContext*,REFGUID,BOOL*) override { return E_NOTIMPL; }
};
