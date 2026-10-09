#include <windows.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <vector>
#include <stdexcept>
#include "candidate_window.h"
#include "theme_catalog.h"
#include "data_paths.h"
namespace {
CandidatePresentation example() {
    CandidatePresentation view; view.active=true; view.preedit=L"ni hao"; view.caret=6; view.last_page=false;
    view.rows={{L"1",L"你好",L"ni hao"},{L"2",L"您好",L"nin hao"},{L"3",L"拟好",L"ni hao"},
        {L"4",L"你号",L"ni hao"},{L"5",L"倪浩",L"ni hao"},{L"6",L"霓虹",L"ni hong"},{L"7",L"你好世界",L"示例注释"}};
    return view;
}
void bitmap_file(HBITMAP bitmap,SIZE size,const std::filesystem::path& path) {
    const auto bytes=static_cast<size_t>(size.cx)*size.cy*4;
    if (size.cx<=0 || size.cy<=0 || bytes>64*1024*1024) throw std::runtime_error("Preview image too large");
    std::vector<BYTE> pixels(bytes); BITMAPINFO info{};
    info.bmiHeader={sizeof(BITMAPINFOHEADER),size.cx,-size.cy,1,32,BI_RGB,static_cast<DWORD>(bytes)};
    HDC dc=GetDC(nullptr);
    const int rows=dc?GetDIBits(dc,bitmap,0,size.cy,pixels.data(),&info,DIB_RGB_COLORS):0;
    if (dc) ReleaseDC(nullptr,dc);
    if (rows!=size.cy) throw std::runtime_error("Cannot copy preview pixels");
    BITMAPFILEHEADER header{}; header.bfType=0x4d42; header.bfOffBits=sizeof(header)+sizeof(BITMAPINFOHEADER);
    header.bfSize=header.bfOffBits+static_cast<DWORD>(bytes);
    std::ofstream output(path,std::ios::binary|std::ios::trunc);
    output.write(reinterpret_cast<const char*>(&header),sizeof(header));
    output.write(reinterpret_cast<const char*>(&info.bmiHeader),sizeof(info.bmiHeader));
    output.write(reinterpret_cast<const char*>(pixels.data()),static_cast<std::streamsize>(bytes));
    if (!output) throw std::runtime_error("Cannot save preview image");
}
std::wstring quote(const std::wstring& text) {
    std::wstring result=L"\""; unsigned slashes=0;
    for (wchar_t c:text) { if (c==L'\\') { ++slashes; continue; }
        result.append(c==L'\"'?slashes*2+1:slashes,L'\\'); result+=c; slashes=0; }
    result.append(slashes*2,L'\\'); return result+L"\"";
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int) {
    bool render=false,self_check=false; std::wstring id=L"default";
    std::filesystem::path root,draft,output;
    int width=900,height=640; UINT dpi=96;
    try {
        int count=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&count);
        if (!argv) return 2;
        struct Arguments { LPWSTR* value; ~Arguments() { LocalFree(value); } } arguments{argv};
        for (int i=1;i<count;++i) {
            const std::wstring key=argv[i];
            if (key==L"--render") render=true;
            else if (key==L"--self-check") self_check=true;
            else if (i+1<count && key==L"--data-root") root=argv[++i];
            else if (i+1<count && key==L"--theme") id=argv[++i];
            else if (i+1<count && key==L"--theme-file") draft=argv[++i];
            else if (i+1<count && key==L"--output") output=argv[++i];
            else if (i+1<count && key==L"--width") width=std::stoi(argv[++i]);
            else if (i+1<count && key==L"--height") height=std::stoi(argv[++i]);
            else if (i+1<count && key==L"--dpi") dpi=static_cast<UINT>(std::stoul(argv[++i]));
            else throw std::runtime_error("Unknown theme editor argument");
        }
        wchar_t filename[32768]{}; const auto n=GetModuleFileNameW(nullptr,filename,32768);
        if (!n || n>=32768) return 2;
        const auto directory=std::filesystem::path(filename).parent_path();
        if (root.empty()) root=DataPaths::resolve().root;
        if (!root.is_absolute() || (!draft.empty() && !draft.is_absolute()) || (!output.empty() && !output.is_absolute()))
            throw std::runtime_error("Paths must be absolute");
        if (!render && !self_check) {
            // The former preview entry point now opens the complete editor.
            const auto exe=directory/L"myime-settings.exe";
            std::wstring command=quote(exe.wstring())+L" --theme-editor --data-root "+quote(root.wstring())+L" --theme "+quote(id);
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
            if (!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,directory.c_str(),&startup,&process)) return 1;
            CloseHandle(process.hThread); CloseHandle(process.hProcess); return 0;
        }
        ThemeCatalog catalog; catalog.open(directory,root); CandidateTheme theme;
        if (draft.empty()) theme=catalog.resolve(id);
        else {
            std::ifstream file(draft,std::ios::binary); std::string text;
            char chunk[4096]; while (file.read(chunk,sizeof(chunk)) || file.gcount()) {
                text.append(chunk,static_cast<size_t>(file.gcount()));
                if (text.size()>64*1024) throw std::runtime_error("Theme exceeds 64 KiB");
            }
            if (!file.eof()) throw std::runtime_error("Cannot read theme draft");
            theme=catalog.parse(text);
        }
        CandidateWindow painter; SIZE size{};
        struct Bitmap { HBITMAP value; ~Bitmap() { if (value) DeleteObject(value); } } bitmap{painter.render_image(theme,example(),width,height,dpi,&size)};
        if (self_check) return bitmap.value && size.cx>0 && size.cy>0?0:3;
        if (output.empty()) throw std::runtime_error("Output image required");
        bitmap_file(bitmap.value,size,output); return 0;
    } catch (...) { return 2; }
}
