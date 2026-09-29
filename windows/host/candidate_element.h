#pragma once
#include <msctf.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "ids.h"

// Apartment-affine COM snapshot. The controller detaches callbacks BEFORE
// EndUIElement. Applications may keep the object after our service is gone.
class CandidateElement final : public ITfCandidateListUIElementBehavior {
public:
    using Action=HRESULT(*)(void*,int,unsigned long long);
    using Visibility=void(*)(void*,BOOL);
    using VisibilityQuery=BOOL(*)(void*);
    CandidateElement(ITfDocumentMgr* document,std::vector<std::wstring> words,UINT selection,
        void* owner,Action action,Visibility visibility,unsigned long long generation,VisibilityQuery visible)
        :document_(document),words_(std::move(words)),selection_(selection),owner_(owner),
         action_(action),visibility_(visibility),generation_(generation),visible_(visible) { InterlockedIncrement(&g_objects); }
    ~CandidateElement() { InterlockedDecrement(&g_objects); }
    void detach() noexcept { owner_=nullptr; action_=nullptr; visibility_=nullptr; shown_=FALSE; document_.Reset(); }
    bool shown() const noexcept { return shown_!=FALSE; }
    void initial_visibility(BOOL show) noexcept { shown_=show; }
    void update(std::vector<std::wstring> words,UINT selection,unsigned long long generation) {
        updated_=0;
        if (words_.size()!=words.size()) updated_|=TF_CLUIE_COUNT|TF_CLUIE_PAGEINDEX;
        if (words_!=words) updated_|=TF_CLUIE_STRING;
        if (selection_!=selection) updated_|=TF_CLUIE_SELECTION;
        words_=std::move(words); selection_=selection; generation_=generation;
        // A selection requested before this update cannot finalize another page.
        // New SetSelection calls always refer to this current snapshot.
    }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfUIElement && iid!=IID_ITfCandidateListUIElement && iid!=IID_ITfCandidateListUIElementBehavior) return E_NOINTERFACE;
        *out=static_cast<ITfCandidateListUIElementBehavior*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP GetDescription(BSTR* value) override { if (!value) return E_POINTER; *value=SysAllocString(L"MYIME Rime candidates"); return *value?S_OK:E_OUTOFMEMORY; }
    STDMETHODIMP GetGUID(GUID* value) override { if (!value) return E_POINTER; *value=kProfile; return S_OK; }
    STDMETHODIMP Show(BOOL show) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (!owner_) return S_FALSE;
        shown_=show; visibility_(owner_,shown_); return S_OK;
    }
    STDMETHODIMP IsShown(BOOL* value) override {
        if (!value) return E_POINTER; *value=FALSE;
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (owner_ && visible_) *value=visible_(owner_);
        return S_OK;
    }
    STDMETHODIMP GetUpdatedFlags(DWORD* value) override {
        if (!value) return E_POINTER;
        *value=updated_; return S_OK;
    }
    STDMETHODIMP GetDocumentMgr(ITfDocumentMgr** value) override { if (!value) return E_POINTER; return document_.CopyTo(value); }
    STDMETHODIMP GetCount(UINT* value) override { if (!value) return E_POINTER; *value=static_cast<UINT>(words_.size()); return S_OK; }
    STDMETHODIMP GetSelection(UINT* value) override { if (!value) return E_POINTER; *value=selection_; return S_OK; }
    STDMETHODIMP GetString(UINT index,BSTR* value) override {
        if (!value) return E_POINTER; *value=nullptr;
        if (index>=words_.size()) return E_INVALIDARG;
        *value=SysAllocStringLen(words_[index].data(),static_cast<UINT>(words_[index].size())); return *value?S_OK:E_OUTOFMEMORY;
    }
    // The C ABI exports only the CURRENT Rime page. Expose one local page,
    // not fake global indices or a guessed total count for unevaluated candidates.
    STDMETHODIMP GetPageIndex(UINT* values,UINT capacity,UINT* count) override {
        if (!count) return E_POINTER; *count=1;
        if (!capacity) return S_FALSE;
        if (!values) return E_POINTER; values[0]=0; return S_OK;
    }
    STDMETHODIMP SetPageIndex(UINT* values,UINT count) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        // UI capacity cannot change Rime's logical page boundaries.
        return count==1 && values && values[0]==0 ? S_OK : E_INVALIDARG;
    }
    STDMETHODIMP GetCurrentPage(UINT* value) override { if (!value) return E_POINTER; *value=0; return S_OK; }
    STDMETHODIMP SetSelection(UINT index) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (!owner_) return S_FALSE;
        if (index>=words_.size()) return E_INVALIDARG;
        selection_=index; selected_generation_=generation_; selection_pending_=true;
        return S_OK; // Pending application selection, committed by Finalize.
    }
    STDMETHODIMP Finalize() override {
        if (selection_pending_ && selected_generation_!=generation_) return S_FALSE;
        return invoke(static_cast<int>(selection_));
    }
    STDMETHODIMP Abort() override { return invoke(-5); }
private:
    long refs_=1;
    DWORD thread_=GetCurrentThreadId();
    Microsoft::WRL::ComPtr<ITfDocumentMgr> document_;
    std::vector<std::wstring> words_;
    UINT selection_=0;
    void* owner_=nullptr;
    Action action_=nullptr;
    Visibility visibility_=nullptr;
    unsigned long long generation_=0;
    BOOL shown_=FALSE;
    VisibilityQuery visible_=nullptr;
    bool selection_pending_=false;
    unsigned long long selected_generation_=0;
    DWORD updated_=TF_CLUIE_DOCUMENTMGR|TF_CLUIE_COUNT|TF_CLUIE_SELECTION|TF_CLUIE_STRING|TF_CLUIE_PAGEINDEX|TF_CLUIE_CURRENTPAGE;
    HRESULT invoke(int action) {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        return owner_ && action_ ? action_(owner_,action,generation_) : S_FALSE;
    }
};
