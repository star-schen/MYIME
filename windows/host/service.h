#pragma once
#include <msctf.h>
#include <wrl/client.h>
#include <array>
#include "ids.h"
#include "engine.h"
#include "candidate_window.h"
using Microsoft::WRL::ComPtr;
class WindowsInputAdapter final : public ITfTextInputProcessorEx, public ITfKeyEventSink,
    public ITfTextEditSink, public ITfThreadMgrEventSink, public ITfTextLayoutSink {
public:
    WindowsInputAdapter() { InterlockedIncrement(&g_objects); }
    ~WindowsInputAdapter() { Deactivate(); InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID, void**) override;
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP Activate(ITfThreadMgr* m,TfClientId id) override { return ActivateEx(m,id,0); }
    STDMETHODIMP ActivateEx(ITfThreadMgr*,TfClientId,DWORD) override;
    STDMETHODIMP Deactivate() override;
    STDMETHODIMP OnSetFocus(BOOL) override;
    STDMETHODIMP OnTestKeyDown(ITfContext*,WPARAM,LPARAM,BOOL*) override;
    STDMETHODIMP OnTestKeyUp(ITfContext*,WPARAM,LPARAM,BOOL*) override;
    STDMETHODIMP OnKeyDown(ITfContext*,WPARAM,LPARAM,BOOL*) override;
    STDMETHODIMP OnKeyUp(ITfContext*,WPARAM,LPARAM,BOOL*) override;
    STDMETHODIMP OnPreservedKey(ITfContext*,REFGUID,BOOL* eaten) override { if (!eaten) return E_POINTER; *eaten=FALSE; return S_OK; }
    STDMETHODIMP OnEndEdit(ITfContext*,TfEditCookie,ITfEditRecord*) override;
    STDMETHODIMP OnInitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnUninitDocumentMgr(ITfDocumentMgr*) override { return S_OK; }
    STDMETHODIMP OnSetFocus(ITfDocumentMgr*,ITfDocumentMgr*) override;
    STDMETHODIMP OnPushContext(ITfContext*) override;
    STDMETHODIMP OnPopContext(ITfContext*) override;
    STDMETHODIMP OnLayoutChange(ITfContext*,TfLayoutCode,ITfContextView*) override;
    HRESULT edit(TfEditCookie,ITfContext*,int action,int key,int mask,unsigned long long generation,BOOL* eaten);
private:
    long refs_=1;
    ComPtr<ITfThreadMgr> manager_;
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
    ComPtr<ITfCompositionSink> composition_observer_;
    TfClientId client_=TF_CLIENTID_NULL;
    DWORD manager_cookie_=TF_INVALID_COOKIE,layout_cookie_=TF_INVALID_COOKIE;
    DWORD edit_cookie_=TF_INVALID_COOKIE;
    Engine engine_;
    CandidateWindow window_;
    std::array<bool,256> forwarded_{};
    bool faulted_=false;
    unsigned long long generation_=0;
    bool eligible(ITfContext*,WPARAM);
    void switch_context(ITfContext*);
    void reset();
    HRESULT request(ITfContext*,int,int,int,bool,BOOL*);
    HRESULT apply(TfEditCookie,ITfContext*);
    HRESULT position(TfEditCookie,ITfContext*);
    static void click(void*,int);
};
