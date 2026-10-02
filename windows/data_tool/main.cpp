#include "encoding.h"
#include "workspace_paths.h"
#include <myime/core.h>
#include <objbase.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <vector>

namespace {
using Path=std::filesystem::path;
struct File {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~File() { if (value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    File()=default; File(const File&)=delete; File& operator=(const File&)=delete;
};
std::string json(const std::string& value) {
    std::string result="\"";
    constexpr char digits[]="0123456789abcdef";
    for (unsigned char c:value) {
        if (c=='"' || c=='\\') { result+='\\'; result+=c; }
        else if (c<32) { result+="\\u00"; result+=digits[c>>4]; result+=digits[c&15]; }
        else result+=c;
    }
    return result+'"';
}
std::string read(const Path&,size_t limit=1024*1024);
void write(const Path&,const std::string&);
bool reparse(const Path&);
void copy_tree(const Path&,const Path&);
std::string generation_id();
void publish(const Path&,const std::string&,const std::string&);
void verify(const Path&,const Path&,const std::string&);
std::string import_dictionary(const Path&,const Path&);
void validate_package(const Path&,const Path&);
void request_tool(const Path&,const Path&,const std::string&,const char*);
void check_configuration(const Path&,const Path&,const Path&);
int run(int count,wchar_t** args) {
    try {
        bool build=false,reset=false,rollback=false; Path root,patches,package;
        for (int i=1;i<count;++i) {
            const std::wstring argument=args[i];
            if (argument==L"--build") build=true;
            else if (argument==L"--reset") reset=true;
            else if (argument==L"--rollback") rollback=true;
            else if (i+1<count && argument==L"--data-root") root=args[++i];
            else if (i+1<count && argument==L"--patch-dir") patches=args[++i];
            else if (i+1<count && argument==L"--dictionary-dir") package=args[++i];
            else throw std::runtime_error("Usage: --build|--reset|--rollback --data-root ABSOLUTE [--patch-dir DIR] [--dictionary-dir PACKAGE]");
        }
        if (static_cast<int>(build)+static_cast<int>(reset)+static_cast<int>(rollback)!=1 || !root.is_absolute() || (build && !patches.is_absolute()) || (!package.empty() && !package.is_absolute()))
            throw std::runtime_error("Choose one operation and absolute paths");
        std::filesystem::create_directories(root/L"rime");
        root=std::filesystem::canonical(root);
        if (reparse(root/L"rime")) throw std::runtime_error("Rime data root may not be a reparse point");
        File lock; lock.value=CreateFileW((root/L"rime"/L".deploy.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (lock.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Another deployment is active or the data directory is not writable");
        wchar_t executable[32768]{}; const auto size=GetModuleFileNameW(nullptr,executable,_countof(executable));
        if (!size || size>=_countof(executable)) throw std::runtime_error("Cannot locate installed data");
        const auto application=Path(executable).parent_path();
        const auto base=application/L"data"/L"shared";
        const auto previous=WorkspacePaths::selector(root);
        if (rollback) {
            if (previous.empty() || previous=="base") throw std::runtime_error("No previous managed deployment is selected");
            const auto catalog=std::filesystem::canonical(root/L"rime"/L"workspaces");
            const auto current=std::filesystem::canonical(catalog/previous);
            if (current.parent_path()!=catalog || !std::filesystem::is_regular_file(current/L"ready")) throw std::runtime_error("Current workspace is invalid");
            std::string target=std::filesystem::exists(current/L"previous")?read(current/L"previous",128):"base";
            while (!target.empty() && (target.back()=='\r' || target.back()=='\n')) target.pop_back();
            if (!WorkspacePaths::valid_id(target)) throw std::runtime_error("Rollback identity is invalid");
            std::string schema="pinyin_simp";
            if (target!="base") {
                const auto restored=std::filesystem::canonical(catalog/target);
                if (restored.parent_path()!=catalog || !std::filesystem::is_regular_file(restored/L"ready") ||
                    !std::filesystem::is_regular_file(restored/L"shared"/L"build"/L"pinyin_simp.table.bin")) throw std::runtime_error("Previous deployment is unavailable");
                if (std::filesystem::exists(restored/L"schema-id")) schema=read(restored/L"schema-id",128);
                while (!schema.empty() && (schema.back()=='\r' || schema.back()=='\n')) schema.pop_back();
                if (!WorkspacePaths::valid_name(schema,128)) throw std::runtime_error("Previous schema identity is invalid");
            }
            check_configuration(application,root,target=="base"?base:catalog/target/L"shared");
            publish(root,previous,target);
            std::cout<<"{\"ok\":true,\"workspace\":"<<json(target)<<",\"schema\":"<<json(schema)<<"}\n"; return 0;
        }
        if (reset) {
            check_configuration(application,root,base);
            publish(root,previous,"base");
            std::cout<<"{\"ok\":true,\"workspace\":\"base\",\"schema\":\"pinyin_simp\"}\n"; return 0;
        }
        if (!std::filesystem::is_regular_file(base/L"pinyin_simp.schema.yaml")) throw std::runtime_error("Installed base schemas are missing");
        const auto id=generation_id(); const auto workspace=root/L"rime"/L"workspaces"/id;
        std::filesystem::create_directories(workspace.parent_path());
        if (reparse(workspace.parent_path()) || !std::filesystem::create_directory(workspace))
            throw std::runtime_error("Cannot create an isolated workspace");
        const auto shared=workspace/L"shared",user=workspace/L"user";
        copy_tree(base,shared); std::filesystem::create_directory(user);
        if (!previous.empty() && previous!="base") {
            const auto old=WorkspacePaths::shared(Path(executable).parent_path(),root);
            size_t files=0; uintmax_t bytes=0;
            for (const auto& entry:std::filesystem::directory_iterator(old)) {
                const auto name=entry.path().filename().wstring();
                if (!entry.is_regular_file() || (!name.ends_with(L".dict.yaml") && !name.ends_with(L".schema.yaml")) ||
                    std::filesystem::exists(base/entry.path().filename())) continue;
                if (reparse(entry.path()) || entry.file_size()>32ull*1024*1024) throw std::runtime_error("Prior dictionary source is invalid");
                bytes+=entry.file_size();
                if (++files>194 || bytes>512ull*1024*1024) throw std::runtime_error("Enabled dictionary sources exceed limits");
                std::filesystem::copy_file(entry.path(),shared/entry.path().filename());
            }
        }
        if (std::filesystem::exists(patches)) {
            if (reparse(patches)) throw std::runtime_error("Patch directory may not be a reparse point");
            size_t files=0;
            for (const auto& entry:std::filesystem::directory_iterator(patches)) {
                if (!entry.is_regular_file() || !entry.path().filename().wstring().ends_with(L".custom.yaml")) continue;
                if (++files>256 || reparse(entry.path())) throw std::runtime_error("Too many patches or a reparse point");
                // Exact bytes, including unknown settings/comments, are copied.
                write(user/entry.path().filename(),read(entry.path()));
            }
        }
        if (!std::filesystem::exists(user/L"default.custom.yaml"))
            write(user/L"default.custom.yaml","patch:\n  schema_list:\n    - schema: pinyin_simp\n  menu/page_size: 7\n");
        std::string schema="pinyin_simp";
        if (!previous.empty() && previous!="base") {
            const auto selected=root/L"rime"/L"workspaces"/previous/L"schema-id";
            if (std::filesystem::exists(selected)) schema=read(selected,128);
            while (!schema.empty() && (schema.back()=='\n' || schema.back()=='\r')) schema.pop_back();
            if (!WorkspacePaths::valid_name(schema,128)) throw std::runtime_error("Prior default schema identity is invalid");
        }
        if (!package.empty()) {
            const auto package_id=utf8(package.filename().wstring());
            if (!WorkspacePaths::valid_id(package_id)) throw std::runtime_error("Invalid package identity");
            const auto snapshot=workspace/L"packages"/package_id;
            std::filesystem::create_directories(snapshot);
            if (reparse(package)) throw std::runtime_error("Package directory may not be a reparse point");
            for (const auto& name:{package_id+".dict.yaml","myime_"+package_id+".dict.yaml","myime_"+package_id+".schema.yaml",
                    std::string("metadata.json"),std::string("README.md"),std::string("manifest.json")}) {
                const auto source=package/name;
                if (reparse(source) || !std::filesystem::is_regular_file(source) || std::filesystem::file_size(source)>64ull*1024*1024)
                    throw std::runtime_error("Package source is incomplete or too large");
                std::filesystem::copy_file(source,snapshot/name);
            }
            validate_package(Path(executable).parent_path(),snapshot);
            schema=import_dictionary(snapshot,shared);
        }
        // Dictionary composition is a Rust maintenance responsibility. Keep
        // the stable global schema while retaining per-package profile choices.
        request_tool(application,workspace,"{\"version\":1,\"command\":\"dictionary.combine\",\"shared_dir\":"+json(utf8(shared.wstring()))+"}","Cannot combine enabled dictionaries");
        schema=std::filesystem::is_regular_file(shared/L"myime_global.schema.yaml")?"myime_global":"pinyin_simp";
        const auto shared_utf8=utf8(shared.wstring()),user_utf8=utf8(user.wstring());
        if (myime_deploy(shared_utf8.c_str(),user_utf8.c_str())) throw std::runtime_error(myime_last_error());
        std::vector<std::string> schemas{"pinyin_simp"};
        for (const auto& entry:std::filesystem::directory_iterator(shared)) {
            const auto name=utf8(entry.path().filename().wstring());
            if (entry.is_regular_file() && name.starts_with("myime_") && name.ends_with(".schema.yaml")) {
                if (schemas.size()>=66) throw std::runtime_error("Too many enabled dictionary packages");
                if (myime_deploy_schema(shared_utf8.c_str(),user_utf8.c_str(),utf8(entry.path().wstring()).c_str()))
                    throw std::runtime_error(myime_last_error());
                schemas.push_back(name.substr(0,name.size()-12));
            }
        }
        std::filesystem::create_directory(shared/L"build");
        for (const auto& entry:std::filesystem::directory_iterator(user/L"build"))
            if (entry.is_regular_file()) std::filesystem::copy_file(entry.path(),shared/L"build"/entry.path().filename());
        if (!std::filesystem::is_regular_file(shared/L"build"/L"pinyin_simp.table.bin") ||
            !std::filesystem::is_regular_file(shared/L"build"/(schema+".table.bin")))
            throw std::runtime_error("Rime did not build all required dictionaries");
        // Validate every compiled schema, including base dependencies used by
        // explicit AppProfiles, rather than checking file existence alone.
        for (const auto& entry:std::filesystem::directory_iterator(shared/L"build")) {
            const auto name=utf8(entry.path().filename().wstring());
            if (entry.is_regular_file() && name.ends_with(".schema.yaml"))
                verify(shared,user,name.substr(0,name.size()-12));
        }
        write(workspace/L"ready","MYIME workspace v1\n");
        write(workspace/L"schema-id",schema+"\n");
        if (!previous.empty()) write(workspace/L"previous",previous+"\n");
        check_configuration(application,root,shared);
        publish(root,previous,id);
        std::cout<<"{\"ok\":true,\"workspace\":"<<json(id)<<",\"schema\":"<<json(schema)<<"}\n"; return 0;
    } catch (const std::exception& error) {
        std::cout<<"{\"ok\":false,\"error\":"<<json(error.what())<<"}\n"; return 1;
    }
}
std::string read(const Path& file,size_t limit) {
    std::ifstream input(file,std::ios::binary); if (!input) throw std::runtime_error("Cannot read data file");
    std::string text(limit+1,'\0'); input.read(text.data(),static_cast<std::streamsize>(text.size()));
    text.resize(static_cast<size_t>(input.gcount()));
    if (text.size()>limit) throw std::runtime_error("Data file exceeds size limit"); return text;
}
void write(const Path& file,const std::string& text) {
    std::ofstream output(file,std::ios::binary|std::ios::trunc); output.write(text.data(),static_cast<std::streamsize>(text.size()));
    output.close(); if (!output) throw std::runtime_error("Cannot write workspace file");
}
bool reparse(const Path& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES) throw std::runtime_error("Cannot inspect data path");
    return (attributes&FILE_ATTRIBUTE_REPARSE_POINT)!=0;
}
void copy_tree(const Path& source,const Path& target) {
    if (reparse(source)) throw std::runtime_error("Reparse points are not allowed in deployment data");
    std::filesystem::create_directories(target); size_t files=0; uintmax_t bytes=0;
    for (const auto& entry:std::filesystem::recursive_directory_iterator(source)) {
        if (reparse(entry.path())) throw std::runtime_error("Reparse points are not allowed in deployment data");
        const auto relative=entry.path().lexically_relative(source);
        if (relative.begin()!=relative.end() && *relative.begin()==L"build") continue;
        const auto destination=target/relative;
        if (entry.is_directory()) std::filesystem::create_directories(destination);
        else if (entry.is_regular_file()) {
            bytes+=entry.file_size(); if (++files>10000 || bytes>512ull*1024*1024) throw std::runtime_error("Deployment input exceeds limits");
            std::filesystem::copy_file(entry.path(),destination);
        } else throw std::runtime_error("Unsupported deployment file");
    }
}
std::string generation_id() {
    GUID value{}; if (FAILED(CoCreateGuid(&value))) throw std::runtime_error("Cannot allocate workspace identity");
    wchar_t text[40]{}; StringFromGUID2(value,text,_countof(text));
    auto id=utf8(text); id.erase(std::remove_if(id.begin(),id.end(),[](char c) { return c=='{' || c=='}'; }),id.end());
    for (auto& c:id) if (c>='A' && c<='F') c+=32;
    return "g-"+id;
}
void publish(const Path& root,const std::string& previous,const std::string& id) {
    if (WorkspacePaths::selector(root)!=previous) throw std::runtime_error("Workspace selector was modified externally; deployment not activated");
    const auto target=root/L"rime"/L"active-workspace.txt";
    const auto pending=root/L"rime"/(generation_id()+".pending");
    write(pending,id+"\n");
    File file; file.value=CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if (file.value==INVALID_HANDLE_VALUE || !FlushFileBuffers(file.value)) throw std::runtime_error("Cannot flush workspace selector");
    CloseHandle(file.value); file.value=INVALID_HANDLE_VALUE;
    if (WorkspacePaths::selector(root)!=previous) throw std::runtime_error("Workspace selector changed before publication");
    if (!MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot publish workspace selector");
}
void verify(const Path& shared,const Path& user,const std::string& schema) {
    MyimeCore* core=nullptr;
    if (myime_create(utf8(shared.wstring()).c_str(),utf8(user.wstring()).c_str(),schema.c_str(),&core))
        throw std::runtime_error(myime_last_error());
    if (myime_destroy(core)) throw std::runtime_error(myime_last_error());
}
std::string import_dictionary(const Path& package,const Path& shared) {
    if (reparse(package)) throw std::runtime_error("Dictionary package may not be a reparse point");
    const auto id=utf8(package.filename().wstring());
    if (!WorkspacePaths::valid_id(id) || id=="pinyin_simp" || id=="stroke" || id.starts_with("myime_"))
        throw std::runtime_error("Dictionary package id is invalid or reserved");
    const auto dictionary=package/(id+".dict.yaml");
    if (!std::filesystem::is_regular_file(package/L"manifest.json") || reparse(dictionary))
        throw std::runtime_error("Dictionary package is incomplete");
    if (std::filesystem::file_size(dictionary)>32ull*1024*1024) throw std::runtime_error("Dictionary exceeds 32 MiB");
    std::filesystem::copy_file(dictionary,shared/dictionary.filename(),std::filesystem::copy_options::overwrite_existing);
    const std::string schema="myime_"+id;
    for (const auto& name:{schema+".dict.yaml",schema+".schema.yaml"})
        std::filesystem::copy_file(package/name,shared/name,std::filesystem::copy_options::overwrite_existing);
    return schema;
}
void validate_package(const Path& application,const Path& package) {
    request_tool(application,package.parent_path(),"{\"version\":1,\"command\":\"package.validate\",\"path\":"+json(utf8(package.wstring()))+"}",
        "Package is incomplete, modified, or incompatible; regenerate it before deployment");
}
void check_configuration(const Path& application,const Path& root,const Path& shared) {
    std::string schemas="["; bool first=true;
    for (const auto& entry:std::filesystem::directory_iterator(shared/L"build")) {
        const auto name=utf8(entry.path().filename().wstring());
        if (!entry.is_regular_file() || !name.ends_with(".schema.yaml")) continue;
        const auto id=name.substr(0,name.size()-12);
        if (!WorkspacePaths::valid_name(id,128)) continue;
        if (!first) schemas+=','; first=false; schemas+=json(id);
    }
    schemas+=']';
    request_tool(application,root/L"rime","{\"version\":1,\"command\":\"config.check-schemas\",\"path\":"+
        json(utf8((root/L"config.toml").wstring()))+",\"schemas\":"+schemas+"}",
        "Deployment would leave an enabled configuration pointing to an unavailable schema; change Windows and AppProfile schemas first");
}
void request_tool(const Path& application,const Path& directory,const std::string& payload,const char* failure) {
    const auto id=generation_id();
    const auto request=directory/(id+".request.json"),reply=directory/(id+".reply.json");
    struct Scratch { Path request,reply; ~Scratch() { std::error_code ignored; std::filesystem::remove(request,ignored); std::filesystem::remove(reply,ignored); } } scratch{request,reply};
    write(request,payload);
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,TRUE};
    File input,output;
    input.value=CreateFileW(request.c_str(),GENERIC_READ,FILE_SHARE_READ,&security,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    output.value=CreateFileW(reply.c_str(),GENERIC_WRITE,FILE_SHARE_READ,&security,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (input.value==INVALID_HANDLE_VALUE || output.value==INVALID_HANDLE_VALUE) throw std::runtime_error("Cannot prepare Rust maintenance request");
    const auto executable=application/L"myime-tool.exe"; std::wstring command=L"\""+executable.wstring()+L"\" --request";
    STARTUPINFOW startup{sizeof(startup)}; startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input.value; startup.hStdOutput=startup.hStdError=output.value;
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,application.c_str(),&startup,&process))
        throw std::runtime_error("Cannot start Rust maintenance tool");
    File thread,handle; thread.value=process.hThread; handle.value=process.hProcess;
    if (WaitForSingleObject(handle.value,30000)!=WAIT_OBJECT_0) {
        TerminateProcess(handle.value,2); WaitForSingleObject(handle.value,5000); throw std::runtime_error("Rust maintenance request timed out");
    }
    DWORD result=1;
    if (!GetExitCodeProcess(handle.value,&result) || result) {
        CloseHandle(output.value); output.value=INVALID_HANDLE_VALUE;
        throw std::runtime_error(std::string(failure)+": "+read(reply,65536));
    }
}
}
int wmain(int count,wchar_t** args) { return run(count,args); }
