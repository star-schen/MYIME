#include "candidate_window.h"
#include "theme_catalog.h"
#include <objidl.h>
#include <gdiplus.h>
#include <cstdio>
#include <fstream>
#include <thread>

namespace {
bool check(bool result,const char* name) { if (!result) std::fprintf(stderr,"FAIL: %s\n",name); return result; }
struct Fixture {
    std::filesystem::path root,parent;
    explicit Fixture(const std::filesystem::path& directory):parent(directory) {
        root=parent/(L"theme-probe-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("Cannot create fixture");
    }
    ~Fixture() {
        // Delete only this test-created directory inside the executable folder.
        std::error_code error;
        const auto resolved=std::filesystem::canonical(root,error);
        if (error) return;
        const auto resolved_parent=std::filesystem::canonical(parent,error);
        if (!error && resolved.parent_path()==resolved_parent && resolved.filename()==root.filename()
            && resolved.filename().wstring().starts_with(L"theme-probe-")) std::filesystem::remove_all(resolved,error);
    }
    void write(const char* contents) {
        std::filesystem::create_directories(root/L"themes"/L"dark");
        std::ofstream file(root/L"themes"/L"dark"/L"theme.toml",std::ios::binary|std::ios::trunc);
        file<<contents; if (!file) throw std::runtime_error("Cannot write fixture");
    }
};
bool capture(HWND window,const std::filesystem::path& path) {
    RECT r{}; GetClientRect(window,&r); HDC screen=GetDC(window),dc=CreateCompatibleDC(screen);
    HBITMAP bitmap=CreateCompatibleBitmap(screen,r.right,r.bottom); bool saved=false;
    if (dc && bitmap) {
        auto old=SelectObject(dc,bitmap); SendMessageW(window,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
        SelectObject(dc,old);
        Gdiplus::Bitmap image(bitmap,nullptr);
        const CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        saved=image.Save(path.c_str(),&png,nullptr)==Gdiplus::Ok;
    }
    if (bitmap) DeleteObject(bitmap); if (dc) DeleteDC(dc); if (screen) ReleaseDC(window,screen);
    return saved;
}
void clicked(void* owner,int index) { *static_cast<int*>(owner)=index; }
CandidatePresentation sample() {
    CandidatePresentation view; view.active=true; view.preedit=L"ni hao"; view.caret=6; view.last_page=false;
    view.rows={{L"1",L"你好",L"ni hao"},{L"2",L"您好",L"nin hao"},{L"3",L"拟好",L"ni hao"},
        {L"4",L"你号",L"ni hao"},{L"5",L"倪浩",L"ni hao"},{L"6",L"霓虹",L"ni hong"},{L"7",L"你好世界",L"示例注释"}};
    return view;
}
}
int main() {
    bool passed=true; ULONG_PTR token=0; Gdiplus::GdiplusStartupInput startup;
    if (Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) return 1;
    try {
        wchar_t path[32768]{}; GetModuleFileNameW(nullptr,path,_countof(path));
        const auto directory=std::filesystem::path(path).parent_path(); Fixture fixture(directory);
        ThemeCatalog catalog; catalog.open(directory,fixture.root);
        auto installed=catalog.resolve(L"dark");
        passed &= check(installed.id==L"dark" && installed.warning.empty(),"installed theme");
        fixture.write("format_version=1\nid='dark'\nname='Override'\nversion='1'\n[font]\nsize=24\n[colors]\nbackground='#123456'\n");
        auto custom=catalog.resolve(L"dark");
        passed &= check(custom.id==L"dark" && custom.style.font_size==24 && custom.style.background==0x123456,"user theme precedence");
        fixture.write("format_version=1\nid='different'\nname='Broken'\nversion='1'\n");
        passed &= check(catalog.resolve(L"dark").id==L"default" && !catalog.resolve(L"dark").warning.empty(),"invalid override fallback");
        std::filesystem::remove(fixture.root/L"themes"/L"dark"/L"theme.toml");
        passed &= check(catalog.resolve(L"dark").style.background==installed.style.background,"theme removal restores installed package");
        passed &= check(catalog.resolve(L"not-installed").id==L"default","missing package fallback");
        MyimeTheme* handle=nullptr; const auto user=utf8((fixture.root/L"themes").wstring()),shared=utf8((directory/L"themes").wstring());
        passed &= check(myime_theme_abi_version()==1 && myime_theme_resolve("dark",user.c_str(),shared.c_str(),&handle)==0,"presentation ABI");
        if (handle) {
            MyimeThemeView view{};
            passed &= check(myime_theme_view(handle,sizeof(view)-1,&view)==-1,"ABI output size guard");
            bool wrong=false;
            std::thread other([&] { MyimeThemeView output{}; wrong=myime_theme_view(handle,sizeof(output),&output)==-1 && myime_theme_destroy(handle)==-1; }); other.join();
            passed &= check(wrong && myime_theme_view(handle,sizeof(view),&view)==0,"thread affinity keeps handle alive");
            passed &= check(wide(view.id)==L"dark" && myime_theme_destroy(handle)==0,"borrowed view and destruction");
        }
        const auto artifacts=directory/L"test-artifacts"; std::filesystem::create_directories(artifacts);
        int action=-4; CandidateWindow window;
        passed &= check(window.create(GetModuleHandleW(nullptr),&action,clicked,catalog.resolve(L"default")),"candidate window creation");
        const auto foreground=GetForegroundWindow();
        for (const auto* id:{L"default",L"light",L"dark",L"ribbon"}) {
            window.set_theme(catalog.resolve(id)); window.present(sample(),{80,80,81,100}); UpdateWindow(window.hwnd());
            passed &= check(window.visible() && GetForegroundWindow()==foreground,"preview must not steal focus");
            for (size_t i=0;i<7;++i) {
                const auto r=window.candidate_bounds(i); POINT p{(r.left+r.right)/2,(r.top+r.bottom)/2};
                passed &= check(window.hit_test(p)==static_cast<int>(i),"layout preserves page-local hit index");
                SendMessageW(window.hwnd(),WM_LBUTTONUP,0,MAKELPARAM(p.x,p.y));
                passed &= check(action==static_cast<int>(i),"mouse candidate selection");
            }
            window.present(sample(),{80,80,81,100});
            passed &= check(!GetUpdateRect(window.hwnd(),nullptr,FALSE),"unchanged presentation is not repainted");
            passed &= check(capture(window.hwnd(),artifacts/(std::wstring(L"theme-")+id+L".png")),"PNG render capture");
            window.hide(); action=-4;
            SendMessageW(window.hwnd(),WM_LBUTTONUP,0,MAKELPARAM(30,70));
            passed &= check(!window.visible() && (!window.shadow_hwnd() || !IsWindowVisible(window.shadow_hwnd())) && action==-4,"hidden popup and shadow reject old clicks");
        }
        window.set_theme(catalog.resolve(L"default")); window.present(sample(),{80,80,81,100}); UpdateWindow(window.hwnd());
        const auto resources=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
        for (int i=0;i<40;++i) { window.set_theme(catalog.resolve(i%2?L"dark":L"default")); window.present(sample(),{80,80,81,100}); UpdateWindow(window.hwnd()); }
        passed &= check(GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=resources+2,"font/bitmap lifetime across theme changes");
        auto large=sample(); for (int i=7;i<100;++i) large.rows.push_back({std::to_wstring(i+1),L"候选",L""}); large.selected=99;
        window.present(large,{80,80,81,100});
        const auto selected=window.candidate_bounds(99);
        passed &= check(window.hit_test({(selected.left+selected.right)/2,(selected.top+selected.bottom)/2})==99,"large page scroll keeps engine index");
        window.present({},{}); passed &= check(!window.visible(),"inactive composition hides candidate");
        window.destroy();
    } catch (const std::exception& e) { std::fprintf(stderr,"Theme probe error: %s\n",e.what()); passed=false; }
    Gdiplus::GdiplusShutdown(token);
    std::puts(passed?"Theme packages, ABI, layout, mouse, repaint and resources: PASS":"Theme integration: FAIL");
    return passed?0:2;
}
