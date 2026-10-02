#pragma once
#include <windows.h>
#include "candidate_theme.h"
#include <vector>
#include <utility>
struct CandidateRow {
    std::wstring label,text,comment;
    bool operator==(const CandidateRow&) const = default;
};
// Owned presentation snapshot; never sort or repaginate engine data.
struct CandidatePresentation {
    std::wstring preedit;
    size_t caret=0,selected=0,page=0;
    bool active=false,last_page=true;
    std::vector<CandidateRow> rows;
    bool operator==(const CandidatePresentation&) const = default;
};
class CandidateWindow {
public:
    using Action=void(*)(void*,int);
    CandidateWindow()=default;
    CandidateWindow(const CandidateWindow&)=delete;
    CandidateWindow& operator=(const CandidateWindow&)=delete;
    ~CandidateWindow() { destroy(); }
    bool create(HINSTANCE,void* owner,Action,const CandidateTheme&);
    void destroy();
    void set_theme(const CandidateTheme&);
    bool created() const noexcept { return window_!=nullptr; }
    HWND hwnd() const noexcept { return window_; }
    HWND shadow_hwnd() const noexcept { return shadow_; }
    HWND parent() const noexcept { return window_?reinterpret_cast<HWND>(GetWindowLongPtrW(window_,GWLP_HWNDPARENT)):nullptr; }
    BOOL visible() const noexcept { return window_ && IsWindowVisible(window_); }
    void set_parent(HWND);
    void hide();
    void refresh(CandidatePresentation view) { if (visible()) present(std::move(view),last_caret_); }
    void present(CandidatePresentation,RECT caret);
    int hit_test(POINT) const noexcept; // local index; -1/-2 pages; -4 none.
    RECT candidate_bounds(size_t) const noexcept;
private:
    static LRESULT CALLBACK procedure(HWND,UINT,WPARAM,LPARAM);
    HWND window_=nullptr,shadow_=nullptr;
    HINSTANCE module_=nullptr;
    HFONT font_=nullptr;
    HDC buffer_=nullptr;
    HBITMAP bitmap_=nullptr;
    HGDIOBJ old_bitmap_=nullptr;
    int buffer_width_=0,buffer_height_=0;
    void* owner_=nullptr;
    Action action_=nullptr;
    CandidateTheme theme_;
    CandidatePresentation snapshot_;
    RECT last_caret_{},header_{},footer_{},previous_button_{},next_button_{},viewport_{};
    std::vector<RECT> cells_;
    UINT dpi_=96;
    int width_=1,height_=1,text_height_=24,content_height_=0,scroll_=0;
    int work_width_=0,work_height_=0;
    bool style_dirty_=true,high_contrast_=false,laying_out_=false;
    int shadow_width_=0,shadow_height_=0;
    int px(uint32_t dip) const noexcept { return MulDiv(static_cast<int>(dip),static_cast<int>(dpi_),96); }
    void layout(int,int);
    void paint(HDC);
    void update_shadow(int,int,bool);
    void clear_buffer() noexcept;
};
