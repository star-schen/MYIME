#include "mode_indicator.h"
#include <cstdio>
HMODULE g_module=nullptr;
long g_objects=0;
class Sink final : public ITfLangBarItemSink {
    long refs_=1;
public:
    int updates=0;
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfLangBarItemSink) return E_NOINTERFACE;
        *out=static_cast<ITfLangBarItemSink*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP OnUpdate(DWORD) override { ++updates; return S_OK; }
};
class Menu final : public ITfMenu {
    long refs_=1;
public:
    int items=0; bool valid=true; DWORD expected_flags=TF_LBMENUF_GRAYED;
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfMenu) return E_NOINTERFACE;
        *out=static_cast<ITfMenu*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP AddMenuItem(UINT,DWORD flags,HBITMAP,HBITMAP,const WCHAR* text,ULONG length,ITfMenu** child) override {
        ++items; valid &= flags==expected_flags && std::wstring(text,length)==L"设置" && !child; return S_OK;
    }
};
HRESULT open_settings(void* owner) { ++*static_cast<int*>(owner); return S_OK; }
int main() {
    g_module=GetModuleHandleW(nullptr);
    auto item=new ModeIndicator(nullptr,nullptr,nullptr);
    auto sink=new Sink; DWORD cookie=0; bool passed=true;
    passed &= item->AdviseSink(IID_ITfLangBarItemSink,sink,&cookie)==S_OK;
    TF_LANGBARITEMINFO info{}; passed &= item->GetInfo(&info)==S_OK && info.guidItem==GUID_LBI_INPUTMODE && (info.dwStyle&TF_LBI_STYLE_BTN_MENU);
    item->refresh(); passed &= sink->updates==1;
    item->Show(FALSE); item->Show(FALSE); passed &= sink->updates==2;
    item->Show(TRUE); item->Show(TRUE); passed &= sink->updates==3;
    item->update(true); passed &= sink->updates==4;
    BSTR label=nullptr; passed &= item->GetText(&label)==S_OK && std::wstring(label)==L"A"; SysFreeString(label);
    item->OnClick(TF_LBI_CLK_LEFT,POINT{},nullptr); passed &= sink->updates==4;
    HICON icon=nullptr; passed &= item->GetIcon(&icon)==S_OK && icon; if (icon) DestroyIcon(icon);
    item->update(false); icon=nullptr;
    passed &= item->GetIcon(&icon)==S_OK && icon; if (icon) DestroyIcon(icon);
    auto menu=new Menu; passed &= item->InitMenu(menu)==S_OK && menu->items==1 && menu->valid; menu->Release();
    item->detach(); item->Release(); passed &= g_objects==0; sink->Release();
    int opened=0; item=new ModeIndicator(&opened,nullptr,nullptr,open_settings);
    menu=new Menu; menu->expected_flags=0;
    passed &= item->InitMenu(menu)==S_OK && menu->valid;
    passed &= item->OnMenuSelect(1)==S_OK && opened==1;
    item->OnClick(TF_LBI_CLK_LEFT,POINT{},nullptr); passed &= opened==1;
    item->detach(); item->OnMenuSelect(1); passed &= opened==1;
    menu->Release(); item->Release(); passed &= g_objects==0;
    std::puts(passed?"Mode icon/menu/notifications: PASS":"Mode icon/menu/notifications: FAIL");
    return passed?0:1;
}
