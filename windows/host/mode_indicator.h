#pragma once
#include <msctf.h>
#include <ctfutb.h>
#include <ctffunc.h>
#include <olectl.h>
#include <wrl/client.h>
#include "ids.h"
#include "resource.h"
#include "diagnostics.h"
#include "ui_language.h"
#include <vector>
#include <string>
#include <new>

// Presentation only: never owns Core or changes Rime state. Detach before
// removing the item because TSF may retain its COM reference after Deactivate.
class ModeIndicator final : public ITfLangBarItemButton, public ITfSource {
    long refs_=1;
    DWORD thread_=GetCurrentThreadId();
    DWORD status_=0;
    bool ascii_=false,attached_=true;
    void* owner_=nullptr;
    HWND (*get_owner_)(void*)=nullptr;
    HRESULT (*open_settings_)(void*)=nullptr;
    const Diagnostics* diagnostics_=nullptr;
    std::vector<std::pair<std::wstring,std::wstring>> groups_,menu_groups_;
    std::wstring group_;
    bool habits_=false;
    bool learning_available_=true,shared_settings_available_=true;
    UiLanguage language_;
    HRESULT (*select_group_)(void*,const std::wstring&)=nullptr;
    Microsoft::WRL::ComPtr<ITfLangBarItemSink> sink_;
    enum : UINT { OpenSettings=1 };
    HRESULT text(const wchar_t* value,BSTR* out) {
        if (!out) return E_POINTER; *out=SysAllocString(value); return *out?S_OK:E_OUTOFMEMORY;
    }
    void notify(DWORD flags) {
        auto sink=sink_; if (sink) sink->OnUpdate(flags);
    }
public:
    ModeIndicator(void* owner,HWND (*get_owner)(void*),const Diagnostics* diagnostics,
                  HRESULT (*open_settings)(void*)=nullptr,HRESULT (*select_group)(void*,const std::wstring&)=nullptr)
        :owner_(owner),get_owner_(get_owner),open_settings_(open_settings),diagnostics_(diagnostics),select_group_(select_group) { InterlockedIncrement(&g_objects); }
    ~ModeIndicator() { InterlockedDecrement(&g_objects); }
    void refresh() { notify(TF_LBI_STATUS|TF_LBI_ICON|TF_LBI_TEXT|TF_LBI_TOOLTIP); }
    void language(UiLanguage value) { language_=std::move(value); refresh(); }
    void update(bool ascii) { if (ascii_!=ascii) { ascii_=ascii; refresh(); } }
    void availability(bool learning,bool settings) {
        if(learning_available_!=learning || shared_settings_available_!=settings) {
            learning_available_=learning;shared_settings_available_=settings;refresh();
        }
    }
    void groups(std::vector<std::pair<std::wstring,std::wstring>> groups,std::wstring selected,bool habits){groups_=std::move(groups);group_=std::move(selected);habits_=habits;}
    void detach() { attached_=false; owner_=nullptr; get_owner_=nullptr; open_settings_=nullptr; select_group_=nullptr; diagnostics_=nullptr; status_|=TF_LBI_STATUS_DISABLED; sink_.Reset(); }
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
        value->dwStyle=TF_LBI_STYLE_BTN_BUTTON|TF_LBI_STYLE_BTN_MENU|TF_LBI_STYLE_SHOWNINTRAY;
        value->ulSort=1;
        wcsncpy_s(value->szDescription,language_.text("MYIME 输入模式"),_TRUNCATE); return S_OK;
    }
    STDMETHODIMP GetStatus(DWORD* value) override { if (!value) return E_POINTER; *value=status_; return S_OK; }
    STDMETHODIMP Show(BOOL show) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        const auto previous=status_;
        if (show) status_&=~TF_LBI_STATUS_HIDDEN; else status_|=TF_LBI_STATUS_HIDDEN;
        if (previous!=status_) {
            if (diagnostics_) diagnostics_->event(show?L"Mode indicator show":L"Mode indicator hide");
            notify(TF_LBI_STATUS);
        }
        return S_OK;
    }
    STDMETHODIMP GetTooltipString(BSTR* value) override {
        try {
            std::wstring message=language_.text(ascii_?"MYIME：英文（左键切换尚未实现）":"MYIME：中文（左键切换尚未实现）");
            if(!shared_settings_available_)message+=std::wstring(L"\n")+language_.text("共享设置不可用；使用本地设置");
            if(!learning_available_)message+=std::wstring(L"\n")+language_.text("共享学习不可用；基础输入可继续");
            return text(message.c_str(),value);
        }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
    }
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
        AppendMenuW(menu,MF_STRING|(open_settings_?0:MF_GRAYED),OpenSettings,language_.text("设置"));
        const auto groups=groups_;
        if(!shared_settings_available_)AppendMenuW(menu,MF_STRING|MF_GRAYED,4,language_.text("共享设置不可用；使用本地设置"));
        if(!learning_available_)AppendMenuW(menu,MF_STRING|MF_GRAYED,5,language_.text("共享学习不可用；基础输入可继续"));
        if(select_group_ && learning_available_){AppendMenuW(menu,MF_SEPARATOR,0,nullptr);AppendMenuW(menu,MF_STRING|MF_GRAYED,3,language_.text(habits_?"按分组使用学习习惯":"全局学习（分组仅作标签）"));AppendMenuW(menu,MF_STRING,2,language_.text("跟随应用分组"));for(size_t i=0;i<groups.size();i++)AppendMenuW(menu,MF_STRING|(groups[i].first==group_?MF_CHECKED:0),100+static_cast<UINT>(i),groups[i].second.c_str());}
        HWND owner=get_owner_?get_owner_(owner_):nullptr;
        HRESULT hr=owner?S_OK:E_FAIL;
        if (diagnostics_) diagnostics_->event(L"Mode indicator right click",hr);
        if (owner) {
            const auto action=TrackPopupMenuEx(menu,TPM_RETURNCMD|TPM_NONOTIFY|TPM_RIGHTBUTTON|TPM_BOTTOMALIGN,
                point.x,point.y,owner,nullptr);
            if (action==OpenSettings) hr=OnMenuSelect(OpenSettings);
            else if(select_group_ && attached_ && learning_available_){if(action==2)hr=select_group_(owner_,L"");else if(action>=100&&action<100+groups.size())hr=select_group_(owner_,groups[action-100].first);}
        }
        DestroyMenu(menu); return hr;
    }
    STDMETHODIMP InitMenu(ITfMenu* menu) override {
        if (!menu) return E_POINTER;
        if (diagnostics_) diagnostics_->event(L"Mode indicator system menu");
        const auto settings=language_.text("设置"),follow=language_.text("跟随应用分组");
        auto result=menu->AddMenuItem(OpenSettings,attached_ && open_settings_?0:TF_LBMENUF_GRAYED,nullptr,nullptr,settings,static_cast<ULONG>(wcslen(settings)),nullptr);if(FAILED(result))return result;
        if(SUCCEEDED(result) && !shared_settings_available_){const auto warning=language_.text("共享设置不可用；使用本地设置");result=menu->AddMenuItem(4,TF_LBMENUF_GRAYED,nullptr,nullptr,warning,static_cast<ULONG>(wcslen(warning)),nullptr);}
        if(SUCCEEDED(result) && !learning_available_){const auto warning=language_.text("共享学习不可用；基础输入可继续");result=menu->AddMenuItem(5,TF_LBMENUF_GRAYED,nullptr,nullptr,warning,static_cast<ULONG>(wcslen(warning)),nullptr);}
        if(FAILED(result))return result;
        if(attached_&&select_group_&&learning_available_){menu_groups_=groups_;result=menu->AddMenuItem(2,0,nullptr,nullptr,follow,static_cast<ULONG>(wcslen(follow)),nullptr);for(size_t i=0;SUCCEEDED(result)&&i<menu_groups_.size();i++)result=menu->AddMenuItem(100+static_cast<UINT>(i),menu_groups_[i].first==group_?TF_LBMENUF_CHECKED:0,nullptr,nullptr,menu_groups_[i].second.c_str(),static_cast<ULONG>(menu_groups_[i].second.size()),nullptr);}return result;
    }
    STDMETHODIMP OnMenuSelect(UINT action) override {
        if (GetCurrentThreadId()!=thread_) return RPC_E_WRONG_THREAD;
        if(!attached_)return S_OK;if(action==OpenSettings&&open_settings_)return open_settings_(owner_);if(select_group_ && learning_available_){if(action==2)return select_group_(owner_,L"");if(action>=100&&action<100+menu_groups_.size())return select_group_(owner_,menu_groups_[action-100].first);}return S_OK;
    }
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
