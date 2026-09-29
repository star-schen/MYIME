#pragma once
#include "engine.h"
#include <vector>
struct CandidateTheme {
    std::wstring font=L"Microsoft YaHei UI";
    int font_size=18;
    int padding=10;
    COLORREF background=RGB(250,250,250), highlight=RGB(210,230,255);
};
class CandidateWindow {
public:
    using Action=void(*)(void*,int);
    ~CandidateWindow() { destroy(); }
    bool create(HINSTANCE module, void* owner, Action action);
    void destroy();
    bool created() const noexcept { return window_!=nullptr; }
    BOOL visible() const noexcept { return window_ && IsWindowVisible(window_); }
    void set_parent(HWND parent) {
        if (window_ && reinterpret_cast<HWND>(GetWindowLongPtrW(window_,GWLP_HWNDPARENT))!=parent)
            SetWindowLongPtrW(window_,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(parent));
    }
    void hide() {
        if (window_ && IsWindowVisible(window_)) {
            NotifyWinEvent(EVENT_OBJECT_IME_HIDE,window_,OBJID_CLIENT,CHILDID_SELF);
            ShowWindow(window_,SW_HIDE);
        }
    }
    void update(Engine& engine, const MyimeState& state, RECT caret);
    void refresh(Engine& engine,const MyimeState& state) { if (visible()) update(engine,state,last_caret_); }
private:
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    HWND window_=nullptr;
    HINSTANCE module_=nullptr;
    HFONT font_=nullptr;
    void* owner_=nullptr;
    Action action_=nullptr;
    CandidateTheme theme_;
    RECT last_caret_{};
    std::vector<std::wstring> rows_;
    std::wstring header_,footer_;
    size_t selected_=0;
    int row_height_=30, width_=360;
};
