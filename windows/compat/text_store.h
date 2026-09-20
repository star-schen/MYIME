#pragma once
#include <windows.h>
#include <msctf.h>
#include <textstor.h>
#include <olectl.h>
#include <wrl/client.h>
#include <string>
#include <algorithm>
using Microsoft::WRL::ComPtr;
// A small plain-text TSF document shared by the compatibility app and integration probe.
class TextStore final : public ITextStoreACP, public ITfContextOwnerCompositionSink {
    long refs_=1;
    ComPtr<ITextStoreACPSink> sink_;
    DWORD lock_=0,pending_=0,mask_=0;
    TS_SELECTION_ACP selection_{0,0,{TS_AE_NONE,FALSE}};
    bool valid(LONG a,LONG b) const { return a>=0 && b>=a && b<=static_cast<LONG>(text.size()); }
    bool read() const { return (lock_&TS_LF_READ)!=0; }
    bool write() const { return (lock_&TS_LF_READWRITE)==TS_LF_READWRITE; }
public:
    std::wstring text;
    HWND window=nullptr;
    bool reject_composition=false,read_only=false,fail_write=false;
    int starts=0,ends=0;
    void redraw() { if (window) InvalidateRect(window,nullptr,TRUE); }
    void type(wchar_t ch) {
        if (lock_ || read_only) return;
        LONG a=selection_.acpStart,b=selection_.acpEnd;
        if (ch==L'\b') { if (a==b && a>0) --a; } else if (ch<32) return;
        const std::wstring insert=ch==L'\b' ? L"" : std::wstring(1,ch);
        text.replace(a,b-a,insert); selection_.acpStart=selection_.acpEnd=a+static_cast<LONG>(insert.size());
        TS_TEXTCHANGE change{a,b,selection_.acpEnd};
        if (sink_) { if (mask_&TS_AS_TEXT_CHANGE) sink_->OnTextChange(0,&change); if (mask_&TS_AS_SEL_CHANGE) sink_->OnSelectionChange(); }
        redraw();
    }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid==IID_IUnknown || iid==IID_ITextStoreACP) *out=static_cast<ITextStoreACP*>(this);
        else if (iid==IID_ITfContextOwnerCompositionSink) *out=static_cast<ITfContextOwnerCompositionSink*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP AdviseSink(REFIID iid,IUnknown* object,DWORD mask) override {
        if (iid!=IID_ITextStoreACPSink || !object) return E_INVALIDARG;
        ComPtr<ITextStoreACPSink> next; auto hr=object->QueryInterface(IID_PPV_ARGS(&next)); if (FAILED(hr)) return hr;
        if (sink_ && sink_.Get()!=next.Get()) return CONNECT_E_ADVISELIMIT;
        sink_=next; mask_=mask; return S_OK;
    }
    STDMETHODIMP UnadviseSink(IUnknown* object) override {
        ComPtr<ITextStoreACPSink> old; if (!object || FAILED(object->QueryInterface(IID_PPV_ARGS(&old))) || old.Get()!=sink_.Get()) return CONNECT_E_NOCONNECTION;
        sink_.Reset(); return S_OK;
    }
    STDMETHODIMP RequestLock(DWORD flags,HRESULT* result) override {
        if (!result) return E_POINTER;
        if (!sink_) return E_UNEXPECTED;
        if (lock_) { if (flags&TS_LF_SYNC) *result=TS_E_SYNCHRONOUS; else { pending_|=(flags&TS_LF_READWRITE); *result=TS_S_ASYNC; } return S_OK; }
        lock_=flags; *result=sink_->OnLockGranted(flags); lock_=0;
        while (pending_) { auto next=pending_; pending_=0; lock_=next; sink_->OnLockGranted(next); lock_=0; }
        redraw(); return S_OK;
    }
    STDMETHODIMP GetStatus(TS_STATUS* s) override { if (!s) return E_POINTER; *s={read_only?static_cast<DWORD>(TS_SD_READONLY):0u,TS_SS_NOHIDDENTEXT}; return S_OK; }
    STDMETHODIMP QueryInsert(LONG a,LONG b,ULONG,LONG* start,LONG* end) override { if (!start || !end) return E_POINTER; if (!valid(a,b)) return TS_E_INVALIDPOS; *start=a; *end=b; return S_OK; }
    STDMETHODIMP GetSelection(ULONG index,ULONG count,TS_SELECTION_ACP* s,ULONG* fetched) override {
        if (!s || !fetched) return E_POINTER; *fetched=0; if (!read()) return TS_E_NOLOCK;
        if (index!=0 && index!=TS_DEFAULT_SELECTION) return E_INVALIDARG; if (count) { *s=selection_; *fetched=1; } return S_OK;
    }
    STDMETHODIMP SetSelection(ULONG count,const TS_SELECTION_ACP* s) override {
        if (!write()) return TS_E_NOLOCK; if (count!=1 || !s || !valid(s->acpStart,s->acpEnd)) return E_INVALIDARG; selection_=*s; return S_OK;
    }
    STDMETHODIMP GetText(LONG a,LONG b,WCHAR* plain,ULONG capacity,ULONG* length,TS_RUNINFO* runs,ULONG run_capacity,ULONG* run_count,LONG* next) override {
        if (!length || !run_count || !next || (capacity && !plain) || (run_capacity && !runs)) return E_POINTER;
        *length=*run_count=0; if (!read()) return TS_E_NOLOCK;
        if (b==-1) b=static_cast<LONG>(text.size()); if (!valid(a,b)) return TS_E_INVALIDPOS;
        ULONG count=capacity?std::min(capacity,static_cast<ULONG>(b-a)):static_cast<ULONG>(b-a);
        if (capacity) { std::copy_n(text.data()+a,count,plain); *length=count; }
        if (run_capacity && count) { runs[0]={count,TS_RT_PLAIN}; *run_count=1; }
        *next=a+count; return S_OK;
    }
    STDMETHODIMP SetText(DWORD,LONG a,LONG b,const WCHAR* value,ULONG count,TS_TEXTCHANGE* change) override {
        if (!write()) return TS_E_NOLOCK; if (read_only) return TS_E_READONLY; if (fail_write) return E_FAIL;
        if (!change || (!value && count)) return E_POINTER; if (!valid(a,b)) return TS_E_INVALIDPOS;
        try { text.replace(a,b-a,value?value:L"",count); } catch (...) { return E_OUTOFMEMORY; }
        *change={a,b,a+static_cast<LONG>(count)}; selection_.acpStart=selection_.acpEnd=change->acpNewEnd; return S_OK;
    }
    STDMETHODIMP GetFormattedText(LONG,LONG,IDataObject** out) override { if (out) *out=nullptr; return E_NOTIMPL; }
    STDMETHODIMP GetEmbedded(LONG,REFGUID,REFIID,IUnknown** out) override { if (out) *out=nullptr; return E_NOTIMPL; }
    STDMETHODIMP QueryInsertEmbedded(const GUID*,const FORMATETC*,BOOL* result) override { if (!result) return E_POINTER; *result=FALSE; return S_OK; }
    STDMETHODIMP InsertEmbedded(DWORD,LONG,LONG,IDataObject*,TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP InsertTextAtSelection(DWORD flags,const WCHAR* value,ULONG count,LONG* a,LONG* b,TS_TEXTCHANGE* change) override {
        if (!read()) return TS_E_NOLOCK;
        if (flags&TS_IAS_QUERYONLY) { if (!a || !b) return E_POINTER; *a=selection_.acpStart; *b=selection_.acpEnd; return S_OK; }
        LONG start=selection_.acpStart,end=selection_.acpEnd;
        auto hr=SetText(0,start,end,value,count,change); if (FAILED(hr)) return hr;
        if (!(flags&TS_IAS_NOQUERY)) { if (a) *a=start; if (b) *b=start+count; } return S_OK;
    }
    STDMETHODIMP InsertEmbeddedAtSelection(DWORD,IDataObject*,LONG*,LONG*,TS_TEXTCHANGE*) override { return E_NOTIMPL; }
    STDMETHODIMP RequestSupportedAttrs(DWORD,ULONG,const TS_ATTRID*) override { return S_OK; }
    STDMETHODIMP RequestAttrsAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override { return S_OK; }
    STDMETHODIMP RequestAttrsTransitioningAtPosition(LONG,ULONG,const TS_ATTRID*,DWORD) override { return S_OK; }
    STDMETHODIMP FindNextAttrTransition(LONG,LONG halt,ULONG,const TS_ATTRID*,DWORD,LONG* next,BOOL* found,LONG* offset) override {
        if (!next || !found || !offset) return E_POINTER; *next=halt; *found=FALSE; *offset=0; return S_OK;
    }
    STDMETHODIMP RetrieveRequestedAttrs(ULONG,TS_ATTRVAL*,ULONG* count) override { if (!count) return E_POINTER; *count=0; return S_OK; }
    STDMETHODIMP GetEndACP(LONG* end) override { if (!end) return E_POINTER; if (!read()) return TS_E_NOLOCK; *end=static_cast<LONG>(text.size()); return S_OK; }
    STDMETHODIMP GetActiveView(TsViewCookie* view) override { if (!view) return E_POINTER; *view=0; return S_OK; }
    STDMETHODIMP GetACPFromPoint(TsViewCookie,const POINT*,DWORD,LONG*) override { return E_NOTIMPL; }
    STDMETHODIMP GetTextExt(TsViewCookie,LONG a,LONG b,RECT* r,BOOL* clipped) override {
        if (!r || !clipped) return E_POINTER; if (!read()) return TS_E_NOLOCK; if (!valid(a,b)) return TS_E_INVALIDPOS;
        if (!window || !IsWindowVisible(window)) return TS_E_NOLAYOUT;
        POINT p{12,12}; ClientToScreen(window,&p); HDC dc=GetDC(window); auto old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        SIZE start{},end{}; GetTextExtentPoint32W(dc,text.data(),a,&start); GetTextExtentPoint32W(dc,text.data(),b,&end);
        SelectObject(dc,old); ReleaseDC(window,dc);
        *r={p.x+start.cx,p.y,p.x+std::max(end.cx,start.cx+1),p.y+24}; *clipped=FALSE; return S_OK;
    }
    STDMETHODIMP GetScreenExt(TsViewCookie,RECT* r) override { if (!r) return E_POINTER; if (window) GetWindowRect(window,r); else *r={}; return S_OK; }
    STDMETHODIMP GetWnd(TsViewCookie,HWND* out) override { if (!out) return E_POINTER; *out=window; return S_OK; }
    STDMETHODIMP OnStartComposition(ITfCompositionView*,BOOL* accept) override { if (!accept) return E_POINTER; *accept=!reject_composition; if (*accept) ++starts; return S_OK; }
    STDMETHODIMP OnUpdateComposition(ITfCompositionView*,ITfRange*) override { redraw(); return S_OK; }
    STDMETHODIMP OnEndComposition(ITfCompositionView*) override { ++ends; redraw(); return S_OK; }
};
