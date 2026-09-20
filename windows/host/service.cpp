#include "service.h"
#include <initguid.h>
#include <inputscope.h>
#include <new>
#include <algorithm>
#include <cstdio>
namespace {
// No back-reference to the service. Windows may retain this sink when an owner
// rejects StartComposition. Late callbacks cannot retain or touch an engine.
class CompositionObserver final : public ITfCompositionSink {
    long refs_=1;
public:
    CompositionObserver() { InterlockedIncrement(&g_objects); }
    ~CompositionObserver() { InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfCompositionSink) return E_NOINTERFACE;
        *out=this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP OnCompositionTerminated(TfEditCookie,ITfComposition*) override { return S_OK; }
};
void log_failure(const char* m) {
    OutputDebugStringA("MYIME: "); OutputDebugStringA(m); OutputDebugStringA("\n");
    wchar_t diagnostic[2];
    if (GetEnvironmentVariableW(L"MYIME_DIAGNOSTICS",diagnostic,2)==1 && diagnostic[0]==L'1') std::fprintf(stderr,"MYIME: %s\n",m);
}
class Edit final : public ITfEditSession {
    long refs_=1;
    WindowsInputAdapter* service_;
    ComPtr<ITfContext> context_;
    int action_,key_,mask_;
    unsigned long long generation_;
public:
    BOOL eaten=FALSE;
    Edit(WindowsInputAdapter* s,ITfContext* c,int a,int k,int m,unsigned long long g)
        :service_(s),context_(c),action_(a),key_(k),mask_(m),generation_(g) { s->AddRef(); InterlockedIncrement(&g_objects); }
    ~Edit() { service_->Release(); InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfEditSession) return E_NOINTERFACE;
        *out=this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override { return service_->edit(ec,context_.Get(),action_,key_,mask_,generation_,&eaten); }
};
class Cancel final : public ITfEditSession {
    long refs_=1;
    ComPtr<ITfComposition> composition_;
public:
    explicit Cancel(ITfComposition* c):composition_(c) { InterlockedIncrement(&g_objects); }
    ~Cancel() { InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfEditSession) return E_NOINTERFACE;
        *out=this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        ComPtr<ITfRange> range;
        if (SUCCEEDED(composition_->GetRange(&range))) range->SetText(ec,0,L"",0);
        return composition_->EndComposition(ec);
    }
};
bool disabled(ITfContext* c,REFGUID id) {
    ComPtr<ITfCompartmentMgr> m; ComPtr<ITfCompartment> p;
    if (FAILED(c->QueryInterface(IID_PPV_ARGS(&m))) || FAILED(m->GetCompartment(id,&p))) return false;
    VARIANT v; VariantInit(&v);
    bool result=SUCCEEDED(p->GetValue(&v)) && v.vt==VT_I4 && v.lVal!=0;
    VariantClear(&v); return result;
}
int translate(WPARAM vk,LPARAM info,int& mask,bool release) {
    BYTE keys[256]; if (!GetKeyboardState(keys)) return 0;
    mask=((keys[VK_SHIFT]&0x80)?1:0)|((keys[VK_CONTROL]&0x80)?4:0)|((keys[VK_MENU]&0x80)?8:0)|((keys[VK_CAPITAL]&1)?2:0)|(release?(1<<30):0);
    switch (vk) {
    case VK_BACK:return 0xff08; case VK_TAB:return 0xff09; case VK_RETURN:return 0xff0d;
    case VK_ESCAPE:return 0xff1b; case VK_DELETE:return 0xffff;
    case VK_HOME:return 0xff50; case VK_END:return 0xff57;
    case VK_LEFT:return 0xff51; case VK_UP:return 0xff52; case VK_RIGHT:return 0xff53; case VK_DOWN:return 0xff54;
    case VK_PRIOR:return 0xff55; case VK_NEXT:return 0xff56;
    case VK_SHIFT:case VK_LSHIFT:return 0xffe1; case VK_RSHIFT:return 0xffe2;
    }
    wchar_t buffer[8]{};
    int n=ToUnicodeEx(static_cast<UINT>(vk),(static_cast<UINT>(info)>>16)&0xff,keys,buffer,8,4,GetKeyboardLayout(0));
    return n==1 && buffer[0]<0x80 ? buffer[0] : 0;
}
HRESULT collapse_selection(ITfContext* c,ITfRange* r,TfEditCookie ec) {
    ComPtr<ITfRange> caret; auto hr=r->Clone(&caret); if (FAILED(hr)) return hr;
    hr=caret->Collapse(ec,TF_ANCHOR_END); if (FAILED(hr)) return hr;
    TF_SELECTION s{caret.Get(),{TF_AE_NONE,FALSE}}; return c->SetSelection(ec,1,&s);
}
}
HRESULT WindowsInputAdapter::QueryInterface(REFIID iid,void** out) {
    if (!out) return E_POINTER; *out=nullptr;
    if (iid==IID_IUnknown || iid==IID_ITfTextInputProcessor || iid==IID_ITfTextInputProcessorEx) *out=static_cast<ITfTextInputProcessorEx*>(this);
    else if (iid==IID_ITfKeyEventSink) *out=static_cast<ITfKeyEventSink*>(this);
    else if (iid==IID_ITfTextEditSink) *out=static_cast<ITfTextEditSink*>(this);
    else if (iid==IID_ITfThreadMgrEventSink) *out=static_cast<ITfThreadMgrEventSink*>(this);
    else if (iid==IID_ITfTextLayoutSink) *out=static_cast<ITfTextLayoutSink*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
HRESULT WindowsInputAdapter::ActivateEx(ITfThreadMgr* manager,TfClientId id,DWORD flags) {
    if (!manager) return E_INVALIDARG;
    if (manager_) return E_UNEXPECTED;
    if (flags & TF_TMAE_UIELEMENTENABLEDONLY) return E_NOTIMPL;
    try {
        composition_observer_.Attach(new CompositionObserver);
        wchar_t module[32768]; auto n=GetModuleFileNameW(g_module,module,32768);
        if (!n || n>=32768) return E_FAIL;
        engine_.open(std::filesystem::path(module).parent_path());
        if (!window_.create(g_module,this,click)) { engine_.close(); return E_FAIL; }
        manager_=manager; client_=id;
        ComPtr<ITfSource> source; auto hr=manager_.As(&source);
        if (SUCCEEDED(hr)) hr=source->AdviseSink(IID_ITfThreadMgrEventSink,static_cast<ITfThreadMgrEventSink*>(this),&manager_cookie_);
        if (FAILED(hr)) log_failure("Advise thread manager sink failed");
        ComPtr<ITfKeystrokeMgr> keys;
        if (SUCCEEDED(hr)) hr=manager_.As(&keys);
        if (SUCCEEDED(hr)) hr=keys->AdviseKeyEventSink(id,this,TRUE);
        if (FAILED(hr)) log_failure("Advise keyboard sink failed");
        if (FAILED(hr)) { Deactivate(); return hr; }
        ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context;
        if (SUCCEEDED(manager_->GetFocus(&document)) && document && SUCCEEDED(document->GetTop(&context))) switch_context(context.Get());
        OutputDebugStringW(L"MYIME: TSF activated\n"); return S_OK;
    } catch (const std::exception& e) { log_failure(e.what()); Deactivate(); return E_FAIL; }
    catch (...) { Deactivate(); return E_UNEXPECTED; }
}
void WindowsInputAdapter::reset() {
    ++generation_; forwarded_.fill(false); window_.hide();
    if (composition_ && context_) {
        auto old=composition_; composition_.Reset();
        auto cancel=new(std::nothrow) Cancel(old.Get());
        if (cancel) { HRESULT result; context_->RequestEditSession(client_,cancel,TF_ES_ASYNCDONTCARE|TF_ES_READWRITE,&result); cancel->Release(); }
    }
    if (engine_.handle) engine_.clear(engine_.handle);
    faulted_=false;
}
void WindowsInputAdapter::switch_context(ITfContext* c) {
    if (context_.Get()==c) return;
    reset();
    if (context_ && layout_cookie_!=TF_INVALID_COOKIE) { ComPtr<ITfSource> s; if (SUCCEEDED(context_.As(&s))) s->UnadviseSink(layout_cookie_); }
    if (context_ && edit_cookie_!=TF_INVALID_COOKIE) { ComPtr<ITfSource> s; if (SUCCEEDED(context_.As(&s))) s->UnadviseSink(edit_cookie_); }
    edit_cookie_=TF_INVALID_COOKIE;
    layout_cookie_=TF_INVALID_COOKIE; context_=c;
    if (engine_.handle) {
        try { if (!engine_.profile()) log_failure(engine_.last_error()); }
        catch (...) { engine_.enabled=false; log_failure("AppProfile resolution failed"); }
    }
    if (context_) {
        ComPtr<ITfSource> s;
        if (SUCCEEDED(context_.As(&s))) {
            s->AdviseSink(IID_ITfTextLayoutSink,static_cast<ITfTextLayoutSink*>(this),&layout_cookie_);
            s->AdviseSink(IID_ITfTextEditSink,static_cast<ITfTextEditSink*>(this),&edit_cookie_);
        }
    }
}
HRESULT WindowsInputAdapter::Deactivate() {
    switch_context(nullptr);
    if (manager_) {
        ComPtr<ITfKeystrokeMgr> keys; if (SUCCEEDED(manager_.As(&keys))) keys->UnadviseKeyEventSink(client_);
        ComPtr<ITfSource> s; if (manager_cookie_!=TF_INVALID_COOKIE && SUCCEEDED(manager_.As(&s))) s->UnadviseSink(manager_cookie_);
    }
    manager_cookie_=TF_INVALID_COOKIE; manager_.Reset(); client_=TF_CLIENTID_NULL;
    window_.destroy(); engine_.close(); composition_observer_.Reset(); return S_OK;
}
bool WindowsInputAdapter::eligible(ITfContext* c,WPARAM key) {
    if (!engine_.handle || !engine_.enabled || !c || faulted_ || key>=256) return false;
    if (GetKeyState(VK_CONTROL)<0 || GetKeyState(VK_MENU)<0 || GetKeyState(VK_LWIN)<0 || GetKeyState(VK_RWIN)<0) return false;
    if (disabled(c,GUID_COMPARTMENT_KEYBOARD_DISABLED) || disabled(c,GUID_COMPARTMENT_EMPTYCONTEXT)) return false;
    TF_STATUS status{}; if (FAILED(c->GetStatus(&status)) || (status.dwDynamicFlags&TF_SD_READONLY)) return false;
    MyimeState state{}; if (engine_.state(engine_.handle,&state)) return false;
    if (state.active) return key!=VK_CAPITAL;
    return (key>='A' && key<='Z') || key==VK_SHIFT || key==VK_LSHIFT || key==VK_RSHIFT;
}
HRESULT WindowsInputAdapter::OnTestKeyDown(ITfContext* c,WPARAM k,LPARAM,BOOL* eaten) {
    if (!eaten) return E_POINTER; *eaten=eligible(c,k); return S_OK;
}
HRESULT WindowsInputAdapter::OnTestKeyUp(ITfContext*,WPARAM k,LPARAM,BOOL* eaten) {
    if (!eaten) return E_POINTER; *eaten=k<256 && forwarded_[k]; return S_OK;
}
HRESULT WindowsInputAdapter::request(ITfContext* c,int a,int k,int m,bool sync,BOOL* eaten) {
    auto edit=new(std::nothrow) Edit(this,c,a,k,m,generation_); if (!edit) return E_OUTOFMEMORY;
    HRESULT result=E_FAIL;
    auto hr=c->RequestEditSession(client_,edit,(sync?TF_ES_SYNC:TF_ES_ASYNC)|(a==-4?TF_ES_READ:TF_ES_READWRITE),&result);
    if (eaten) *eaten=edit->eaten; edit->Release(); return FAILED(hr)?hr:result;
}
HRESULT WindowsInputAdapter::OnKeyDown(ITfContext* c,WPARAM k,LPARAM l,BOOL* eaten) {
    if (!eaten) return E_POINTER; *eaten=FALSE;
    if (!eligible(c,k)) return S_OK;
    switch_context(c);
    int mask=0; int symbol=translate(k,l,mask,false); if (!symbol) return S_OK;
    auto hr=request(c,-3,symbol,mask,true,eaten);
    if (*eaten || SUCCEEDED(hr)) forwarded_[k]=true;
    if (FAILED(hr)) log_failure("Key edit session failed"); return S_OK;
}
HRESULT WindowsInputAdapter::OnKeyUp(ITfContext* c,WPARAM k,LPARAM l,BOOL* eaten) {
    if (!eaten) return E_POINTER; *eaten=FALSE;
    if (k>=256 || !forwarded_[k] || c!=context_.Get()) return S_OK;
    forwarded_[k]=false;
    int mask=0; int symbol=translate(k,l,mask,true); if (symbol) request(c,-3,symbol,mask,true,eaten); return S_OK;
}
HRESULT WindowsInputAdapter::edit(TfEditCookie ec,ITfContext* c,int action,int key,int mask,unsigned long long generation,BOOL* eaten) {
    if (!engine_.handle || c!=context_.Get() || generation!=generation_) return S_FALSE;
    try {
        if (action==-4) return position(ec,c);
        if (faulted_) return E_FAIL;
        ComPtr<ITfReadOnlyProperty> prop; TF_SELECTION selection{}; ULONG fetched=0;
        if (SUCCEEDED(c->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&fetched)) && fetched) {
            ComPtr<ITfRange> range; range.Attach(selection.range);
            if (SUCCEEDED(c->GetAppProperty(GUID_PROP_INPUTSCOPE,&prop))) {
                VARIANT v; VariantInit(&v);
                if (SUCCEEDED(prop->GetValue(ec,range.Get(),&v)) && v.vt==VT_UNKNOWN && v.punkVal) {
                    ComPtr<ITfInputScope> scope;
                    if (SUCCEEDED(v.punkVal->QueryInterface(IID_PPV_ARGS(&scope)))) {
                        InputScope* scopes=nullptr; UINT count=0;
                        if (SUCCEEDED(scope->GetInputScopes(&scopes,&count))) {
                            bool password=false; for (UINT i=0;i<count;++i) if (scopes[i]==IS_PASSWORD) password=true;
                            CoTaskMemFree(scopes); if (password) { VariantClear(&v); return S_FALSE; }
                        }
                    }
                }
                VariantClear(&v);
            }
        }
        uint32_t handled=0; int rc=0;
        if (action>=0) { rc=engine_.select(engine_.handle,static_cast<size_t>(action)); handled=rc==0; }
        else rc=engine_.key(engine_.handle,key,mask,&handled);
        *eaten=handled!=0;
        if (rc) { log_failure(engine_.last_error()); return E_FAIL; }
        ++generation_;
        auto hr=apply(ec,c);
        if (FAILED(hr)) { faulted_=true; window_.hide(); log_failure("Document write failed; input suspended until focus change"); }
        return hr;
    } catch (const std::exception& e) { log_failure(e.what()); faulted_=true; window_.hide(); return E_FAIL; }
    catch (...) { faulted_=true; window_.hide(); return E_UNEXPECTED; }
}
HRESULT WindowsInputAdapter::apply(TfEditCookie ec,ITfContext* c) {
    MyimeState s{}; if (engine_.state(engine_.handle,&s)) return E_FAIL;
    auto commit=wide(s.commit),preedit=wide(s.preedit); HRESULT hr=S_OK;
    if (!commit.empty()) {
        ComPtr<ITfRange> r;
        if (composition_) {
            hr=composition_->GetRange(&r); if (SUCCEEDED(hr)) hr=r->SetText(ec,0,commit.data(),static_cast<LONG>(commit.size()));
        } else {
            ComPtr<ITfInsertAtSelection> insert; hr=c->QueryInterface(IID_PPV_ARGS(&insert));
            if (SUCCEEDED(hr)) hr=insert->InsertTextAtSelection(ec,0,commit.data(),static_cast<LONG>(commit.size()),&r);
        }
        if (FAILED(hr)) return hr;
        engine_.ack_commit(engine_.handle);
        if (composition_) { auto old=composition_; composition_.Reset(); hr=old->EndComposition(ec); if (FAILED(hr)) return hr; }
        hr=collapse_selection(c,r.Get(),ec); if (FAILED(hr)) return hr;
    }
    if (!s.active) {
        if (composition_) {
            ComPtr<ITfRange> r; hr=composition_->GetRange(&r); if (SUCCEEDED(hr)) hr=r->SetText(ec,0,L"",0); if (FAILED(hr)) return hr;
            auto old=composition_; composition_.Reset(); hr=old->EndComposition(ec);
        }
        window_.hide(); return hr;
    }
    ComPtr<ITfRange> r;
    if (!composition_) {
        ComPtr<ITfInsertAtSelection> insert; hr=c->QueryInterface(IID_PPV_ARGS(&insert)); if (FAILED(hr)) return hr;
        hr=insert->InsertTextAtSelection(ec,TF_IAS_QUERYONLY,L"",0,&r); if (FAILED(hr)) return hr;
        ComPtr<ITfContextComposition> compositions; hr=c->QueryInterface(IID_PPV_ARGS(&compositions)); if (FAILED(hr)) return hr;
        // Observe termination through the context's edit sink. On current Windows,
        // a rejected StartComposition can retain its optional composition sink.
        hr=compositions->StartComposition(ec,r.Get(),composition_observer_.Get(),&composition_); if (FAILED(hr) || !composition_) return FAILED(hr)?hr:E_FAIL;
    }
    hr=composition_->GetRange(&r); if (FAILED(hr)) return hr;
    hr=r->SetText(ec,0,preedit.data(),static_cast<LONG>(preedit.size())); if (FAILED(hr)) return hr;
    ComPtr<ITfRange> caret; hr=r->Clone(&caret); if (FAILED(hr)) return hr;
    hr=caret->Collapse(ec,TF_ANCHOR_START); if (FAILED(hr)) return hr;
    auto offset=wide({s.preedit.data,std::min(s.caret,s.preedit.len)}).size(); LONG moved=0;
    hr=caret->ShiftStart(ec,static_cast<LONG>(offset),&moved,nullptr); if (FAILED(hr)) return hr;
    hr=caret->Collapse(ec,TF_ANCHOR_START); if (FAILED(hr)) return hr;
    TF_SELECTION selection{caret.Get(),{TF_AE_NONE,FALSE}}; hr=c->SetSelection(ec,1,&selection); if (FAILED(hr)) return hr;
    return position(ec,c);
}
HRESULT WindowsInputAdapter::position(TfEditCookie ec,ITfContext* c) {
    MyimeState s{}; if (engine_.state(engine_.handle,&s)) return E_FAIL;
    if (!s.active) { window_.hide(); return S_OK; }
    ComPtr<ITfContextView> view; if (FAILED(c->GetActiveView(&view))) return S_OK;
    TF_SELECTION selection{}; ULONG count=0;
    if (FAILED(c->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&count)) || !count) return S_OK;
    ComPtr<ITfRange> caret; caret.Attach(selection.range); RECT r{}; BOOL clipped=FALSE;
    if (FAILED(view->GetTextExt(ec,caret.Get(),&r,&clipped)) || clipped || (!r.left && !r.right && !r.top && !r.bottom)) { window_.hide(); return S_OK; }
    window_.update(engine_,s,r); return S_OK;
}
void WindowsInputAdapter::click(void* owner,int candidate) {
    auto self=static_cast<WindowsInputAdapter*>(owner); if (!self->context_ || self->faulted_) return;
    self->request(self->context_.Get(),candidate>=0?candidate:-3,candidate==-1?0xff55:0xff56,0,false,nullptr);
}
HRESULT WindowsInputAdapter::OnEndEdit(ITfContext* c,TfEditCookie,ITfEditRecord*) {
    if (c!=context_.Get() || !composition_) return S_OK;
    ComPtr<ITfContextComposition> compositions; ComPtr<IEnumITfCompositionView> views;
    if (FAILED(c->QueryInterface(IID_PPV_ARGS(&compositions))) || FAILED(compositions->EnumCompositions(&views))) return S_OK;
    ComPtr<IUnknown> ours; composition_.As(&ours);
    ComPtr<ITfCompositionView> view; ULONG fetched=0;
    while (views->Next(1,&view,&fetched)==S_OK && fetched) {
        ComPtr<IUnknown> identity; view.As(&identity);
        if (identity.Get()==ours.Get()) return S_OK;
        view.Reset();
    }
    composition_.Reset(); if (engine_.handle) engine_.clear(engine_.handle); ++generation_; window_.hide();
    return S_OK;
}
HRESULT WindowsInputAdapter::OnSetFocus(BOOL foreground) { if (!foreground) reset(); return S_OK; }
HRESULT WindowsInputAdapter::OnSetFocus(ITfDocumentMgr* d,ITfDocumentMgr*) { ComPtr<ITfContext> c; if (d) d->GetTop(&c); switch_context(c.Get()); return S_OK; }
HRESULT WindowsInputAdapter::OnPushContext(ITfContext* c) { switch_context(c); return S_OK; }
HRESULT WindowsInputAdapter::OnPopContext(ITfContext* c) { if (c==context_.Get()) switch_context(nullptr); return S_OK; }
HRESULT WindowsInputAdapter::OnLayoutChange(ITfContext* c,TfLayoutCode code,ITfContextView*) {
    if (c==context_.Get()) { if (code==TF_LC_DESTROY) switch_context(nullptr); else if (composition_) request(c,-4,0,0,false,nullptr); } return S_OK;
}
