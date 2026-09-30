#include <windows.h>
#include <msctf.h>
#include <cstdio>
#include <myime/core.h>
#include <string>
#include <vector>
#include <thread>
#include <filesystem>
#include <fstream>
// This probe is run only by the user. Temporary synthetic product config,
// never an installed config or real user dictionary.
class ProductConfigFixture {
    std::filesystem::path path_;
public:
    ~ProductConfigFixture() { if (!path_.empty()) DeleteFileW(path_.c_str()); }
    bool create() {
        wchar_t directory[MAX_PATH]{},file[MAX_PATH]{};
        const auto length=GetTempPathW(MAX_PATH,directory);
        if (!length || length>=MAX_PATH || !GetTempFileNameW(directory,L"myi",0,file)) return false;
        path_=file;
        std::ofstream output(path_,std::ios::binary|std::ios::trunc);
        output << "[default]\nschema='pinyin_simp'\n"
               << "[[profiles]]\nexecutable='Unconfigured.exe'\n[profiles.overrides]\nschema='myime_nonexistent_probe_schema'\n"
               << "[[profiles]]\nexecutable='InvalidOption.exe'\n[profiles.overrides.options]\n\"invalid\\u0000option\"=true\n"
               << "[[profiles]]\nexecutable='Disabled.exe'\n[profiles.overrides]\nenabled=false\n";
        output.close();
        return !output.fail();
    }
    std::string utf8_path() const {
        const auto value=path_.wstring();
        const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);
        std::string result(size,'\0');
        if (size) WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);
        return result;
    }
};
static bool ok(int result) {
    if (result == 0) return true;
    std::fprintf(stderr, "%s\n", myime_last_error()); return false;
}
int wmain(int argc, wchar_t** arguments) {
    std::vector<std::string> argv;
    for (int i=0;i<argc;++i) {
        int size=WideCharToMultiByte(CP_UTF8,0,arguments[i],-1,nullptr,0,nullptr,nullptr);
        std::string value(size,'\0'); WideCharToMultiByte(CP_UTF8,0,arguments[i],-1,value.data(),size,nullptr,nullptr);
        value.pop_back(); argv.push_back(value);
    }
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;
    const auto version = myime_abi_version();
    std::printf("MYIME C++ -> Rust ABI version: %u\n", version);
    if (argc == 4 && std::string(argv[1]) == "--deploy") {
        const bool deployed = ok(myime_deploy(argv[2].c_str(), argv[3].c_str()));
        CoUninitialize(); return deployed ? 0 : 3;
    }
    if (argc == 3) {
        MyimeCore* core = nullptr;
        if (!ok(myime_create(argv[1].c_str(), argv[2].c_str(), "pinyin_simp", &core))) return 4;
        bool passed = true;
        for (const char key : std::string("nihao")) {
            uint32_t eaten = 0;
            passed &= ok(myime_key(core, key, 0, &eaten)) && eaten;
        }
        MyimeState state{};
        passed &= ok(myime_state(core, &state)) && state.active && state.count > 0;
        std::printf("preedit=%.*s candidates=%zu\n", int(state.preedit.len), state.preedit.data, state.count);
        if (state.count) {
            MyimeCandidate candidate{};
            passed &= ok(myime_candidate(core, 0, &candidate));
            const std::string expected(candidate.text.data, candidate.text.len);
            passed &= ok(myime_select(core, 0));
            passed &= ok(myime_state(core, &state));
            passed &= std::string(state.commit.data, state.commit.len) == expected && !expected.empty();
            std::printf("commit=%.*s\n", int(state.commit.len), state.commit.data);
            uint32_t eaten=99;
            passed &= myime_key(core,'x',0,&eaten)==-1 && eaten==0;
            passed &= ok(myime_state(core,&state)) && std::string(state.commit.data,state.commit.len)==expected;
            passed &= ok(myime_ack_commit(core));
        }
        // Paging uses Rime's logical page/index, never a frontend fixed slot count.
        for (char key:std::string("ni")) { uint32_t eaten=0; passed &= ok(myime_key(core,key,0,&eaten)); }
        passed &= ok(myime_state(core,&state)) && !state.last_page;
        const size_t previous_page=state.page;
        uint32_t eaten=0; passed &= ok(myime_key(core,0xff56,0,&eaten));
        passed &= ok(myime_state(core,&state)) && state.page==previous_page+1;
        MyimeCandidate first{}; passed &= ok(myime_candidate(core,0,&first)) && first.index==state.page*state.page_size;
        passed &= myime_select(core,state.count)==-1;
        passed &= ok(myime_key(core,0xff1b,0,&eaten));
        passed &= ok(myime_state(core,&state)) && !state.active && state.commit.len==0;
        ProductConfigFixture config;
        if (!config.create()) passed=false;
        else {
            passed &= ok(myime_load_config(core,config.utf8_path().c_str()));
            uint32_t enabled=0;
            passed &= ok(myime_apply_profile(core,"Editor.exe",&enabled)) && enabled==1;
            passed &= ok(myime_key(core,'n',0,&eaten));
            passed &= myime_apply_profile(core,"Disabled.exe",&enabled)==-1;
            passed &= ok(myime_clear(core));
            // An unavailable schema must release only the provisional session.
            enabled=99;
            passed &= myime_apply_profile(core,"Unconfigured.exe",&enabled)==-1 && enabled==99;
            passed &= myime_apply_profile(core,"InvalidOption.exe",&enabled)==-1 && enabled==99;
            passed &= ok(myime_state(core,&state)) && std::string(state.schema.data,state.schema.len)=="pinyin_simp";
            passed &= ok(myime_key(core,'n',0,&eaten));
            // If the last valid effective config was lost, this unchanged profile
            // would try to replace the provider during composition and fail.
            passed &= ok(myime_apply_profile(core,"Editor.exe",&enabled)) && enabled==1;
            passed &= ok(myime_select(core,0));
            passed &= myime_apply_profile(core,"Disabled.exe",&enabled)==-1;
            passed &= ok(myime_clear(core)); // Cancel a pending commit without ack.
            passed &= ok(myime_state(core,&state)) && state.commit.len==0;
            passed &= ok(myime_apply_profile(core,"Disabled.exe",&enabled)) && enabled==0;
            passed &= ok(myime_apply_profile(core,"Editor.exe",&enabled)) && enabled==1;
        }
        bool wrong_thread_rejected=false;
        std::thread other([&] { MyimeState invalid{}; wrong_thread_rejected=myime_state(core,&invalid)==-1; }); other.join();
        passed &= wrong_thread_rejected;
        bool wrong_destroy_rejected=false;
        std::thread wrong_destroy([&] { wrong_destroy_rejected=myime_destroy(core)==-1; }); wrong_destroy.join();
        passed &= wrong_destroy_rejected;
        if (!wrong_destroy_rejected) { CoUninitialize(); return 6; } // Never reuse an unexpectedly freed handle.
        MyimeCore* failed=nullptr;
        passed &= myime_create(argv[1].c_str(),argv[2].c_str(),"myime_nonexistent_probe_schema",&failed)==-1 && !failed;
        passed &= ok(myime_state(core,&state));
        // Live providers prohibit offline deployment, without finalizing them.
        passed &= myime_deploy(argv[1].c_str(),argv[2].c_str())==-1;
        MyimeCore* second=nullptr;
        passed &= ok(myime_create(argv[1].c_str(),argv[2].c_str(),"pinyin_simp",&second));
        if (second) passed &= ok(myime_destroy(second));
        passed &= ok(myime_state(core,&state));
        passed &= ok(myime_destroy(core));
        passed &= myime_state(nullptr,&state)==-1;
        // Exercise finalization after the last provider, failed fresh creation,
        // and reinitialization in the same process.
        failed=nullptr;
        passed &= myime_create(argv[1].c_str(),argv[2].c_str(),"myime_nonexistent_probe_schema",&failed)==-1 && !failed;
        second=nullptr;
        passed &= ok(myime_create(argv[1].c_str(),argv[2].c_str(),"pinyin_simp",&second));
        if (second) passed &= ok(myime_destroy(second));
        std::puts(passed?"C ABI lifecycle, paging, commit acknowledgement and atomic profiles: PASS":"C ABI integration: FAIL");
        CoUninitialize(); return passed ? 0 : 5;
    }
    CoUninitialize();
    return version == 1 ? 0 : 2;
}
