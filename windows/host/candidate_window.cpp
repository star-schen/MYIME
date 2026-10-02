#include "candidate_window.h"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace {
long window_classes=0;
constexpr wchar_t kClass[]=L"MYIME.Candidates.v2";
constexpr UINT kRelayout=WM_APP+11;
COLORREF color(uint32_t rgb) { return RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); }
void fill(HDC dc,const RECT& r,COLORREF color) {
    HBRUSH brush=CreateSolidBrush(color); if (brush) { FillRect(dc,&r,brush); DeleteObject(brush); }
}
void text(HDC dc,const std::wstring& value,RECT r,COLORREF color) {
    SetTextColor(dc,color);
    DrawTextW(dc,value.data(),static_cast<int>(value.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
}
}
bool CandidateWindow::create(HINSTANCE module,void* owner,Action action,const CandidateTheme& theme) {
    destroy(); owner_=owner; action_=action; set_theme(theme);
    WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=module; wc.lpszClassName=kClass; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
    module_=module; InterlockedIncrement(&window_classes);
    // Inherit caller DPI awareness, keeping TSF caret and popup coordinates
    // consistent. Never change a hosting application's process/thread context.
    window_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,kClass,L"",WS_POPUP,0,0,1,1,nullptr,nullptr,module,this);
    if (!window_) { destroy(); return false; }
    dpi_=GetDpiForWindow(window_); if (!dpi_) dpi_=96;
    return true;
}
void CandidateWindow::hide() {
    if (shadow_) ShowWindow(shadow_,SW_HIDE);
    if (visible()) { NotifyWinEvent(EVENT_OBJECT_IME_HIDE,window_,OBJID_CLIENT,CHILDID_SELF); ShowWindow(window_,SW_HIDE); }
}
void CandidateWindow::set_parent(HWND parent) {
    for (HWND window:{window_,shadow_}) if (window && reinterpret_cast<HWND>(GetWindowLongPtrW(window,GWLP_HWNDPARENT))!=parent)
        SetWindowLongPtrW(window,GWLP_HWNDPARENT,reinterpret_cast<LONG_PTR>(parent));
}
void CandidateWindow::clear_buffer() noexcept {
    if (buffer_ && old_bitmap_) SelectObject(buffer_,old_bitmap_);
    if (bitmap_) DeleteObject(bitmap_);
    if (buffer_) DeleteDC(buffer_);
    buffer_=nullptr; bitmap_=nullptr; old_bitmap_=nullptr; buffer_width_=buffer_height_=0;
}
void CandidateWindow::destroy() {
    hide();
    if (shadow_) { DestroyWindow(shadow_); shadow_=nullptr; }
    if (window_) { DestroyWindow(window_); window_=nullptr; }
    if (font_) { DeleteObject(font_); font_=nullptr; }
    clear_buffer(); cells_.clear(); snapshot_={}; scroll_=0; shadow_width_=shadow_height_=0;
    work_width_=work_height_=0; style_dirty_=true;
    if (module_) { if (InterlockedDecrement(&window_classes)==0) UnregisterClassW(kClass,module_); module_=nullptr; }
}
void CandidateWindow::set_theme(const CandidateTheme& theme) {
    theme_=theme; style_dirty_=true; scroll_=0;
    if (font_) { DeleteObject(font_); font_=nullptr; }
    if (shadow_) { ShowWindow(shadow_,SW_HIDE); shadow_width_=shadow_height_=0; }
}
void CandidateWindow::layout(int max_width,int max_height) {
    const auto& s=theme_.style;
    if (!font_) font_=CreateFontW(-px(s.font_size),0,0,0,static_cast<int>(s.font_weight),FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,theme_.font_family.c_str());
    if (!font_) throw std::runtime_error("Candidate font creation failed");
    HDC dc=GetDC(window_); if (!dc) throw std::runtime_error("Candidate DC unavailable");
    auto old=SelectObject(dc,font_); TEXTMETRICW metric{}; GetTextMetricsW(dc,&metric);
    text_height_=std::max(1,static_cast<int>(metric.tmHeight));
    auto measure=[&](const std::wstring& v) { SIZE z{}; GetTextExtentPoint32W(dc,v.data(),static_cast<int>(v.size()),&z); return static_cast<int>(z.cx); };
    const int edge=px(s.padding_x)+std::max(high_contrast_?1:0,px(s.border_width));
    const int top=px(s.padding_y)+std::max(high_contrast_?1:0,px(s.border_width));
    const int gap=px(s.spacing),cx=px(s.candidate_padding_x),cy=px(s.candidate_padding_y);
    width_=std::max(1,std::min(max_width,px(s.max_width))); const int available=std::max(1,width_-2*edge);
    std::vector<int> widths; widths.reserve(snapshot_.rows.size());
    int widest=s.preedit?measure(snapshot_.preedit):0;
    for (const auto& row:snapshot_.rows) {
        int w=measure(row.text)+2*cx;
        if (s.labels && !row.label.empty()) w+=measure(row.label)+cx;
        if (s.comments && !row.comment.empty()) w+=measure(row.comment)+cx;
        widths.push_back(std::clamp(w,1,available)); widest=std::max(widest,w);
    }
    if (!s.horizontal) width_=std::min(width_,std::max(px(s.min_width),widest+2*edge));
    else {
        long long total=2*edge;
        for (int w:widths) total+=w+gap;
        width_=std::min(width_,std::max(px(s.min_width),static_cast<int>(std::min<long long>(total,width_))));
    }
    const int rh=text_height_+2*cy; cells_.clear(); cells_.reserve(widths.size()); int x=edge,y=0;
    // Wrapping changes geometry only; Rime page and local indexes stay intact.
    for (int w:widths) {
        if (s.horizontal && x>edge && x+w>width_-edge) { x=edge; y+=rh+gap; }
        cells_.push_back({x,y,s.horizontal?x+w:width_-edge,y+rh});
        if (s.horizontal) x+=w+gap; else y+=rh+gap;
    }
    content_height_=cells_.empty()?0:cells_.back().bottom;
    const int hh=s.preedit && !snapshot_.preedit.empty()?text_height_+cy+gap:0;
    const int fh=s.page_controls && !snapshot_.rows.empty()?text_height_+cy+gap:0;
    height_=std::max(1,std::min(max_height,2*top+hh+content_height_+fh));
    header_={edge,top,width_-edge,top+hh};
    viewport_={edge,top+hh,width_-edge,std::max(top+hh,height_-top-fh)};
    footer_={edge,height_-top-fh,width_-edge,height_-top}; previous_button_=next_button_={};
    if (fh) {
        const int button=std::min(text_height_+2*cx,(width_-2*edge)/3);
        previous_button_={edge,footer_.top,edge+button,footer_.bottom}; next_button_={width_-edge-button,footer_.top,width_-edge,footer_.bottom};
    }
    const int vh=std::max(0,static_cast<int>(viewport_.bottom-viewport_.top)); scroll_=std::clamp(scroll_,0,std::max(0,content_height_-vh));
    if (snapshot_.selected<cells_.size()) {
        const auto& selected=cells_[snapshot_.selected];
        if (selected.bottom>scroll_+vh) scroll_=selected.bottom-vh;
        if (selected.top<scroll_) scroll_=selected.top;
    }
    SelectObject(dc,old); ReleaseDC(window_,dc);
}
RECT CandidateWindow::candidate_bounds(size_t index) const noexcept {
    if (index>=cells_.size()) return {};
    auto r=cells_[index]; r.top+=viewport_.top-scroll_; r.bottom+=viewport_.top-scroll_; return r;
}
int CandidateWindow::hit_test(POINT p) const noexcept {
    if (PtInRect(&previous_button_,p) && snapshot_.page>0) return -1;
    if (PtInRect(&next_button_,p) && !snapshot_.last_page) return -2;
    if (PtInRect(&viewport_,p)) for (size_t i=0;i<cells_.size();++i) { auto r=candidate_bounds(i); if (PtInRect(&r,p)) return static_cast<int>(i); }
    return -4;
}
void CandidateWindow::present(CandidatePresentation view,RECT caret) {
    if (!window_) return;
    if (!view.active) { snapshot_=std::move(view); hide(); return; }
    const bool changed=view!=snapshot_; if (view.page!=snapshot_.page) scroll_=0;
    snapshot_=std::move(view); last_caret_=caret;
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool high=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) && (contrast.dwFlags&HCF_HIGHCONTRASTON);
    if (high_contrast_!=high) { high_contrast_=high; style_dirty_=true; }
    const auto dpi=GetDpiForWindow(window_);
    if (dpi && dpi_!=dpi) { dpi_=dpi; if (font_) { DeleteObject(font_); font_=nullptr; } style_dirty_=true; }
    MONITORINFO m{sizeof(m)}; if (!GetMonitorInfoW(MonitorFromRect(&caret,MONITOR_DEFAULTTONEAREST),&m)) return;
    const bool redraw_shadow=style_dirty_; laying_out_=true;
    const int work_width=m.rcWork.right-m.rcWork.left,work_height=m.rcWork.bottom-m.rcWork.top;
    try {
        if (changed || style_dirty_ || work_width!=work_width_ || work_height!=work_height_)
            layout(work_width,work_height);
        work_width_=work_width; work_height_=work_height;
    } catch (...) { laying_out_=false; throw; }
    const int x=std::clamp(static_cast<int>(caret.left),static_cast<int>(m.rcWork.left),static_cast<int>(m.rcWork.right)-width_);
    int y=caret.bottom+px(theme_.style.caret_gap);
    if (y+height_>m.rcWork.bottom) y=static_cast<int>(caret.top)-height_-px(theme_.style.caret_gap);
    y=std::clamp(y,static_cast<int>(m.rcWork.top),static_cast<int>(m.rcWork.bottom)-height_);
    RECT before{}; GetWindowRect(window_,&before); const bool shown=visible()!=FALSE;
    const bool moved=before.left!=x || before.top!=y || before.right-before.left!=width_ || before.bottom-before.top!=height_;
    if (style_dirty_ || before.right-before.left!=width_ || before.bottom-before.top!=height_) {
        const int r=std::min({px(theme_.style.border_radius),width_/2,height_/2});
        HRGN region=r?CreateRoundRectRgn(0,0,width_+1,height_+1,2*r,2*r):nullptr;
        if (!SetWindowRgn(window_,region,FALSE) && region) DeleteObject(region);
    }
    if (!shown || moved || redraw_shadow) update_shadow(x,y,redraw_shadow);
    if (!shown || moved) {
        SetWindowPos(window_,HWND_TOPMOST,x,y,width_,height_,SWP_NOACTIVATE|(shown?0:SWP_SHOWWINDOW));
        NotifyWinEvent(shown?EVENT_OBJECT_IME_CHANGE:EVENT_OBJECT_IME_SHOW,window_,OBJID_CLIENT,CHILDID_SELF);
    }
    if (!shown || moved || changed || style_dirty_) InvalidateRect(window_,nullptr,FALSE);
    style_dirty_=false; laying_out_=false;
}
void CandidateWindow::update_shadow(int x,int y,bool redraw) {
    const auto& s=theme_.style; const int margin=px(s.shadow_size);
    if (!s.shadow || !margin || !s.shadow_opacity || high_contrast_) { if (shadow_) ShowWindow(shadow_,SW_HIDE); return; }
    if (!shadow_) shadow_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
        L"STATIC",L"",WS_POPUP,0,0,1,1,parent(),nullptr,module_,nullptr);
    if (!shadow_) return;
    const int w=width_+2*margin,h=height_+2*margin;
    if (redraw || w!=shadow_width_ || h!=shadow_height_) {
        if (static_cast<unsigned long long>(w)*h>16*1024*1024) { ShowWindow(shadow_,SW_HIDE); return; }
        BITMAPINFO info{}; info.bmiHeader={sizeof(BITMAPINFOHEADER),w,-h,1,32,BI_RGB};
        void* bits=nullptr; HDC dc=CreateCompatibleDC(nullptr);
        HBITMAP bitmap=dc?CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
        if (!bitmap) { if (dc) DeleteDC(dc); ShowWindow(shadow_,SW_HIDE); return; }
        auto old=SelectObject(dc,bitmap); const double radius=std::min({px(s.border_radius),width_/2,height_/2});
        auto pixels=static_cast<uint32_t*>(bits);
        for (int j=0;j<h;++j) for (int i=0;i<w;++i) {
            const double qx=std::abs(i-margin-width_/2.0)-(width_/2.0-radius),qy=std::abs(j-margin-height_/2.0)-(height_/2.0-radius);
            const double d=std::hypot(std::max(qx,0.0),std::max(qy,0.0))+std::min(std::max(qx,qy),0.0)-radius;
            const double fade=std::clamp(1.0-std::max(d,0.0)/margin,0.0,1.0);
            pixels[static_cast<size_t>(j)*w+i]=static_cast<uint32_t>(s.shadow_opacity*fade*fade)<<24; // premultiplied black
        }
        POINT destination{x-margin,y-margin+px(2)},source{}; SIZE size{w,h}; BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
        const bool success=UpdateLayeredWindow(shadow_,nullptr,&destination,&size,dc,&source,0,&blend,ULW_ALPHA)!=FALSE;
        SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
        if (!success) { ShowWindow(shadow_,SW_HIDE); return; } shadow_width_=w; shadow_height_=h;
    }
    SetWindowPos(shadow_,window_,x-margin,y-margin+px(2),w,h,SWP_NOACTIVATE|SWP_SHOWWINDOW);
}
void CandidateWindow::paint(HDC screen) {
    const auto& s=theme_.style; if (!font_) return;
    if (buffer_width_!=width_ || buffer_height_!=height_ || !buffer_) {
        clear_buffer(); buffer_=CreateCompatibleDC(screen); bitmap_=buffer_?CreateCompatibleBitmap(screen,width_,height_):nullptr;
        if (bitmap_) { old_bitmap_=SelectObject(buffer_,bitmap_); buffer_width_=width_; buffer_height_=height_; }
    }
    HDC dc=bitmap_?buffer_:screen;
    const auto bg=high_contrast_?GetSysColor(COLOR_WINDOW):color(s.background);
    const auto fg=high_contrast_?GetSysColor(COLOR_WINDOWTEXT):color(s.text);
    const auto muted=high_contrast_?fg:color(s.muted);
    const auto highlight=high_contrast_?GetSysColor(COLOR_HIGHLIGHT):color(s.highlight);
    const auto selected_text=high_contrast_?GetSysColor(COLOR_HIGHLIGHTTEXT):color(s.highlight_text);
    RECT bounds{0,0,width_,height_}; fill(dc,bounds,bg);
    auto old=SelectObject(dc,font_); SetBkMode(dc,TRANSPARENT);
    if (header_.bottom>header_.top) {
        text(dc,snapshot_.preedit,header_,fg); SIZE prefix{}; const auto length=std::min(snapshot_.caret,snapshot_.preedit.size());
        GetTextExtentPoint32W(dc,snapshot_.preedit.data(),static_cast<int>(length),&prefix);
        const LONG x=std::min(header_.right-1,header_.left+prefix.cx+1);
        RECT caret{x,header_.top+px(2),x+1,header_.top+text_height_-px(2)}; fill(dc,caret,muted);
    }
    const int saved=SaveDC(dc); IntersectClipRect(dc,viewport_.left,viewport_.top,viewport_.right,viewport_.bottom);
    for (size_t i=0;i<snapshot_.rows.size();++i) {
        auto r=candidate_bounds(i); if (r.bottom<=viewport_.top || r.top>=viewport_.bottom) continue;
        const bool selected=i==snapshot_.selected; if (selected) fill(dc,r,highlight);
        r.left+=px(s.candidate_padding_x); r.right-=px(s.candidate_padding_x); const auto& row=snapshot_.rows[i];
        auto part=[&](const std::wstring& v,COLORREF tint) {
            text(dc,v,r,tint); SIZE size{}; GetTextExtentPoint32W(dc,v.data(),static_cast<int>(v.size()),&size);
            r.left=std::min(r.right,r.left+size.cx+px(s.candidate_padding_x));
        };
        if (s.labels && !row.label.empty()) part(row.label,selected?selected_text:(high_contrast_?fg:color(s.label)));
        part(row.text,selected?selected_text:fg);
        if (s.comments && !row.comment.empty()) part(row.comment,selected?selected_text:muted);
    }
    if (saved) RestoreDC(dc,saved);
    if (footer_.bottom>footer_.top) {
        text(dc,L"◀",previous_button_,snapshot_.page?fg:muted); text(dc,L"▶",next_button_,snapshot_.last_page?muted:fg);
        RECT page=footer_; page.left=previous_button_.right+px(s.spacing); page.right=next_button_.left;
        SetTextColor(dc,muted); const auto label=std::to_wstring(snapshot_.page+1);
        DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&page,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
    }
    const int border=std::max(high_contrast_?1:0,px(s.border_width));
    if (border) {
        HPEN pen=CreatePen(PS_SOLID,border,high_contrast_?fg:color(s.border));
        auto old_pen=SelectObject(dc,pen),old_brush=SelectObject(dc,GetStockObject(NULL_BRUSH)); const int radius=px(s.border_radius);
        RoundRect(dc,border/2,border/2,width_-(border+1)/2,height_-(border+1)/2,2*radius,2*radius);
        SelectObject(dc,old_pen); SelectObject(dc,old_brush); DeleteObject(pen);
    }
    SelectObject(dc,old); if (bitmap_) BitBlt(screen,0,0,width_,height_,buffer_,0,0,SRCCOPY);
}
LRESULT CALLBACK CandidateWindow::procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto self=reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { self=static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(hwnd,msg,w,l);
    if (msg==WM_NCDESTROY) { self->window_=nullptr; SetWindowLongPtrW(hwnd,GWLP_USERDATA,0); return DefWindowProcW(hwnd,msg,w,l); }
    if (msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg==WM_ERASEBKGND) return 1;
    if (msg==WM_LBUTTONUP) {
        if (!self->visible() || !self->snapshot_.active) return 0;
        const int action=self->hit_test({GET_X_LPARAM(l),GET_Y_LPARAM(l)});
        if (action!=-4 && self->action_) self->action_(self->owner_,action); return 0;
    }
    if (msg==WM_MOUSEWHEEL) {
        const int limit=std::max(0,self->content_height_-static_cast<int>(self->viewport_.bottom-self->viewport_.top));
        self->scroll_=std::clamp(self->scroll_-GET_WHEEL_DELTA_WPARAM(w)*self->text_height_/WHEEL_DELTA*3,0,limit);
        InvalidateRect(hwnd,nullptr,FALSE); return 0;
    }
    if (msg==WM_DPICHANGED) {
        self->dpi_=HIWORD(w); self->style_dirty_=true;
        if (self->font_) { DeleteObject(self->font_); self->font_=nullptr; } PostMessageW(hwnd,kRelayout,0,0); return 0;
    }
    if (msg==WM_SETTINGCHANGE || msg==WM_SYSCOLORCHANGE || msg==WM_THEMECHANGED) { self->style_dirty_=true; PostMessageW(hwnd,kRelayout,0,0); }
    if (msg==kRelayout && self->visible() && !self->laying_out_) {
        self->style_dirty_=true;
        try { self->present(self->snapshot_,self->last_caret_); } catch (...) { self->hide(); } return 0;
    }
    if (msg==WM_PRINTCLIENT) { self->paint(reinterpret_cast<HDC>(w)); return 0; }
    if (msg==WM_PAINT) { PAINTSTRUCT ps{}; auto dc=BeginPaint(hwnd,&ps); self->paint(dc); EndPaint(hwnd,&ps); return 0; }
    return DefWindowProcW(hwnd,msg,w,l);
}
