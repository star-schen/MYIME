#pragma once
#include <msctf.h>
#include <ctfutb.h>
#include <ctffunc.h>
#include <olectl.h>
#include <wrl/client.h>
#include "ids.h"
#include "resource.h"

// Presentation only: never owns Core or changes Rime state. Detach before
// removing the item because TSF may retain its COM reference after Deactivate.
class ModeIndicator final : public ITfLangBarItemButton, public ITfSource {
    long refs_=1;
    DWORD thread_=GetCurrentThreadId();
    DWORD status_=0;
    bool ascii_=false,attached_=true;
    Microsoft::WRL::ComPtr<ITfLangBarItemSink> sink_;
    enum : UINT { OpenSettings=1 }; // Future launcher action; no GUI dependency.
    HRESULT text(const wchar_t* value,BSTR* out) {
        if (!out) return E_POINTER; *out=SysAllocString(value); return *out?S_OK:E_OUTOFMEMORY;
    }
    void notify(DWORD flags) {
        auto sink=sink_; if (sink) sink->OnUpdate(flags);
    }
public:
    ModeIndicator() { InterlockedIncrement(&g_objects); }
    ~ModeIndicator() { InterlockedDecrement(&g_objects); }
    void update(bool ascii) { if (ascii_!=ascii) { ascii_=ascii; notify(TF_LBI_ICON|TF_LBI_TEXT|TF_LBI_TOOLTIP); } }
    void detach() { attached_=false; status_|=TF_LBI_STATUS_DISABLED; sink_.Reset(); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid==IID_IUnknown || iid==IID_ITfLangBarItem || iid==IID_ITfLangBarItemButton) *out=static_cast<ITfLangBarItemButton*>(this);
        else if (iid==IID_ITfSource) *out=static_cast<ITfSource*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP GetInfo(TF_LANGBARITEMINFO* value) override {
        if (!value) return E_POINTER; *value={};
        value->clsidService=kService; value->guidItem=GUID_LBI_INPUTMODE;
        value->dwStyle=TF_LBI_STYLE_BTN_BUTTON|TF_LBI_STYLE_SHOWNINTRAY;
        wcscpy_s(value->szDescription,L"MYIME 输入模式"); return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD* value) override { if (!value) return E_POINTER; *value=status_; return S_OK; }
    STDMETHODIMP Show(BOOL show) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (show) status_&=~TF_LBI_STATUS_HIDDEN; else status_|=TF_LBI_STATUS_HIDDEN;
        notify(TF_LBI_STATUS); return S_OK;
    }
    STDMETHODIMP GetTooltipString(BSTR* value) override { return text(ascii_?L"MYIME：英文（左键切换尚未实现）":L"MYIME：中文（左键切换尚未实现）",value); }
    STDMETHODIMP GetText(BSTR* value) override { return text(ascii_?L"A":L"中",value); }
    STDMETHODIMP GetIcon(HICON* value) override {
        if (!value) return E_POINTER;
        *value=static_cast<HICON>(LoadImageW(g_module,MAKEINTRESOURCEW(ascii_?IDI_MODE_EN:IDI_MODE_ZH),IMAGE_ICON,
            GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),0));
        // The TSF caller destroys this non-shared HICON.
        return *value?S_OK:HRESULT_FROM_WIN32(GetLastError());
    }
    STDMETHODIMP OnClick(TfLBIClick click,POINT point,const RECT*) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (!attached_ || click!=TF_LBI_CLK_RIGHT) return S_OK; // Left click intentionally inert.
        Microsoft::WRL::ComPtr<ITfLangBarItemButton> lifetime=this;
        HMENU menu=CreatePopupMenu(); if (!menu) return HRESULT_FROM_WIN32(GetLastError());
        AppendMenuW(menu,MF_STRING|MF_GRAYED,OpenSettings,L"设置");
        // Temporary, invisible, same-thread owner. No focus-stealing tray window.
        HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"STATIC",L"MYIME menu owner",WS_POPUP,
            0,0,0,0,GetFocus(),nullptr,g_module,nullptr);
        const HRESULT hr=owner?S_OK:HRESULT_FROM_WIN32(GetLastError());
        if (owner) {
            TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,
                point.x,point.y,owner,nullptr);
            DestroyWindow(owner);
        }
        DestroyMenu(menu); return hr;
    }
    STDMETHODIMP InitMenu(ITfMenu* menu) override {
        if (!menu) return E_POINTER;
        return menu->AddMenuItem(OpenSettings,TF_LBMENUF_GRAYED,nullptr,nullptr,L"设置",2,nullptr);
    }
    STDMETHODIMP OnMenuSelect(UINT) override { return S_OK; }
    STDMETHODIMP AdviseSink(REFIID iid,IUnknown* source,DWORD* cookie) override {
        if (!cookie || !source) return E_POINTER; *cookie=TF_INVALID_COOKIE;
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (!attached_) return CONNECT_E_CANNOTCONNECT;
        if (iid!=IID_ITfLangBarItemSink) return CONNECT_E_CANNOTCONNECT;
        if (sink_) return CONNECT_E_ADVISELIMIT;
        auto hr=source->QueryInterface(IID_PPV_ARGS(&sink_)); if (SUCCEEDED(hr)) *cookie=1; return hr;
    }
    STDMETHODIMP UnadviseSink(DWORD cookie) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if (cookie!=1 || !sink_) return CONNECT_E_NOCONNECTION;
        sink_.Reset(); return S_OK;
    }
};
