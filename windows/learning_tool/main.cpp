#include <myime/core.h>
#include "encoding.h"
#include "shared_profile.h"
#include "workspace_paths.h"
#include <iostream>
#include <stdexcept>
#include <objbase.h>

namespace {
using Path=std::filesystem::path;
std::string json(const std::string& value){
    std::string output="\"";constexpr char hex[]="0123456789abcdef";
    for(unsigned char c:value){if(c=='"'||c=='\\'){output+='\\';output+=c;}else if(c<32){output+="\\u00";output+=hex[c>>4];output+=hex[c&15];}else output+=c;}return output+'"';
}
bool reparse(const Path& path){const auto attributes=GetFileAttributesW(path.c_str());return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT);}
int run(int count,wchar_t** arguments){
    try{
        int operation=0;Path root,output;
        for(int index=1;index<count;index++){
            const std::wstring value=arguments[index];
            if(value==L"--merge-slots"){if(operation)throw std::runtime_error("Choose one operation");operation=1;}
            else if(value==L"--backup"){if(operation)throw std::runtime_error("Choose one operation");operation=2;}
            else if(value==L"--checkpoint"){if(operation)throw std::runtime_error("Choose one operation");operation=3;}
            else if(value==L"--data-root" && index+1<count)root=arguments[++index];
            else if(value==L"--output" && index+1<count)output=arguments[++index];
            else throw std::runtime_error("Unknown learning maintenance argument");
        }
        if(!operation || !root.is_absolute() || (operation==2 && !output.is_absolute()))throw std::runtime_error("Choose a maintenance operation and absolute paths");
        if(reparse(root))throw std::runtime_error("Data root may not be a reparse point");
        wchar_t executable[32768]{};const auto size=GetModuleFileNameW(nullptr,executable,_countof(executable));
        if(!size || size>=_countof(executable))throw std::runtime_error("Cannot locate installed runtime");
        const auto shared=WorkspacePaths::shared(Path(executable).parent_path(),root);
        const auto learning=SharedProfile::root(Path(executable).parent_path())/L"learning";
        const auto work_parent=root/L"tools"/L"learning-work";
        std::filesystem::create_directories(work_parent);
        if(reparse(work_parent))throw std::runtime_error("Maintenance directory may not be a reparse point");
        GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot allocate maintenance workspace");
        wchar_t formatted[40]{};StringFromGUID2(id,formatted,_countof(formatted));const auto workspace=work_parent/formatted;
        if(!std::filesystem::create_directory(workspace))throw std::runtime_error("Cannot create isolated maintenance workspace");
        // This directory contains Rime deployment metadata, never live userdb.
        struct Workspace{Path path,parent;~Workspace(){if(path.is_absolute() && path.lexically_normal().parent_path()==parent.lexically_normal() && !reparse(path)){std::error_code ignored;std::filesystem::remove_all(path,ignored);}}} owned{workspace,work_parent};
        int merged=0,skipped=0;
        auto maintain=[&](const Path& argument){int32_t result=0;if(myime_maintain_learning(utf8(shared.wstring()).c_str(),utf8(workspace.wstring()).c_str(),utf8(learning.wstring()).c_str(),operation,utf8(argument.wstring()).c_str(),&result))throw std::runtime_error(myime_last_error());return result;};
        if(operation==1){
            const auto slots=root/L"rime"/L"slots";
            if(std::filesystem::exists(slots)){
                if(reparse(slots))throw std::runtime_error("Legacy slot directory may not be a reparse point");
                for(const auto& entry:std::filesystem::directory_iterator(slots)){
                    const auto name=entry.path().filename().wstring();
                    if(!entry.is_directory() || reparse(entry.path()) || name.empty() || name.size()>3 || name.find_first_not_of(L"0123456789")!=std::wstring::npos)continue;
                    const auto result=maintain(entry.path());if(result==-2)skipped++;else merged+=result;
                }
            }
        }else merged=maintain(operation==2?output:Path());
        std::cout<<"{\"ok\":true,\"count\":"<<merged<<",\"skipped\":"<<skipped<<",\"output\":"<<json(utf8(output.wstring()))<<"}\n";return 0;
    }catch(const std::exception& error){std::cout<<"{\"ok\":false,\"error\":"<<json(error.what())<<"}\n";return 1;}
}
}
int wmain(int count,wchar_t** arguments){return run(count,arguments);}
