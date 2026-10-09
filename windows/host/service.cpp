#include "service.h"
#include <initguid.h>
#include <inputscope.h>
#include <new>
#include <algorithm>
#include <cstdio>
#include "data_paths.h"
#include "functions.h"
namespace {
bool owned_text(ITfContext*,ITfComposition*,TfEditCookie,const std::wstring&,ComPtr<ITfRange>&);
CandidatePresentation presentation(Engine& engine,const MyimeState& state) {
    CandidatePresentation view;
    view.active=state.active!=0; view.preedit=wide(state.preedit);
    view.caret=wide({state.preedit.data,std::min(state.caret,state.preedit.len)}).size();
    view.selected=state.selected; view.page=state.page; view.last_page=state.last_page!=0;
    view.rows.reserve(state.count);
    for (size_t i=0;i<state.count;++i) {
        MyimeCandidate c{}; if (engine.candidate(engine.handle,i,&c)) throw std::runtime_error(engine.last_error());
        view.rows.push_back({wide(c.label),wide(c.text),wide(c.comment)});
    }
    return view;
}
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
class FocusEdit final : public ITfEditSession {
    long refs_=1;
    WindowsInputAdapter* owner_;
    std::shared_ptr<ContextSession> session_;
    unsigned long long serial_;
public:
    FocusEdit(WindowsInputAdapter* owner,std::shared_ptr<ContextSession> session) : owner_(owner),session_(std::move(session)),serial_(session_->serial) { owner_->AddRef(); InterlockedIncrement(&g_objects); }
    ~FocusEdit(){owner_->Release(); InterlockedDecrement(&g_objects);}
    STDMETHODIMP QueryInterface(REFIID id,void** out) override {if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_ITfEditSession)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    STDMETHODIMP_(ULONG) AddRef() override{return InterlockedIncrement(&refs_);}
    STDMETHODIMP_(ULONG) Release() override{auto n=InterlockedDecrement(&refs_);if(!n)delete this;return n;}
    STDMETHODIMP DoEditSession(TfEditCookie ec) override{try{return owner_->detach_context(ec,session_,serial_);}catch(...){return E_FAIL;}}
};
class CompositionCleanup final : public ITfEditSession {
    long refs_=1;
    ComPtr<ITfContext> context_;
    ComPtr<ITfComposition> composition_;
    std::wstring expected_;
public:
    CompositionCleanup(ITfContext* context,ITfComposition* composition,std::wstring expected):context_(context),composition_(composition),expected_(std::move(expected)) { InterlockedIncrement(&g_objects); }
    ~CompositionCleanup() { InterlockedDecrement(&g_objects); }
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfEditSession) return E_NOINTERFACE;
        *out=this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP DoEditSession(TfEditCookie ec) override {
        try{ComPtr<ITfRange> range;if(!owned_text(context_.Get(),composition_.Get(),ec,expected_,range))return S_FALSE;const auto hr=range->SetText(ec,0,L"",0);return FAILED(hr)?hr:composition_->EndComposition(ec);}catch(...){return E_FAIL;}
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
    case VK_CAPITAL:return 0xffe5;
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
void WindowsInputAdapter::log_failure(const char* message) {
    wchar_t text[512]{};
    MultiByteToWideChar(CP_UTF8,0,message,-1,text,_countof(text));
    diagnostics_.event(text,E_FAIL);
}
void WindowsInputAdapter::candidate_visibility_event(const wchar_t* reason,HRESULT result) {
    if (!diagnostics_.candidate_enabled()) return;
    wchar_t stage[512]{};
    swprintf_s(stage,L"Candidate visibility reason=%s context=%p hwnd=%p owner=%p requested=%d visible=%d",
        reason,static_cast<void*>(context_.Get()),static_cast<void*>(window_.hwnd()),
        static_cast<void*>(window_.parent()),candidate_element_ && candidate_element_->shown(),window_.visible()!=FALSE);
    diagnostics_.candidate_event(Diagnostics::CandidateChannel::Visibility,stage,result);
}
void WindowsInputAdapter::hide_candidates(const wchar_t* reason,HRESULT result) {
    window_.hide();
    candidate_visibility_event(reason,result);
}
HRESULT WindowsInputAdapter::QueryInterface(REFIID iid,void** out) {
    if (!out) return E_POINTER; *out=nullptr;
    if (iid==IID_IUnknown || iid==IID_ITfTextInputProcessor || iid==IID_ITfTextInputProcessorEx) *out=static_cast<ITfTextInputProcessorEx*>(this);
    else if (iid==IID_ITfKeyEventSink) *out=static_cast<ITfKeyEventSink*>(this);
    else if (iid==IID_ITfTextEditSink) *out=static_cast<ITfTextEditSink*>(this);
    else if (iid==IID_ITfThreadMgrEventSink) *out=static_cast<ITfThreadMgrEventSink*>(this);
    else if (iid==IID_ITfTextLayoutSink) *out=static_cast<ITfTextLayoutSink*>(this);
    else if (iid==IID_ITfFunctionProvider) *out=static_cast<ITfFunctionProvider*>(this);
    else return E_NOINTERFACE;
    AddRef(); return S_OK;
}
HRESULT WindowsInputAdapter::ActivateEx(ITfThreadMgr* manager,TfClientId id,DWORD flags) {
    if (!manager) return E_INVALIDARG;
    if (manager_) return E_UNEXPECTED;
    try {
        diagnostics_.event(L"Resolving data storage");
        auto paths=DataPaths::resolve();
        diagnostics_.configure(paths.root);
        wchar_t stage[96]{}; swprintf_s(stage,L"ActivateEx flags=0x%08lX",flags); diagnostics_.event(stage);
        diagnostics_.event(paths.app_container?L"AppContainer isolated storage":L"Desktop storage");
        diagnostics_.event(paths.root.c_str());
        diagnostics_.module(nullptr);
        diagnostics_.module(L"MSVCP140.dll");
        ComPtr<ITfThreadMgrEx> extended;
        DWORD active_flags=0;
        if (SUCCEEDED(manager->QueryInterface(IID_PPV_ARGS(&extended))) && SUCCEEDED(extended->GetActiveFlags(&active_flags))) {
            swprintf_s(stage,L"Thread manager flags=0x%08lX",active_flags); diagnostics_.event(stage);
        }
        composition_observer_.Attach(new CompositionObserver);
        wchar_t module[32768]; auto n=GetModuleFileNameW(g_module,module,32768);
        if (!n || n>=32768) { Deactivate(); return E_FAIL; }
        diagnostics_.event(L"Engine initialization begin");
        module_directory_=std::filesystem::path(module).parent_path();
        engine_.open(module_directory_,paths.root,diagnostics_);
        themes_.open(module_directory_,engine_.theme_root());
        settings_.configure(std::filesystem::path(module).parent_path(),paths.root,paths.app_container);
        update_theme();
        diagnostics_.event(L"Engine initialization complete");
        diagnostics_.module(L"myime_core.dll"); diagnostics_.module(L"rime.dll");
        manager_=manager; client_=id;
        start_settings_watch();
        auto hr=manager_.As(&ui_manager_);
        if (FAILED(hr)) { diagnostics_.event(L"UIElement manager unavailable",hr); Deactivate(); return hr; }
        hr=manager_.As(&language_bar_);
        if (SUCCEEDED(hr)) {
            mode_indicator_.Attach(new ModeIndicator(this,mode_owner,&diagnostics_,
                settings_.available()?open_settings:nullptr,select_group));
            UiLanguage language; language.open(module_directory_,engine_.theme_root()); mode_indicator_->language(std::move(language));
            hr=language_bar_->AddItem(mode_indicator_.Get()); mode_added_=SUCCEEDED(hr);
        }
        if (FAILED(hr)) { diagnostics_.event(L"Mode indicator registration failed",hr); Deactivate(); return hr; }
        update_mode();
        mode_indicator_->Show(TRUE);
        mode_indicator_->refresh();
        ComPtr<ITfSourceSingle> functions;
        hr=manager_.As(&functions);
        if (SUCCEEDED(hr)) { hr=functions->AdviseSingleSink(client_,IID_ITfFunctionProvider,static_cast<ITfFunctionProvider*>(this)); function_advised_=SUCCEEDED(hr); }
        if (FAILED(hr)) { diagnostics_.event(L"Function provider registration failed",hr); Deactivate(); return hr; }
        ComPtr<ITfSource> source; hr=manager_.As(&source);
        if (SUCCEEDED(hr)) hr=source->AdviseSink(IID_ITfThreadMgrEventSink,static_cast<ITfThreadMgrEventSink*>(this),&manager_cookie_);
        if (FAILED(hr)) log_failure("Advise thread manager sink failed");
        ComPtr<ITfKeystrokeMgr> keys;
        if (SUCCEEDED(hr)) hr=manager_.As(&keys);
        if (SUCCEEDED(hr)) { hr=keys->AdviseKeyEventSink(id,this,TRUE); key_sink_advised_=SUCCEEDED(hr); }
        if (FAILED(hr)) log_failure("Advise keyboard sink failed");
        if (FAILED(hr)) { diagnostics_.event(L"TSF sink registration failed",hr); Deactivate(); return hr; }
        ComPtr<ITfDocumentMgr> document; ComPtr<ITfContext> context;
        if (SUCCEEDED(manager_->GetFocus(&document)) && document && SUCCEEDED(document->GetTop(&context))) switch_context(context.Get());
        OutputDebugStringW(L"MYIME: TSF activated\n"); return S_OK;
    } catch (const std::exception& error) {
        const auto hr=exception_result(error);
        diagnostics_.failure(L"Activation failed during initialization",error);Deactivate();return hr;
    } catch (...) { diagnostics_.event(L"Unexpected activation exception",E_UNEXPECTED);Deactivate();return E_UNEXPECTED; }
}
namespace {
bool at_anchor(ITfContext* context,TfEditCookie ec,ITfRange* anchor) {
    if (!anchor) return false; TF_SELECTION selection{}; ULONG count=0;
    if (FAILED(context->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&count)) || count!=1) return false;
    ComPtr<ITfRange> range; range.Attach(selection.range); LONG start=1,end=1;
    return SUCCEEDED(range->CompareStart(ec,anchor,TF_ANCHOR_START,&start)) && SUCCEEDED(range->CompareEnd(ec,anchor,TF_ANCHOR_START,&end)) && start==0 && end==0;
}
bool owned_text(ITfContext* context,ITfComposition* composition,TfEditCookie ec,const std::wstring& expected,ComPtr<ITfRange>& range) {
    if (!composition || FAILED(composition->GetRange(&range))) return false;
    ComPtr<ITfContextComposition> owner; ComPtr<IEnumITfCompositionView> views; ComPtr<IUnknown> identity;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&owner))) || FAILED(owner->EnumCompositions(&views)) || FAILED(composition->QueryInterface(IID_PPV_ARGS(&identity)))) return false;
    bool found=false; ComPtr<ITfCompositionView> view; ULONG count=0;
    while(views->Next(1,&view,&count)==S_OK && count) { ComPtr<IUnknown> next; view.As(&next); if(next.Get()==identity.Get()){found=true;break;}view.Reset(); }
    if(!found || expected.size()>16384)return false;
    std::wstring text(expected.size()+1,L'\0'); ULONG length=0;
    return SUCCEEDED(range->GetText(ec,0,text.data(),static_cast<ULONG>(text.size()),&length)) && length==expected.size() && text.compare(0,length,expected)==0;
}
}
void WindowsInputAdapter::suspend_context() {
    if(!current_session_ || !current_session_->focused)return;
    auto session=current_session_; session->focused=false; session->touched=GetTickCount64(); session->composition=composition_;
    ++generation_; forwarded_.fill(false); end_candidates(); hide_candidates(L"focus-policy"); composition_.Reset(); faulted_=false;
    MyimeState state{}; if(engine_.state(session->core,&state)){session->valid=false;return;}
    if(!state.active && !state.commit.len && !session->composition){session->phase=ContextSession::Phase::Idle;return;}
    session->expected=wide(state.preedit); session->focus_policy=engine_.policy().focus; session->phase=ContextSession::Phase::Detaching; ++session->serial;
    if(session->edit_cookie==TF_INVALID_COOKIE && session->focus_policy==0)session->focus_policy=1;
    auto edit=new(std::nothrow) FocusEdit(this,session); if(!edit){session->valid=false;engine_.clear(session->core);return;}
    HRESULT result=E_FAIL; const auto hr=session->context->RequestEditSession(client_,edit,TF_ES_ASYNC|TF_ES_READWRITE,&result); edit->Release();
    if(FAILED(hr) || (FAILED(result) && result!=TF_S_ASYNC)){session->valid=false;session->phase=ContextSession::Phase::Invalid;engine_.clear(session->core);diagnostics_.event(L"Focus policy edit request denied; draft invalidated",FAILED(hr)?hr:result);}
}
HRESULT WindowsInputAdapter::detach_context(TfEditCookie ec,const std::shared_ptr<ContextSession>& session,unsigned long long serial) {
    if(!manager_ || !session->valid || !session->core || session->serial!=serial)return S_FALSE;
    try{
    if(session->focused && session==current_session_ && session->focus_policy==0){session->phase=ContextSession::Phase::Active;composition_=session->composition;BOOL ignored=FALSE;return edit(ec,session->context.Get(),-4,0,0,generation_,&ignored);}
    auto original=session->composition;ComPtr<ITfRange> range;
    if(!owned_text(session->context.Get(),original.Get(),ec,session->expected,range)){engine_.clear(session->core);session->phase=ContextSession::Phase::Invalid;session->valid=false;session->composition.Reset();return S_FALSE;}
    if(!session->valid || !session->core || session->serial!=serial)return S_FALSE;
    bool retain=session->focus_policy==0;
    // Retention is valid only while the original selection is in our composition.
    TF_SELECTION selection{};ULONG fetched=0;
    if(retain && SUCCEEDED(session->context->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&fetched)) && fetched){ComPtr<ITfRange> caret;caret.Attach(selection.range);LONG begin=0,end=0;retain=SUCCEEDED(caret->CompareStart(ec,range.Get(),TF_ANCHOR_START,&begin))&&SUCCEEDED(caret->CompareEnd(ec,range.Get(),TF_ANCHOR_END,&end))&&begin>=0&&end<=0;}else if(retain)retain=false;
    HRESULT hr=S_OK;std::wstring text;
    if(session->focus_policy==2){MyimeState state{};if(engine_.state(session->core,&state))return E_FAIL;if(!state.commit.len && engine_.commit_current(session->core))return E_FAIL;if(engine_.state(session->core,&state))return E_FAIL;text=wide(state.commit);}
    if(!session->valid || !session->core || session->serial!=serial)return S_FALSE;
    hr=range->SetText(ec,0,text.data(),static_cast<LONG>(text.size()));if(FAILED(hr)){session->valid=false;engine_.clear(session->core);return hr;}
    if(session->focus_policy==2 && engine_.ack_commit(session->core)){session->valid=false;return E_FAIL;}
    session->composition.Reset();hr=original->EndComposition(ec);if(FAILED(hr)){session->valid=false;engine_.clear(session->core);return hr;}
    if(retain){hr=range->Collapse(ec,TF_ANCHOR_START);if(SUCCEEDED(hr))hr=collapse_selection(session->context.Get(),range.Get(),ec);if(SUCCEEDED(hr)){session->anchor=range;session->phase=ContextSession::Phase::AwaitingEndEdit;}else{session->valid=false;engine_.clear(session->core);}}
    else{engine_.clear(session->core);session->anchor.Reset();session->phase=ContextSession::Phase::Idle;}
    if(session->focused && session==current_session_)composition_.Reset();
    return hr;
    }catch(...){session->valid=false;session->phase=ContextSession::Phase::Invalid;if(session->core)engine_.clear(session->core);diagnostics_.event(L"Focus policy failed; draft invalidated",E_FAIL);return E_FAIL;}
}
HRESULT WindowsInputAdapter::restore_context(TfEditCookie ec) {
    auto session=current_session_;if(!session || !session->focused)return S_FALSE;
    if(session->phase==ContextSession::Phase::Detaching)return S_FALSE;
    if(session->phase==ContextSession::Phase::AwaitingEndEdit)return S_FALSE;
    if(session->phase!=ContextSession::Phase::Suspended)return S_OK;
    if(!session->valid || !at_anchor(session->context.Get(),ec,session->anchor.Get())){engine_.clear(session->core);session->anchor.Reset();session->phase=ContextSession::Phase::Idle;return S_FALSE;}
    session->phase=ContextSession::Phase::Active;session->anchor.Reset();auto hr=apply(ec,session->context.Get());session->composition=composition_;if(FAILED(hr)){session->valid=false;engine_.clear(session->core);}return hr;
}
HRESULT WindowsInputAdapter::context_changed(void* owner,ContextSession* session,TfEditCookie ec,ITfEditRecord* changes) {
    auto self=static_cast<WindowsInputAdapter*>(owner);if(!session->valid)return S_OK;
    if(session->phase==ContextSession::Phase::AwaitingEndEdit){session->phase=ContextSession::Phase::Suspended;if(session->focused && session==self->current_session_.get())self->request(session->context.Get(),-6,0,0,false,nullptr);return S_OK;}
    if(session->phase==ContextSession::Phase::Suspended){
        ComPtr<IEnumTfRanges> ranges;ComPtr<ITfRange> range;ULONG fetched=0;BOOL selection=FALSE;
        bool text=changes && SUCCEEDED(changes->GetTextAndPropertyUpdates(TF_GTP_INCL_TEXT,nullptr,0,&ranges)) && ranges && ranges->Next(1,&range,&fetched)==S_OK && fetched;
        if(changes)changes->GetSelectionStatus(&selection);
        if(text || (selection && !at_anchor(session->context.Get(),ec,session->anchor.Get()))){session->valid=false;session->phase=ContextSession::Phase::Invalid;self->engine_.clear(session->core);session->anchor.Reset();}
        return S_OK;
    }
    return session==self->current_session_.get()?self->OnEndEdit(session->context.Get(),ec,changes):S_OK;
}
void WindowsInputAdapter::discard_context(const std::shared_ptr<ContextSession>& session) {
    session->valid=false;++session->serial;if(session->observer)session->observer->detach();
    if(session->edit_cookie!=TF_INVALID_COOKIE){ComPtr<ITfSource> source;if(SUCCEEDED(session->context.As(&source)))source->UnadviseSink(session->edit_cookie);session->edit_cookie=TF_INVALID_COOKIE;}
    if(session->composition && client_!=TF_CLIENTID_NULL){
        auto cleanup=new(std::nothrow) CompositionCleanup(session->context.Get(),session->composition.Get(),std::move(session->expected));
        if(cleanup){HRESULT result=E_FAIL;session->context->RequestEditSession(client_,cleanup,TF_ES_ASYNC|TF_ES_READWRITE,&result);cleanup->Release();}
    }
    engine_.release_session(session->core);session->core=nullptr;session->composition.Reset();session->anchor.Reset();
}
void WindowsInputAdapter::switch_context(ITfContext* c) {
    if(context_.Get()==c && ((!c && !current_session_) || (current_session_ && current_session_->focused && current_session_->valid && current_session_->core==engine_.handle && !faulted_)))return;
    try{
    wchar_t stage[160]{};
    swprintf_s(stage,L"Candidate context old=%p new=%p",static_cast<void*>(context_.Get()),static_cast<void*>(c));
    diagnostics_.candidate_event(Diagnostics::CandidateChannel::Context,stage);
    suspend_context();
    if (context_ && layout_cookie_!=TF_INVALID_COOKIE) { ComPtr<ITfSource> s; if (SUCCEEDED(context_.As(&s))) s->UnadviseSink(layout_cookie_); }
    layout_cookie_=TF_INVALID_COOKIE; context_=c;
    current_session_.reset();
    engine_.handle=nullptr;
    if(c) {
        ComPtr<IUnknown> identity;if(FAILED(c->QueryInterface(IID_PPV_ARGS(&identity))))throw std::runtime_error("Cannot identify input context");
        for(const auto& session:sessions_)if(session->identity.Get()==identity.Get()){current_session_=session;break;}
        if(!current_session_){
            auto session=std::make_shared<ContextSession>();session->context=c;session->identity=identity;ComPtr<ITfDocumentMgr> document;if(SUCCEEDED(c->GetDocumentMgr(&document)) && document)document.As(&session->document_identity);session->core=engine_.take_session();
            try{session->observer.Attach(new ContextObserver(this,session.get(),context_changed));ComPtr<ITfSource> source;if(SUCCEEDED(session->context.As(&source))){const auto advised=source->AdviseSink(IID_ITfTextEditSink,static_cast<ITfTextEditSink*>(session->observer.Get()),&session->edit_cookie);if(FAILED(advised))diagnostics_.event(L"Context edit observer unavailable; retention will cancel",advised);}sessions_.push_back(session);current_session_=session;}
            catch(...){discard_context(session);throw;}
        }
        current_session_->focused=true;current_session_->touched=GetTickCount64();engine_.use_session(current_session_->core);composition_=current_session_->phase==ContextSession::Phase::Active?current_session_->composition:nullptr;
        faulted_=false;sync_learning_group(true);
        if(!current_session_->valid){engine_.clear(engine_.handle);current_session_->valid=true;current_session_->phase=ContextSession::Phase::Idle;}
        if(current_session_->phase==ContextSession::Phase::Suspended)request(c,-6,0,0,false,nullptr);
    }
    if (c && engine_.handle) {
        try { update_theme(); }
        catch (...) { engine_.enabled=false; log_failure("AppProfile resolution failed"); }
        update_mode();
    }
    if (context_) {
        ComPtr<ITfSource> s;
        if (SUCCEEDED(context_.As(&s))) {
            s->AdviseSink(IID_ITfTextLayoutSink,static_cast<ITfTextLayoutSink*>(this),&layout_cookie_);
        }
    }
    trim_sessions();
    }catch(const std::exception& error){log_failure(error.what());engine_.handle=nullptr;engine_.enabled=false;faulted_=true;composition_.Reset();hide_candidates(L"context-initialization-failed");}
    catch(...){engine_.handle=nullptr;engine_.enabled=false;faulted_=true;composition_.Reset();hide_candidates(L"context-exception");}
}
void WindowsInputAdapter::trim_sessions(){
    const auto now=GetTickCount64();
    for(auto iterator=sessions_.begin();iterator!=sessions_.end();) { const auto& session=*iterator; if(session!=current_session_ && ((sessions_.size()>32 && session->phase!=ContextSession::Phase::Detaching) || now-session->touched>300000)){discard_context(session);iterator=sessions_.erase(iterator);}else++iterator; }
}
void WindowsInputAdapter::start_settings_watch() {
    WNDCLASSEXW type{sizeof(type)};type.lpfnWndProc=settings_message;type.hInstance=g_module;type.lpszClassName=L"MYIME.SettingsWatch.v1";
    if(!RegisterClassExW(&type) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Cannot register settings watcher");
    settings_watch_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,type.lpszClassName,L"",WS_POPUP,0,0,0,0,nullptr,nullptr,g_module,this);
    if(!settings_watch_ || !SetTimer(settings_watch_,1,1000,nullptr))throw std::runtime_error("Cannot start settings watcher");
}
LRESULT CALLBACK WindowsInputAdapter::settings_message(HWND window,UINT message,WPARAM w,LPARAM l) {
    if(message==WM_NCCREATE){auto create=reinterpret_cast<CREATESTRUCTW*>(l);SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));}
    auto self=reinterpret_cast<WindowsInputAdapter*>(GetWindowLongPtrW(window,GWLP_USERDATA));
    static const UINT changed=RegisterWindowMessageW(L"MYIME.PresentationChanged.v1");
    if(self && (message==WM_TIMER || message==changed)){self->refresh_settings(message==changed);return 0;}
    if(message==WM_NCDESTROY)SetWindowLongPtrW(window,GWLP_USERDATA,0);
    return DefWindowProcW(window,message,w,l);
}
void WindowsInputAdapter::refresh_settings(bool force) {
    if(!manager_)return;
    try {
        trim_sessions();engine_.apply_pending();sync_learning_group();if(!engine_.handle)return;
        auto stamp=[](const std::filesystem::path& path)->ULONGLONG{WIN32_FILE_ATTRIBUTE_DATA data{};if(!GetFileAttributesExW(path.c_str(),GetFileExInfoStandard,&data))return 0;return (static_cast<ULONGLONG>(data.ftLastWriteTime.dwHighDateTime)<<32)|data.ftLastWriteTime.dwLowDateTime;};
        const auto root=engine_.theme_root();MyimeText selected{};if(engine_.ui_theme_id(engine_.handle,&selected))throw std::runtime_error(engine_.last_error());
        const auto config_time=stamp(engine_.config_path()),theme_time=stamp(root/L"themes"/wide(selected)/L"theme.toml"),language_time=stamp(root/L"ui-language.txt");
        if(!force && root==presentation_generation_ && config_time==config_stamp_ && theme_time==theme_stamp_ && language_time==language_stamp_)return;
        engine_.refresh();themes_.open(module_directory_,root);update_theme(true);presentation_generation_=root;config_stamp_=config_time;theme_stamp_=theme_time;
        if(mode_indicator_){UiLanguage language;language.open(module_directory_,root);mode_indicator_->language(std::move(language));}language_stamp_=language_time;
        sync_learning_group(true);update_learning_menu();
        if(composition_ && current_session_ && current_session_->focused)request(context_.Get(),-4,0,0,false,nullptr);
        diagnostics_.event(L"Product settings and candidate theme refreshed");
    } catch(const std::exception& error){log_failure(error.what());}
}
HRESULT WindowsInputAdapter::Deactivate() {
    if(settings_watch_){KillTimer(settings_watch_,1);DestroyWindow(settings_watch_);settings_watch_=nullptr;}
    if (mode_indicator_) mode_indicator_->detach();
    if (mode_added_ && language_bar_) language_bar_->RemoveItem(mode_indicator_.Get());
    mode_added_=false; mode_indicator_.Reset(); language_bar_.Reset();
    switch_context(nullptr);
    for(const auto& session:sessions_)discard_context(session);sessions_.clear();current_session_.reset();composition_.Reset();
    if (manager_) {
        ComPtr<ITfKeystrokeMgr> keys; if (key_sink_advised_ && SUCCEEDED(manager_.As(&keys))) keys->UnadviseKeyEventSink(client_);
        ComPtr<ITfSource> s; if (manager_cookie_!=TF_INVALID_COOKIE && SUCCEEDED(manager_.As(&s))) s->UnadviseSink(manager_cookie_);
        ComPtr<ITfSourceSingle> functions; if (function_advised_ && SUCCEEDED(manager_.As(&functions))) functions->UnadviseSingleSink(client_,IID_ITfFunctionProvider);
    }
    end_candidates(); ui_manager_.Reset(); function_advised_=false;
    key_sink_advised_=false;
    manager_cookie_=TF_INVALID_COOKIE; manager_.Reset(); client_=TF_CLIENTID_NULL;
    window_.destroy(); themes_.close(); theme_id_.clear(); candidate_theme_={};
    settings_.clear();
    engine_.close(); composition_observer_.Reset(); return S_OK;
}
bool WindowsInputAdapter::eligible(ITfContext* c,WPARAM key,LPARAM info) {
    if (!engine_.handle || !engine_.enabled || !c || faulted_ || key>=256) return false;
    if (GetKeyState(VK_CONTROL)<0 || GetKeyState(VK_MENU)<0 || GetKeyState(VK_LWIN)<0 || GetKeyState(VK_RWIN)<0) return false;
    if (disabled(c,GUID_COMPARTMENT_KEYBOARD_DISABLED) || disabled(c,GUID_COMPARTMENT_EMPTYCONTEXT)) return false;
    TF_STATUS status{}; if (FAILED(c->GetStatus(&status)) || (status.dwDynamicFlags&TF_SD_READONLY)) return false;
    MyimeState state{}; if (engine_.state(engine_.handle,&state)) return false;
    if(current_session_ && (current_session_->phase==ContextSession::Phase::Detaching || current_session_->phase==ContextSession::Phase::Suspended || current_session_->phase==ContextSession::Phase::AwaitingEndEdit))return false;
    if (key==VK_CAPITAL) return (state.options&1)==0;
    if (state.active) return true;
    if ((GetKeyState(VK_CAPITAL)&1) && engine_.policy().caps_lock==1 && (state.options&1)==0)return false;
    if (key==VK_SHIFT || key==VK_LSHIFT || key==VK_RSHIFT) return true;
    int mask=0; const int symbol=translate(key,info,mask,false);
    return symbol>=0x20 && symbol<0x7f;
}
HRESULT WindowsInputAdapter::OnTestKeyDown(ITfContext* c,WPARAM k,LPARAM info,BOOL* eaten) {
    if (!eaten) return E_POINTER; *eaten=eligible(c,k,info); return S_OK;
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
    if (!eligible(c,k,l)) return S_OK;
    switch_context(c);
    int mask=0; int symbol=translate(k,l,mask,false); if (!symbol) return S_OK;
    auto hr=request(c,-3,symbol,mask,true,eaten);
    if (k!=VK_CAPITAL && *eaten) forwarded_[k]=true;
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
        if(action==-6)return restore_context(ec);
        uint32_t handled=0; int rc=0;
        if (action>=0) { rc=engine_.select(engine_.handle,static_cast<size_t>(action)); handled=rc==0; }
        else if (action==-5) { rc=engine_.clear(engine_.handle); handled=rc==0; }
        else rc=engine_.key(engine_.handle,key,mask,&handled);
        *eaten=handled!=0;
        if (rc) {
            end_candidates(); candidate_visibility_event(L"core-input-failure-retain-window",E_FAIL);
            log_failure("Core input operation failed"); return E_FAIL;
        }
        ++generation_;
        if(const auto status=engine_.learning_status()){wchar_t message[128]{};swprintf_s(message,L"Shared learning read/write failed code=%d; native input retained",status);diagnostics_.event(message,E_FAIL);}
        auto hr=apply(ec,c);
        if(current_session_){current_session_->composition=composition_;current_session_->phase=composition_?ContextSession::Phase::Active:ContextSession::Phase::Idle;}
        update_mode();
        if (FAILED(hr)) { faulted_=true; end_candidates(); hide_candidates(L"document-or-ui-failure",hr); diagnostics_.event(L"Document/UI update failed; input suspended until focus change",hr); }
        return hr;
    } catch (...) { log_failure("Input edit exception"); faulted_=true; end_candidates(); hide_candidates(L"edit-exception",E_FAIL); return E_FAIL; }
}
HRESULT WindowsInputAdapter::apply(TfEditCookie ec,ITfContext* c) {
    MyimeState s{}; if (engine_.state(engine_.handle,&s)) return E_FAIL;
    auto commit=wide(s.commit),preedit=wide(s.preedit); HRESULT hr=S_OK;
    const auto caret_offset=wide({s.preedit.data,std::min(s.caret,s.preedit.len)}).size();
    if (!commit.empty()) {
        ComPtr<ITfRange> r;
        if (composition_) {
            hr=composition_->GetRange(&r); if (SUCCEEDED(hr)) hr=r->SetText(ec,0,commit.data(),static_cast<LONG>(commit.size()));
        } else {
            ComPtr<ITfInsertAtSelection> insert; hr=c->QueryInterface(IID_PPV_ARGS(&insert));
            if (SUCCEEDED(hr)) hr=insert->InsertTextAtSelection(ec,0,commit.data(),static_cast<LONG>(commit.size()),&r);
        }
        if (FAILED(hr)) return hr;
        if(engine_.ack_commit(engine_.handle))return E_FAIL;
        if (composition_) { auto old=composition_; composition_.Reset(); hr=old->EndComposition(ec); if (FAILED(hr)) return hr; }
        hr=collapse_selection(c,r.Get(),ec); if (FAILED(hr)) return hr;
    }
    if (!s.active) {
        if (composition_) {
            ComPtr<ITfRange> r; hr=composition_->GetRange(&r); if (SUCCEEDED(hr)) hr=r->SetText(ec,0,L"",0); if (FAILED(hr)) return hr;
            auto old=composition_; composition_.Reset(); hr=old->EndComposition(ec);
        }
        if(current_session_)current_session_->expected.clear();
        end_candidates(); hide_candidates(L"composition-inactive",hr); return hr;
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
    if(current_session_){current_session_->composition=composition_;current_session_->expected=preedit;}
    ComPtr<ITfRange> caret; hr=r->Clone(&caret); if (FAILED(hr)) return hr;
    hr=caret->Collapse(ec,TF_ANCHOR_START); if (FAILED(hr)) return hr;
    LONG moved=0;
    hr=caret->ShiftStart(ec,static_cast<LONG>(caret_offset),&moved,nullptr); if (FAILED(hr)) return hr;
    hr=caret->Collapse(ec,TF_ANCHOR_START); if (FAILED(hr)) return hr;
    TF_SELECTION selection{caret.Get(),{TF_AE_NONE,FALSE}}; hr=c->SetSelection(ec,1,&selection); if (FAILED(hr)) return hr;
    return position(ec,c);
}
void WindowsInputAdapter::update_mode() {
    MyimeState state{};
    if (!mode_indicator_ || !engine_.handle || engine_.state(engine_.handle,&state)) return;
    const auto options=state.options;
    auto indicator=mode_indicator_;
    ComPtr<ITfCompartmentMgr> compartments;
    ComPtr<ITfCompartment> conversion;
    if (manager_ && SUCCEEDED(manager_.As(&compartments)) &&
        SUCCEEDED(compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_INPUTMODE_CONVERSION,&conversion))) {
        VARIANT previous; VariantInit(&previous);
        const auto hr=conversion->GetValue(&previous);
        LONG flags=SUCCEEDED(hr) && previous.vt==VT_I4?previous.lVal:0;
        const LONG next=(flags & ~(TF_CONVERSIONMODE_NATIVE|TF_CONVERSIONMODE_FULLSHAPE)) |
            ((options&1)?0:TF_CONVERSIONMODE_NATIVE) | ((options&2)?TF_CONVERSIONMODE_FULLSHAPE:0);
        const bool changed=previous.vt!=VT_I4 || flags!=next;
        VariantClear(&previous);
        if (changed) {
            VARIANT value; VariantInit(&value); value.vt=VT_I4; value.lVal=next;
            diagnostics_.event(L"Publish input conversion mode",conversion->SetValue(client_,&value));
        }
    }
    if (mode_indicator_.Get()==indicator.Get()) indicator->update((options&1)!=0);
    update_learning_menu();
}
HWND WindowsInputAdapter::mode_owner(void* owner) {
    auto self=static_cast<WindowsInputAdapter*>(owner);
    ComPtr<ITfContextView> view;
    HWND window=nullptr;
    if (self->context_ && SUCCEEDED(self->context_->GetActiveView(&view))) view->GetWnd(&window);
    if (!window) window=GetFocus();
    if (!window) window=GetForegroundWindow();
    return window && GetWindowThreadProcessId(window,nullptr)==GetCurrentThreadId()?window:nullptr;
}
HRESULT WindowsInputAdapter::position(TfEditCookie ec,ITfContext* c) {
    MyimeState s{}; if (engine_.state(engine_.handle,&s)) return E_FAIL;
    if (!s.active) { end_candidates(); hide_candidates(L"position-inactive"); return S_OK; }
    auto hr=publish_candidates(s); if (hr!=S_OK) return hr;
    // UIElement callbacks are external COM calls and may re-enter the service.
    if (!engine_.handle || c!=context_.Get()) return S_FALSE;
    if (engine_.state(engine_.handle,&s)) return E_FAIL;
    if (candidate_element_ && !candidate_element_->shown()) { hide_candidates(L"host-display-not-requested"); return S_OK; }
    ComPtr<ITfContextView> view;
    hr=c->GetActiveView(&view);
    if (FAILED(hr)) {
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Layout,L"Candidate GetActiveView unavailable; retain previous window",hr);
        candidate_visibility_event(L"active-view-unavailable",hr); return S_OK;
    }
    HWND parent=nullptr;
    const auto window_result=view->GetWnd(&parent);
    const HWND view_window=parent;
    if (FAILED(window_result) || !parent) parent=GetFocus();
    wchar_t stage[512]{};
    swprintf_s(stage,L"Candidate owner context=%p view=%p view_hwnd=%p resolved=%p previous=%p fallback=%d",
        static_cast<void*>(c),static_cast<void*>(view.Get()),static_cast<void*>(view_window),
        static_cast<void*>(parent),static_cast<void*>(window_.parent()),FAILED(window_result) || !view_window);
    diagnostics_.candidate_event(Diagnostics::CandidateChannel::Owner,stage,window_result);
    if (!parent) { hide_candidates(L"no-owner-hwnd",window_result); return S_OK; }
    TF_SELECTION selection{}; ULONG count=0;
    hr=c->GetSelection(ec,TF_DEFAULT_SELECTION,1,&selection,&count);
    if (FAILED(hr) || !count) {
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Layout,L"Candidate GetSelection unavailable; retain previous window",hr);
        candidate_visibility_event(L"selection-unavailable",hr); return S_OK;
    }
    ComPtr<ITfRange> caret; caret.Attach(selection.range); RECT r{}; BOOL clipped=FALSE;
    const auto layout=view->GetTextExt(ec,caret.Get(),&r,&clipped);
    const bool empty=!r.left && !r.right && !r.top && !r.bottom;
    swprintf_s(stage,L"Candidate caret layout clipped=%d empty=%d",clipped!=FALSE,empty);
    diagnostics_.candidate_event(Diagnostics::CandidateChannel::Layout,stage,layout);
    // A text change may temporarily invalidate layout. OnLayoutChange retries;
    // don't hide a valid same-context window between every character.
    if (c!=context_.Get() || !engine_.handle) return S_FALSE;
    // Layout calls may re-enter TSF; reacquire borrowed Core strings afterwards.
    if (engine_.state(engine_.handle,&s)) return E_FAIL;
    if (!s.active) { end_candidates(); hide_candidates(L"inactive-after-layout"); return S_OK; }
    if (candidate_element_ && !candidate_element_->shown()) { hide_candidates(L"display-revoked-after-layout"); return S_OK; }
    if (layout==TF_E_NOLAYOUT) {
        window_.refresh(presentation(engine_,s)); candidate_visibility_event(L"no-layout-retain-same-context",layout); return S_OK;
    }
    if (FAILED(layout)) { hide_candidates(L"layout-failure",layout); return S_OK; }
    if (clipped) { hide_candidates(L"caret-clipped",layout); return S_OK; }
    if (empty) { hide_candidates(L"caret-empty",layout); return S_OK; }
    if (!window_.created() && !window_.create(g_module,this,click,candidate_theme_,parent)) {
        candidate_visibility_event(L"window-create-failure",E_FAIL); return E_FAIL;
    }
    if(!window_.set_parent(parent)){diagnostics_.event(L"Candidate owner binding failed",HRESULT_FROM_WIN32(GetLastError()));hide_candidates(L"owner-binding-failed");return E_FAIL;}window_.present(presentation(engine_,s),r);
    candidate_visibility_event(L"layout-ready-update"); return S_OK;
}
void WindowsInputAdapter::click(void* owner,int candidate) {
    auto self=static_cast<WindowsInputAdapter*>(owner); if (!self->context_ || self->faulted_) return;
    self->request(self->context_.Get(),candidate>=0?candidate:-3,candidate==-1?0xff55:0xff56,0,false,nullptr);
}
void WindowsInputAdapter::update_theme(bool force) {
    MyimeText id{};
    if (engine_.ui_theme_id(engine_.handle,&id)) throw std::runtime_error(engine_.last_error());
    const auto requested=wide(id);
    if (!force && requested==theme_id_) return; // No catalog reads in per-key callbacks.
    auto theme=themes_.resolve(requested);
    if (!theme.warning.empty()) diagnostics_.event((L"Theme fallback: "+requested+L"; "+theme.warning).c_str());
    else diagnostics_.event((L"Theme loaded: "+theme.id).c_str());
    window_.set_theme(theme); candidate_theme_=std::move(theme); theme_id_=requested;
}
HRESULT WindowsInputAdapter::open_settings(void* owner) {
    auto self=static_cast<WindowsInputAdapter*>(owner);
    const auto result=self->settings_.open();
    self->diagnostics_.event(L"Open settings action",result); return result;
}
void WindowsInputAdapter::sync_learning_group(bool force){
    auto selected=ApplicationGroup::get();if(!force && selected.revision==group_revision_)return;
    MyimeCore* reference=engine_.handle;
    if(!reference)for(const auto& session:sessions_)if(session->core){reference=session->core;break;}
    if(!reference)return;
    if(!selected.id.empty()){
        MyimeLearningInfo info{};if(engine_.learning_info(reference,&info))throw std::runtime_error(engine_.last_error());bool found=false;
        for(size_t index=0;index<std::min<size_t>(info.group_count,32);++index){MyimeText id{},name{};if(engine_.learning_group(reference,index,&id,&name))throw std::runtime_error(engine_.last_error());if(utf8(wide(id))==selected.id){found=true;break;}}
        if(!found){ApplicationGroup::reset_if(selected);selected=ApplicationGroup::get();}
    }
    const auto application=utf8(CompatibilityLayer::executable_path());bool applied=false;
    for(const auto& session:sessions_)if(session->core){if(engine_.learning_context(session->core,application.c_str(),selected.id.c_str()))throw std::runtime_error(engine_.last_error());if(session->core==reference)applied=true;}
    if(!applied && engine_.learning_context(reference,application.c_str(),selected.id.c_str()))throw std::runtime_error(engine_.last_error());
    group_revision_=selected.revision;update_learning_menu();
}
void WindowsInputAdapter::update_learning_menu(){
    if(!mode_indicator_)return;
    mode_indicator_->availability(engine_.shared_learning_available(),engine_.shared_settings_available());
    if(!engine_.handle)return;MyimeLearningInfo info{};if(engine_.learning_info(engine_.handle,&info))return;
    const auto current=wide(info.group);std::vector<std::pair<std::wstring,std::wstring>> groups;
    for(size_t i=0;i<std::min<size_t>(info.group_count,32);i++){MyimeText id{},name{};if(engine_.learning_group(engine_.handle,i,&id,&name))return;groups.emplace_back(wide(id),wide(name));}
    mode_indicator_->groups(std::move(groups),current,info.habits_enabled!=0);
}
HRESULT WindowsInputAdapter::select_group(void* owner,const std::wstring& group){
    auto self=static_cast<WindowsInputAdapter*>(owner);if(!self->current_session_ || !self->engine_.handle)return S_FALSE;
    try{
        const auto id=utf8(group);if(self->engine_.learning_context(self->engine_.handle,utf8(CompatibilityLayer::executable_path()).c_str(),id.c_str()))return E_FAIL;
        ApplicationGroup::select(id);self->sync_learning_group(true);self->diagnostics_.event(L"Application learning group selected for this run (deferred while composing)");return S_OK;
    }catch(const std::exception& error){self->log_failure(error.what());return E_FAIL;}catch(...){return E_FAIL;}
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
    composition_.Reset();if(current_session_){current_session_->composition.Reset();current_session_->expected.clear();current_session_->phase=ContextSession::Phase::Idle;} if (engine_.handle) engine_.clear(engine_.handle); ++generation_; end_candidates(); hide_candidates(L"application-ended-composition");
    return S_OK;
}
HRESULT WindowsInputAdapter::OnSetFocus(BOOL foreground) {
    try{
    if (!foreground) suspend_context();
    else if (auto indicator=mode_indicator_) {
        if(context_)switch_context(context_.Get());
        indicator->Show(TRUE); update_mode();
        if (mode_indicator_.Get()==indicator.Get()) indicator->refresh();
    }
    return S_OK;
    }catch(...){log_failure("Foreground transition failed");faulted_=true;hide_candidates(L"foreground-transition-failed");return E_FAIL;}
}
HRESULT WindowsInputAdapter::OnSetFocus(ITfDocumentMgr* d,ITfDocumentMgr*) { ComPtr<ITfContext> c; if (d) d->GetTop(&c); switch_context(c.Get()); return S_OK; }
HRESULT WindowsInputAdapter::OnPushContext(ITfContext* c) { switch_context(c); return S_OK; }
HRESULT WindowsInputAdapter::OnPopContext(ITfContext* c) { if (c==context_.Get()) switch_context(nullptr);for(auto iterator=sessions_.begin();iterator!=sessions_.end();)if((*iterator)->context.Get()==c){discard_context(*iterator);iterator=sessions_.erase(iterator);}else++iterator;return S_OK; }
HRESULT WindowsInputAdapter::OnUninitDocumentMgr(ITfDocumentMgr* document){
    if(!document)return S_OK;
    ComPtr<IUnknown> removed;document->QueryInterface(IID_PPV_ARGS(&removed));
    if(current_session_ && current_session_->document_identity.Get()==removed.Get())switch_context(nullptr);
    for(auto iterator=sessions_.begin();iterator!=sessions_.end();){
        if(removed && (*iterator)->document_identity.Get()==removed.Get()){
            discard_context(*iterator);iterator=sessions_.erase(iterator);
        }else++iterator;
    }
    return S_OK;
}
HRESULT WindowsInputAdapter::OnLayoutChange(ITfContext* c,TfLayoutCode code,ITfContextView*) {
    if(code==TF_LC_DESTROY)return OnPopContext(c);
    if (c==context_.Get() && composition_) request(c,-4,0,0,false,nullptr); return S_OK;
}
void WindowsInputAdapter::end_candidates() {
    auto old=candidate_element_;
    candidate_element_.Reset();
    if (old) old->detach();
    const bool started=element_started_; const DWORD id=element_id_;
    element_started_=false;
    if (started && ui_manager_) {
        const auto hr=ui_manager_->EndUIElement(id);
        wchar_t stage[96]{}; swprintf_s(stage,L"Candidate EndUIElement id=%lu",id);
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,stage,hr);
    }
}
HRESULT WindowsInputAdapter::publish_candidates(const MyimeState& state) {
    if (!state.count) {
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,L"Candidate snapshot empty; end element");
        end_candidates(); return S_OK;
    }
    if (element_started_ && element_generation_==generation_) return S_OK;
    if (!ui_manager_ || !context_) {
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,L"Candidate manager/context unavailable",E_UNEXPECTED);
        return E_UNEXPECTED;
    }
    const auto generation=generation_;
    auto context=context_; auto manager=ui_manager_;
    std::vector<std::wstring> words; words.reserve(state.count);
    const auto selection=static_cast<UINT>(state.selected);
    for (size_t i=0;i<state.count;++i) {
        MyimeCandidate candidate{};
        if (engine_.candidate(engine_.handle,i,&candidate)) return E_FAIL;
        words.push_back(wide(candidate.text));
    }
    ComPtr<ITfDocumentMgr> document;
    auto hr=context->GetDocumentMgr(&document);
    if (FAILED(hr)) {
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,L"Candidate GetDocumentMgr failed",hr); return hr;
    }
    if (generation_!=generation || context_.Get()!=context.Get() || ui_manager_.Get()!=manager.Get()) return S_FALSE;
    if (element_started_ && candidate_element_) {
        auto element=candidate_element_;
        element->update(std::move(words),selection,generation);
        element_generation_=generation;
        hr=manager->UpdateUIElement(element_id_);
        wchar_t stage[96]{}; swprintf_s(stage,L"Candidate UpdateUIElement id=%lu",element_id_);
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,stage,hr);
        if (FAILED(hr)) return hr;
        return generation_==generation && candidate_element_.Get()==element.Get()?S_OK:S_FALSE;
    }
    auto element=new(std::nothrow) CandidateElement(document.Get(),std::move(words),
        selection,this,candidate_action,candidate_visibility,generation,candidate_is_visible);
    if (!element) return E_OUTOFMEMORY;
    candidate_element_.Attach(element); element_generation_=generation_;
    // One UI element per candidate session; subsequent keys update it in place.
    // Focus change, cancellation and completion detach it before EndUIElement.
    ComPtr<CandidateElement> keep_alive=candidate_element_;
    BOOL show=TRUE; DWORD id=0;
    hr=manager->BeginUIElement(element,&show,&id);
    wchar_t stage[160]{}; swprintf_s(stage,L"Candidate BeginUIElement id=%lu pbShow=%d context=%p",id,show!=FALSE,static_cast<void*>(context.Get()));
    diagnostics_.candidate_event(Diagnostics::CandidateChannel::Negotiation,stage,hr);
    if (FAILED(hr)) { end_candidates(); return hr; }
    if (candidate_element_.Get()!=element || element_generation_!=generation_) {
        element->detach(); const auto ended=manager->EndUIElement(id);
        swprintf_s(stage,L"Candidate stale Begin; EndUIElement id=%lu",id);
        diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,stage,ended);
        return S_FALSE;
    }
    element_id_=id; element_started_=true;
    element->initial_visibility(show);
    diagnostics_.event(show?L"Candidate UI: host draws":L"Candidate UI: application draws");
    hr=manager->UpdateUIElement(id);
    swprintf_s(stage,L"Candidate initial UpdateUIElement id=%lu",id);
    diagnostics_.candidate_event(Diagnostics::CandidateChannel::Lifecycle,stage,hr);
    if (FAILED(hr)) return hr;
    return generation_==generation && candidate_element_.Get()==element ? S_OK : S_FALSE;
}
HRESULT WindowsInputAdapter::candidate_action(void* owner,int action,unsigned long long generation) {
    auto self=static_cast<WindowsInputAdapter*>(owner);
    if (!self->context_ || self->faulted_ || self->generation_!=generation) return S_FALSE;
    return self->request(self->context_.Get(),action,0,0,false,nullptr);
}
void WindowsInputAdapter::candidate_visibility(void* owner,BOOL show) {
    auto self=static_cast<WindowsInputAdapter*>(owner);
    self->diagnostics_.candidate_event(Diagnostics::CandidateChannel::Show,show?L"Candidate Show request=true":L"Candidate Show request=false");
    if (!show) self->hide_candidates(L"Show-false");
    else if (self->context_) {
        const auto hr=self->request(self->context_.Get(),-4,0,0,false,nullptr);
        self->candidate_visibility_event(L"Show-true-layout-request",hr);
    } else self->candidate_visibility_event(L"Show-true-no-context");
}
BOOL WindowsInputAdapter::candidate_is_visible(void* owner) {
    auto self=static_cast<WindowsInputAdapter*>(owner);
    const BOOL visible=self->window_.visible();
    self->candidate_visibility_event(L"IsShown-query");
    return visible;
}
HRESULT WindowsInputAdapter::GetType(GUID* value) { if (!value) return E_POINTER; *value=kService; return S_OK; }
HRESULT WindowsInputAdapter::GetDescription(BSTR* value) {
    if (!value) return E_POINTER; *value=SysAllocString(L"MYIME functions"); return *value?S_OK:E_OUTOFMEMORY;
}
HRESULT WindowsInputAdapter::GetFunction(REFGUID function,REFIID iid,IUnknown** value) {
    if (!value) return E_POINTER; *value=nullptr;
    if (function!=GUID_NULL || iid!=IID_ITfFnGetPreferredTouchKeyboardLayout) return E_NOINTERFACE;
    auto layout=new(std::nothrow) TouchKeyboardLayout;
    if (!layout) return E_OUTOFMEMORY;
    *value=static_cast<ITfFnGetPreferredTouchKeyboardLayout*>(layout); return S_OK;
}
