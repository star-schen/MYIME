#include "candidate_window.h"
#include "theme_catalog.h"
#include "data_paths.h"
#include <windowsx.h>

namespace {
constexpr int ThemeChoice=101,Reload=102,Orientation=103,FontSize=104;
struct Preview {
    HINSTANCE module=nullptr;
    HWND window=nullptr,choice=nullptr,orientation=nullptr,size=nullptr,status=nullptr;
    HFONT font=nullptr;
    ThemeCatalog catalog;
    CandidateWindow candidates;
    CandidateTheme theme;
    std::vector<std::wstring> ids;
    struct Control { HWND window; int x,y,w,h; };
    std::vector<Control> controls;
    CandidatePresentation example;
    int px(int dip) const { return MulDiv(dip,static_cast<int>(GetDpiForWindow(window)),96); }
    ~Preview() { candidates.destroy(); if (font) DeleteObject(font); }
    static void action(void* owner,int index) {
        auto& self=*static_cast<Preview*>(owner);
        if (index>=0) self.example.selected=static_cast<size_t>(index);
        else if (index==-1 && self.example.page) --self.example.page;
        else if (index==-2) ++self.example.page;
        self.present();
    }
    void present() {
        POINT anchor{px(26),px(200)}; ClientToScreen(window,&anchor);
        candidates.present(example,{anchor.x,anchor.y,anchor.x+1,anchor.y+px(24)});
    }
    void load(bool apply_controls=false) {
        const auto index=static_cast<size_t>(SendMessageW(choice,CB_GETCURSEL,0,0));
        if (index>=ids.size()) return;
        theme=catalog.resolve(ids[index]);
        if (apply_controls) {
            theme.style.horizontal=SendMessageW(orientation,CB_GETCURSEL,0,0)==1;
            const auto font_index=SendMessageW(size,CB_GETCURSEL,0,0);
            if (font_index>=0) theme.style.font_size=static_cast<uint32_t>(font_index+10);
        } else {
            SendMessageW(orientation,CB_SETCURSEL,theme.style.horizontal,0);
            SendMessageW(size,CB_SETCURSEL,theme.style.font_size-10,0);
        }
        candidates.set_theme(theme);
        const auto label=theme.warning.empty()?theme.description:(L"已回退到雾灰："+theme.warning);
        SetWindowTextW(status,label.c_str()); present();
    }
    HWND control(const wchar_t* type,const wchar_t* title,DWORD style,int id,int x,int y,int w,int h) {
        auto control=CreateWindowExW(0,type,title,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),module,nullptr);
        if (!control) throw std::runtime_error("Cannot create preview control");
        SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        controls.push_back({control,x,y,w,h}); return control;
    }
    void scale_controls() {
        auto next=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        if (!next) return;
        for (const auto& c:controls) {
            SendMessageW(c.window,WM_SETFONT,reinterpret_cast<WPARAM>(next),TRUE);
            SetWindowPos(c.window,nullptr,px(c.x),px(c.y),px(c.w),px(c.h),SWP_NOZORDER|SWP_NOACTIVATE);
        }
        if (font) DeleteObject(font); font=next;
    }
    void initialize() {
        wchar_t path[32768]{}; if (!GetModuleFileNameW(module,path,_countof(path))) throw std::runtime_error("Preview path unavailable");
        catalog.open(std::filesystem::path(path).parent_path(),DataPaths::resolve().root);
        font=CreateFontW(-px(16),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        control(L"STATIC",L"MYIME 候选主题预览",0,0,24,20,650,30);
        control(L"STATIC",L"仅预览示例候选，不改变当前输入法配置。",0,0,24,55,650,25);
        control(L"STATIC",L"主题",0,0,24,96,55,24);
        choice=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,ThemeChoice,80,92,220,240);
        control(L"BUTTON",L"重新加载",BS_PUSHBUTTON|WS_TABSTOP,Reload,320,92,110,30);
        ids=catalog.ids();
        for (const auto& id:ids) {
            const auto available_theme=catalog.resolve(id);
            const auto label=available_theme.name+L"  ("+id+L")";
            SendMessageW(choice,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
        }
        SendMessageW(choice,CB_SETCURSEL,0,0);
        control(L"STATIC",L"布局",0,0,24,142,55,24);
        orientation=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,Orientation,80,138,120,160);
        SendMessageW(orientation,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"竖排"));
        SendMessageW(orientation,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"横排"));
        control(L"STATIC",L"字号",0,0,225,142,55,24);
        size=control(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,FontSize,280,138,90,240);
        for (int value=10;value<=48;++value) { const auto label=std::to_wstring(value); SendMessageW(size,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())); }
        control(L"STATIC",L"预览区（布局和字号调整只影响此窗口）",0,0,24,190,640,25);
        status=control(L"STATIC",L"",0,0,24,620,680,60);
        example.active=true; example.preedit=L"ni hao"; example.caret=6; example.last_page=false;
        example.rows={{L"1",L"你好",L"ni hao"},{L"2",L"您好",L"nin hao"},{L"3",L"拟好",L"ni hao"},
            {L"4",L"你号",L"ni hao"},{L"5",L"倪浩",L"ni hao"},{L"6",L"霓虹",L"ni hong"},{L"7",L"你好世界",L"示例注释"}};
        theme=catalog.resolve(ids.front());
        if (!candidates.create(module,this,action,theme)) throw std::runtime_error("Cannot create preview popup");
        candidates.set_parent(window); load();
    }
};
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto self=reinterpret_cast<Preview*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { self=static_cast<Preview*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); self->window=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(hwnd,msg,w,l);
    try {
        if (msg==WM_COMMAND) {
            if (LOWORD(w)==Reload || (LOWORD(w)==ThemeChoice && HIWORD(w)==CBN_SELCHANGE)) self->load();
            if ((LOWORD(w)==Orientation || LOWORD(w)==FontSize) && HIWORD(w)==CBN_SELCHANGE) self->load(true);
        }
        if (msg==WM_MOVE && self->candidates.created()) self->present();
        if (msg==WM_SIZE && self->candidates.created()) {
            if (w==SIZE_MINIMIZED) self->candidates.hide(); else self->present();
        }
        if (msg==WM_DPICHANGED) {
            auto r=reinterpret_cast<RECT*>(l); SetWindowPos(hwnd,nullptr,r->left,r->top,r->right-r->left,r->bottom-r->top,SWP_NOACTIVATE|SWP_NOZORDER);
            self->scale_controls(); if (self->candidates.created()) self->present();
        }
    } catch (const std::exception&) { self->candidates.hide(); SetWindowTextW(self->status,L"无法加载预览。请检查主题包及运行库。"); }
    if (msg==WM_DESTROY) { self->candidates.destroy(); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE module,HINSTANCE,PWSTR command_line,int show) {
    const bool self_check=std::wstring(command_line?command_line:L"")==L"--self-check";
    Preview preview; preview.module=module;
    WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=module; wc.lpszClassName=L"MYIME.ThemePreview.v1";
    wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
    RegisterClassW(&wc);
    try {
        const auto dpi=GetDpiForSystem();
        preview.window=CreateWindowExW(0,wc.lpszClassName,L"MYIME 主题预览",WS_OVERLAPPEDWINDOW,160,100,
            MulDiv(780,static_cast<int>(dpi),96),MulDiv(780,static_cast<int>(dpi),96),nullptr,nullptr,module,&preview);
        if (!preview.window) return 1;
        preview.initialize();
        if (self_check) {
            bool passed=SendMessageW(preview.choice,CB_GETCOUNT,0,0)>=4;
            for (size_t i=0;i<preview.ids.size();++i) {
                SendMessageW(preview.choice,CB_SETCURSEL,i,0);
                SendMessageW(preview.window,WM_COMMAND,MAKEWPARAM(ThemeChoice,CBN_SELCHANGE),reinterpret_cast<LPARAM>(preview.choice));
                passed &= !preview.theme.id.empty();
            }
            SendMessageW(preview.orientation,CB_SETCURSEL,1,0);
            SendMessageW(preview.size,CB_SETCURSEL,14,0); // 24 DIPs
            SendMessageW(preview.window,WM_COMMAND,MAKEWPARAM(FontSize,CBN_SELCHANGE),reinterpret_cast<LPARAM>(preview.size));
            passed &= preview.theme.style.horizontal==1 && preview.theme.style.font_size==24;
            SendMessageW(preview.window,WM_COMMAND,MAKEWPARAM(Reload,BN_CLICKED),0);
            passed &= preview.example.rows.size()==7 && preview.example.preedit==L"ni hao";
            DestroyWindow(preview.window); return passed?0:3;
        }
        ShowWindow(preview.window,show); preview.present();
        MSG message{}; while (GetMessageW(&message,nullptr,0,0)>0) {
            if (!IsDialogMessageW(preview.window,&message)) { TranslateMessage(&message); DispatchMessageW(&message); }
        }
        return 0;
    } catch (const std::exception&) {
        if (preview.window) DestroyWindow(preview.window);
        if (!self_check) MessageBoxW(nullptr,L"无法打开主题预览。请将程序与 MYIME 的运行库和主题目录放在一起。",L"MYIME",MB_OK|MB_ICONERROR);
        return 2;
    }
}
