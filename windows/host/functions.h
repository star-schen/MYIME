#pragma once
#include <ctffunc.h>
#include "ids.h"

class TouchKeyboardLayout final : public ITfFnGetPreferredTouchKeyboardLayout {
    long refs_=1;
public:
    TouchKeyboardLayout() { InterlockedIncrement(&g_objects); }
    ~TouchKeyboardLayout() { InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfFunction && iid!=IID_ITfFnGetPreferredTouchKeyboardLayout) return E_NOINTERFACE;
        *out=static_cast<ITfFnGetPreferredTouchKeyboardLayout*>(this); AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP GetDisplayName(BSTR* name) override {
        if (!name) return E_POINTER; *name=SysAllocString(L"MYIME keyboard"); return *name?S_OK:E_OUTOFMEMORY;
    }
    STDMETHODIMP GetLayout(TKBLayoutType* type,WORD* id) override {
        if (!type || !id) return E_POINTER;
        *type=TKBLT_CLASSIC; *id=0; return S_OK;
    }
};
