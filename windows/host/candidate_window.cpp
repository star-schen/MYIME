#include "candidate_window.h"
#include <windowsx.h>
#include <algorithm>
namespace { long window_classes=0; }
bool CandidateWindow::create(HINSTANCE module, void* owner, Action action) {
    destroy();
    owner_=owner; action_=action;
    WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=module; wc.lpszClassName=L"MYIME.Candidates.v1"; wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    if (!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;
    module_=module; InterlockedIncrement(&window_classes);
    window_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,wc.lpszClassName,L"",WS_POPUP|WS_BORDER,0,0,1,1,nullptr,nullptr,module,this);
    font_=CreateFontW(-theme_.font_size,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,theme_.font.c_str());
    if (!window_ || !font_) { destroy(); return false; }
    return true;
}
void CandidateWindow::destroy() {
    hide();
    if (window_) { DestroyWindow(window_); window_=nullptr; }
    if (font_) { DeleteObject(font_); font_=nullptr; }
    if (module_) { if (InterlockedDecrement(&window_classes)==0) UnregisterClassW(L"MYIME.Candidates.v1",module_); module_=nullptr; }
}
void CandidateWindow::update(Engine& engine, const MyimeState& state, RECT caret) {
    if (!state.active) { hide(); return; }
    last_caret_=caret;
    std::vector<std::wstring> rows;
    auto header=wide(state.preedit);
    for (size_t i=0;i<state.count;++i) {
        MyimeCandidate c{};
        if (engine.candidate(engine.handle,i,&c)) throw std::runtime_error(engine.last_error());
        rows.push_back(wide(c.label)+L"  "+wide(c.text)+L"  "+wide(c.comment));
    }
    auto footer=L"◀  PgUp      "+std::to_wstring(state.page+1)+L"      PgDn  ▶";
    const bool changed=rows!=rows_ || header!=header_ || footer!=footer_ || selected_!=state.selected;
    rows_=std::move(rows); header_=std::move(header); footer_=std::move(footer); selected_=state.selected;
    HDC dc=GetDC(window_); auto old=SelectObject(dc,font_);
    width_=300;
    for (const auto& row: rows_) { SIZE s{}; GetTextExtentPoint32W(dc,row.data(),static_cast<int>(row.size()),&s); width_=std::max(width_,static_cast<int>(s.cx)+2*theme_.padding); }
    SelectObject(dc,old); ReleaseDC(window_,dc);
    const int height=static_cast<int>(rows_.size()+2)*row_height_;
    MONITORINFO info{sizeof(info)}; GetMonitorInfoW(MonitorFromRect(&caret,MONITOR_DEFAULTTONEAREST),&info);
    width_=std::min(width_,static_cast<int>(info.rcWork.right-info.rcWork.left));
    const int x=std::clamp(static_cast<int>(caret.left),static_cast<int>(info.rcWork.left),static_cast<int>(info.rcWork.right)-width_);
    int y=caret.bottom;
    if (y+height>info.rcWork.bottom) y=std::max(static_cast<int>(info.rcWork.top),static_cast<int>(caret.top)-height);
    const bool visible=IsWindowVisible(window_)!=FALSE;
    RECT previous{}; GetWindowRect(window_,&previous);
    const bool moved=previous.left!=x || previous.top!=y || previous.right-previous.left!=width_ || previous.bottom-previous.top!=height;
    if (!visible || moved) {
        SetWindowPos(window_,HWND_TOPMOST,x,y,width_,height,SWP_NOACTIVATE|(visible?0:SWP_SHOWWINDOW));
        NotifyWinEvent(visible?EVENT_OBJECT_IME_CHANGE:EVENT_OBJECT_IME_SHOW,window_,OBJID_CLIENT,CHILDID_SELF);
    }
    if (!visible || moved || changed) InvalidateRect(window_,nullptr,FALSE);
}
LRESULT CALLBACK CandidateWindow::procedure(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto self=reinterpret_cast<CandidateWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if (msg==WM_NCCREATE) { self=static_cast<CandidateWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    if (!self) return DefWindowProcW(hwnd,msg,w,l);
    if (msg==WM_NCDESTROY) {
        self->window_=nullptr;
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,0);
        return DefWindowProcW(hwnd,msg,w,l);
    }
    if (msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if (msg==WM_ERASEBKGND) return 1;
    if (msg==WM_LBUTTONUP) {
        int row=GET_Y_LPARAM(l)/self->row_height_-1;
        if (row>=0 && row<static_cast<int>(self->rows_.size())) self->action_(self->owner_,row);
        else if (row==static_cast<int>(self->rows_.size())) self->action_(self->owner_,GET_X_LPARAM(l)<self->width_/2 ? -1 : -2);
        return 0;
    }
    if (msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC screen=BeginPaint(hwnd,&ps); RECT bounds; GetClientRect(hwnd,&bounds);
        HDC buffer=CreateCompatibleDC(screen);
        HBITMAP bitmap=buffer?CreateCompatibleBitmap(screen,bounds.right,bounds.bottom):nullptr;
        HGDIOBJ previous=bitmap?SelectObject(buffer,bitmap):nullptr;
        HDC dc=bitmap?buffer:screen;
        HBRUSH bg=CreateSolidBrush(self->theme_.background); FillRect(dc,&bounds,bg); DeleteObject(bg);
        auto old=SelectObject(dc,self->font_); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(25,25,25));
        auto draw=[&](const std::wstring& text,int row,bool selected) {
            RECT r{0,row*self->row_height_,self->width_,(row+1)*self->row_height_};
            if (selected) { HBRUSH b=CreateSolidBrush(self->theme_.highlight); FillRect(dc,&r,b); DeleteObject(b); }
            r.left+=self->theme_.padding; r.right-=self->theme_.padding;
            DrawTextW(dc,text.data(),static_cast<int>(text.size()),&r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
        };
        draw(self->header_,0,false);
        for (size_t i=0;i<self->rows_.size();++i) draw(self->rows_[i],static_cast<int>(i+1),i==self->selected_);
        draw(self->footer_,static_cast<int>(self->rows_.size()+1),false);
        SelectObject(dc,old);
        if (bitmap) {
            BitBlt(screen,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
            SelectObject(buffer,previous); DeleteObject(bitmap);
        }
        if (buffer) DeleteDC(buffer);
        EndPaint(hwnd,&ps); return 0;
    }
    return DefWindowProcW(hwnd,msg,w,l);
}
