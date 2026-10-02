#include "encoding.h"
#include "workspace_paths.h"
#include "user_data.h"
#include <cstdio>
int wmain(int count,wchar_t** args) {
    if (count<6 || (count-4)%2!=0) return 2;
    try {
        const auto root=std::filesystem::path(args[1]),installed=std::filesystem::path(args[2]);
        if (!root.filename().wstring().starts_with(L"myime-maintenance-test-") ||
            root.parent_path().filename()!=L"test-artifacts" || !std::filesystem::is_regular_file(root/L".myime-test-fixture"))
            throw std::runtime_error("Workspace probe requires a test-created fixture");
        const auto shared=WorkspacePaths::shared(installed,root);
        const auto schema=utf8(args[3]);
        MyimeCore* core=nullptr;
        std::filesystem::create_directory(root/L"probe-user");
        if (myime_create(utf8(shared.wstring()).c_str(),utf8((root/L"probe-user").wstring()).c_str(),schema.c_str(),&core))
            throw std::runtime_error(myime_last_error());
        struct Core { MyimeCore* value; ~Core() { myime_destroy(value); } } owner{core};
        for (int query=4;query<count;query+=2) {
            if (myime_clear(core)) throw std::runtime_error(myime_last_error());
            for (int key:utf8(args[query])) {
                uint32_t eaten=0; if (myime_key(core,key,0,&eaten) || !eaten) throw std::runtime_error("Imported-key processing failed");
            }
            MyimeState state{}; if (myime_state(core,&state)) throw std::runtime_error(myime_last_error());
            bool found=false;
            for (size_t i=0;i<state.count;++i) { MyimeCandidate candidate{};
                if (myime_candidate(core,i,&candidate)) throw std::runtime_error(myime_last_error());
                if (wide(candidate.text)==args[query+1]) found=true;
            }
            if (!found) throw std::runtime_error("Imported dictionary word not found in Rime candidates");
        }
        if (myime_deploy_schema(utf8(shared.wstring()).c_str(),utf8((root/L"probe-user").wstring()).c_str(),
            utf8((shared/(schema+".schema.yaml")).wstring()).c_str())!=-1)
            throw std::runtime_error("Offline deployment accepted a live session");
        // Leasing a generation is stable across concurrent apartments even if
        // a new offline selector is published between their activations.
        UserDataLease first,second;
        first.acquire(root/L"rime"/L"slots"); const auto original=first.shared_data(installed,root);
        std::ofstream(root/L"rime"/L"active-workspace.txt")<<"base\n";
        second.acquire(root/L"rime"/L"slots");
        if (second.shared_data(installed,root)!=original) throw std::runtime_error("Live apartment generation changed");
        first.release(); second.release();
        if (WorkspacePaths::shared(installed,root)!=installed/L"data"/L"shared") throw std::runtime_error("Base reset path failed");
        std::puts("Isolated deployment, imported Rime candidate, live-session rejection and apartment generation: PASS"); return 0;
    } catch (const std::exception& error) { std::fprintf(stderr,"Workspace probe: %s\n",error.what()); return 1; }
}
