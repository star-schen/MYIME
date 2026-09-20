#include "text_store.h"
#include <commctrl.h>
#include <imm.h>
#include <windowsx.h>
#include <vector>
#include <cstdio>
namespace {
class UiSink;
struct App {
    HWND main=nullptr,editor=nullptr,ordinary=nullptr,list=nullptr,log=nullptr,draw=nullptr,slots=nullptr,reject=nullptr;
    ComPtr<ITfThreadMgrEx> manager;
    ComPtr<ITfDocumentMgr> document;
    ComPtr<ITfContext> context;
    ComPtr<TextStore> store;
    ComPtr<ITfSource> source;
    ComPtr<ITfCandidateListUIElement> candidates;
    DWORD sink_cookie=TF_INVALID_COOKIE;
    UINT element=0;
    bool has_element=false;
    std::vector<UINT> visible_indices;
    bool app_draws() const { return SendMessageW(draw,BM_GETCHECK,0,0)==BST_CHECKED; }
    void note(const std::wstring& text) {
        if (SendMessageW(log,LB_GETCOUNT,0,0)>200) SendMessageW(log,LB_DELETESTRING,0,0);
        SendMessageW(log,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(log,LB_SETTOPINDEX,SendMessageW(log,LB_GETCOUNT,0,0)-1,0);
    }
    void refresh(UINT id) {
        ComPtr<ITfUIElementMgr> ui; ComPtr<ITfUIElement> element_ptr;
        if (FAILED(manager.As(&ui)) || FAILED(ui->GetUIElement(id,&element_ptr))) return;
        ComPtr<ITfCandidateListUIElement> c; if (FAILED(element_ptr.As(&c))) return;
        candidates=c; element=id; has_element=true;
        UINT total=0,selected=0,page=0,pages=0;
        c->GetCount(&total); c->GetSelection(&selected); c->GetCurrentPage(&page);
        c->GetPageIndex(nullptr,0,&pages);
        if (pages>4096) return;
        std::vector<UINT> offsets(pages);
        if (pages && FAILED(c->GetPageIndex(offsets.data(),pages,&pages))) return;
        const UINT start=page<offsets.size()?offsets[page]:0;
        const UINT end=page+1<offsets.size()?offsets[page+1]:total;
        const UINT capacity=SendMessageW(slots,CB_GETCURSEL,0,0)==0?7:9;
        SendMessageW(list,LB_RESETCONTENT,0,0); visible_indices.clear();
        for (UINT i=start;i<std::min(end,start+capacity);++i) {
            BSTR value=nullptr;
            if (SUCCEEDED(c->GetString(i,&value))) {
                std::wstring row=std::to_wstring(i)+L": "+(value?value:L""); SysFreeString(value);
                SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(row.c_str())); visible_indices.push_back(i);
            }
        }
        for (size_t i=0;i<visible_indices.size();++i) if (visible_indices[i]==selected) SendMessageW(list,LB_SETCURSEL,i,0);
        note(L"TSF candidates: count="+std::to_wstring(total)+L" page="+std::to_wstring(page)+L" start="+std::to_wstring(start)+L" next="+std::to_wstring(end)+L" visual slots="+std::to_wstring(capacity));
    }
    void close() {
        if (source && sink_cookie!=TF_INVALID_COOKIE) source->UnadviseSink(sink_cookie);
        sink_cookie=TF_INVALID_COOKIE; candidates.Reset();
        if (manager) manager->SetFocus(nullptr);
        if (document) document->Pop(TF_POPF_ALL);
        context.Reset(); document.Reset(); store.Reset(); source.Reset();
        if (manager) manager->Deactivate(); manager.Reset();
    }
};
class UiSink final:public ITfUIElementSink {
    long refs_=1; App* app_;
public:
    explicit UiSink(App* app):app_(app) {}
    STDMETHODIMP QueryInterface(REFIID iid,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (iid!=IID_IUnknown && iid!=IID_ITfUIElementSink) return E_NOINTERFACE;
        *out=this; AddRef(); return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs_); }
    STDMETHODIMP_(ULONG) Release() override { auto n=InterlockedDecrement(&refs_); if (!n) delete this; return n; }
    STDMETHODIMP BeginUIElement(DWORD id,BOOL* show) override {
        if (!show) return E_POINTER;
        *show=TRUE;
        try {
            ComPtr<ITfUIElementMgr> m; ComPtr<ITfUIElement> e; ComPtr<ITfCandidateListUIElement> c;
            if (SUCCEEDED(app_->manager.As(&m)) && SUCCEEDED(m->GetUIElement(id,&e)) && SUCCEEDED(e.As(&c))) *show=!app_->app_draws();
            app_->refresh(id); return S_OK;
        } catch (...) { return E_FAIL; }
    }
    STDMETHODIMP UpdateUIElement(DWORD id) override { try { app_->refresh(id); return S_OK; } catch (...) { return E_FAIL; } }
    STDMETHODIMP EndUIElement(DWORD id) override {
        if (app_->has_element && id==app_->element) { app_->has_element=false; app_->candidates.Reset(); app_->visible_indices.clear(); SendMessageW(app_->list,LB_RESETCONTENT,0,0); }
        return S_OK;
    }
};
LRESULT CALLBACK edit_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if (!app) return DefWindowProcW(hwnd,msg,w,l);
    if (msg==WM_LBUTTONDOWN) { SetFocus(hwnd); return 0; }
    if (msg==WM_SETFOCUS && app->manager) { app->manager->SetFocus(app->document.Get()); return 0; }
    if (msg==WM_KILLFOCUS && app->manager) { app->manager->SetFocus(nullptr); return 0; }
    if (msg==WM_CHAR && app->store) { app->store->type(static_cast<wchar_t>(w)); return 0; }
    if (msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps); RECT r; GetClientRect(hwnd,&r); FillRect(dc,&r,GetSysColorBrush(COLOR_WINDOW)); r.left+=12; r.top+=12;
        auto old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        if (app->store) DrawTextW(dc,app->store->text.c_str(),-1,&r,DT_SINGLELINE|DT_NOPREFIX);
        SelectObject(dc,old); EndPaint(hwnd,&ps); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
LRESULT CALLBACK legacy_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    auto app=reinterpret_cast<App*>(data);
    if (msg==WM_IME_STARTCOMPOSITION || msg==WM_IME_ENDCOMPOSITION || msg==WM_IME_COMPOSITION || msg==WM_IME_NOTIFY) {
        try { app->note(L"IMM32 msg="+std::to_wstring(msg)+L" flags="+std::to_wstring(static_cast<unsigned long>(l))); } catch (...) {}
    }
    if (msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,legacy_proc,1);
    return DefSubclassProc(hwnd,msg,w,l);
}
LRESULT CALLBACK main_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto app=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { app=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(app)); }
    if (msg==WM_COMMAND && app) {
        if (reinterpret_cast<HWND>(l)==app->reject && app->store) app->store->reject_composition=SendMessageW(app->reject,BM_GETCHECK,0,0)==BST_CHECKED;
    }
    if (msg==WM_CLOSE) { if (app) app->close(); DestroyWindow(hwnd); return 0; }
    if (msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,w,l);
}
LRESULT CALLBACK candidate_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR data) {
    auto app=reinterpret_cast<App*>(data);
    if (msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg==WM_LBUTTONDOWN) {
        const auto hit=SendMessageW(hwnd,LB_ITEMFROMPOINT,0,l);
        const size_t index=LOWORD(hit);
        if (!HIWORD(hit) && index<app->visible_indices.size() && app->candidates) {
            ComPtr<ITfCandidateListUIElementBehavior> behavior;
            if (SUCCEEDED(app->candidates.As(&behavior))) {
                const UINT candidate=app->visible_indices[index];
                if (SUCCEEDED(behavior->SetSelection(candidate))) behavior->Finalize();
            }
        }
        return 0; // Keep focus in the composition document.
    }
    if (msg==WM_NCDESTROY) RemoveWindowSubclass(hwnd,candidate_proc,1);
    return DefSubclassProc(hwnd,msg,w,l);
}
}
int wmain(int argc,wchar_t** argv) {
    const bool check=argc>1 && std::wstring(argv[1])==L"--self-check";
    const bool uiless=argc>1 && std::wstring(argv[1])==L"--uiless";
    if (FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED))) return 1;
    App app; HINSTANCE module=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.hInstance=module; wc.lpfnWndProc=main_proc; wc.lpszClassName=L"MYIME.Compatibility"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=GetSysColorBrush(COLOR_BTNFACE); RegisterClassW(&wc);
    wc.lpfnWndProc=edit_proc; wc.lpszClassName=L"MYIME.TestDocument"; RegisterClassW(&wc);
    app.main=CreateWindowW(L"MYIME.Compatibility",L"IME Compatibility Test App",WS_OVERLAPPEDWINDOW,100,100,920,640,nullptr,nullptr,module,&app);
    auto control=[&](const wchar_t* cls,const wchar_t* title,DWORD style,int x,int y,int width,int height,int id) {
        HWND h=CreateWindowW(cls,title,WS_CHILD|WS_VISIBLE|style,x,y,width,height,app.main,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
        SendMessageW(h,WM_SETFONT,reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)),TRUE); return h;
    };
    control(L"STATIC",L"Native Windows EDIT (IMM/TSF bridge; compare installed IMEs)",0,16,12,650,22,0);
    app.ordinary=control(L"EDIT",L"",WS_BORDER|ES_AUTOHSCROLL|WS_TABSTOP,16,38,850,40,1);
    SetWindowSubclass(app.ordinary,legacy_proc,1,reinterpret_cast<DWORD_PTR>(&app));
    control(L"STATIC",L"Custom TSF document (inline composition; click here to type)",0,16,92,800,22,0);
    app.editor=CreateWindowW(L"MYIME.TestDocument",L"",WS_CHILD|WS_VISIBLE|WS_BORDER|WS_TABSTOP,16,118,850,65,app.main,nullptr,module,&app);
    app.draw=control(L"BUTTON",L"Application draws TSF candidates",BS_AUTOCHECKBOX,16,198,290,28,2);
    app.reject=control(L"BUTTON",L"Reject composition",BS_AUTOCHECKBOX,320,198,170,28,3);
    app.slots=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST,510,198,150,160,4);
    SendMessageW(app.slots,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"7 visual slots")); SendMessageW(app.slots,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"9 visual slots")); SendMessageW(app.slots,CB_SETCURSEL,0,0);
    app.list=control(L"LISTBOX",L"",WS_BORDER|LBS_NOTIFY,16,242,310,285,5);
    SetWindowSubclass(app.list,candidate_proc,1,reinterpret_cast<DWORD_PTR>(&app));
    app.log=control(L"LISTBOX",L"",WS_BORDER|WS_VSCROLL,342,242,524,285,6);
    control(L"STATIC",L"Slot count changes presentation only. Logs retain the IME's actual page offsets. Click selects if supported.",0,16,540,850,36,0);
    if (!app.main || !app.editor || !app.ordinary || !app.list || !app.log || !app.draw || !app.slots || !app.reject) {
        if (app.main) DestroyWindow(app.main); CoUninitialize(); return 3;
    }
    HRESULT hr=CoCreateInstance(CLSID_TF_ThreadMgr,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&app.manager)); TfClientId client=0;
    if (SUCCEEDED(hr)) hr=app.manager->ActivateEx(&client,check?TF_TMAE_NOACTIVATETIP:(uiless?TF_TMAE_UIELEMENTENABLEDONLY:0));
    if (SUCCEEDED(hr)) hr=app.manager->CreateDocumentMgr(&app.document);
    app.store.Attach(new TextStore); app.store->window=app.editor; TfEditCookie cookie=0;
    if (SUCCEEDED(hr)) hr=app.document->CreateContext(client,0,static_cast<ITextStoreACP*>(app.store.Get()),&app.context,&cookie);
    if (SUCCEEDED(hr)) hr=app.document->Push(app.context.Get());
    if (SUCCEEDED(hr)) hr=app.manager.As(&app.source);
    ComPtr<UiSink> sink; sink.Attach(new UiSink(&app));
    if (SUCCEEDED(hr)) hr=app.source->AdviseSink(IID_ITfUIElementSink,sink.Get(),&app.sink_cookie);
    if (FAILED(hr) || check) {
        std::printf("Compatibility app initialization: 0x%08lx\n",hr);
        app.close(); DestroyWindow(app.main); CoUninitialize(); return FAILED(hr)?2:0;
    }
    app.note(uiless?L"UI-less mode: only participating TIPs activate":L"Normal mode: select an installed IME with Win+Space");
    ShowWindow(app.main,SW_SHOW); SetFocus(app.ordinary);
    ComPtr<ITfKeystrokeMgr> keys; app.manager.As(&keys);
    MSG msg{};
    while (GetMessageW(&msg,nullptr,0,0)>0) {
        if (msg.hwnd==app.editor && keys && (msg.message==WM_KEYDOWN || msg.message==WM_KEYUP)) {
            BOOL eaten=FALSE;
            if (msg.message==WM_KEYDOWN) { keys->TestKeyDown(msg.wParam,msg.lParam,&eaten); if (eaten) keys->KeyDown(msg.wParam,msg.lParam,&eaten); }
            else { keys->TestKeyUp(msg.wParam,msg.lParam,&eaten); if (eaten) keys->KeyUp(msg.wParam,msg.lParam,&eaten); }
            if (eaten) continue;
        }
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    keys.Reset(); sink.Reset(); app.close(); CoUninitialize(); return 0;
}
