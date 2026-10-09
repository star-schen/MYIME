#pragma once
#include <myime/ui_language.h>
#include "encoding.h"
#include <filesystem>
#include <map>
#include <stdexcept>

// Owned menu strings. All loading happens outside the keystroke path.
class UiLanguage {
    std::map<std::string,std::wstring,std::less<>> messages_;
public:
    void open(const std::filesystem::path& module_directory,const std::filesystem::path& data_root) {
        HMODULE module=LoadLibraryExW((module_directory/L"myime_core.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module)throw std::runtime_error("Cannot load interface language library");
        struct Library { HMODULE value; ~Library(){FreeLibrary(value);} } library{module};
        auto version=reinterpret_cast<decltype(&myime_ui_language_abi_version)>(GetProcAddress(module,"myime_ui_language_abi_version"));
        auto load=reinterpret_cast<decltype(&myime_ui_language_load)>(GetProcAddress(module,"myime_ui_language_load"));
        auto get=reinterpret_cast<decltype(&myime_ui_language_text)>(GetProcAddress(module,"myime_ui_language_text"));
        auto release=reinterpret_cast<decltype(&myime_ui_language_destroy)>(GetProcAddress(module,"myime_ui_language_destroy"));
        if(!version || version()!=1 || !load || !get || !release)throw std::runtime_error("UI language ABI mismatch");
        const auto language=GetUserDefaultUILanguage();
        const char* fallback=language==0x0404 || language==0x0c04 || language==0x1404?"zh-Hant":"zh-Hans";
        MyimeUiLanguage* handle=nullptr;
        if(load(utf8(data_root.wstring()).c_str(),utf8(module_directory.wstring()).c_str(),fallback,&handle))throw std::runtime_error("Cannot load interface language");
        struct Owner { MyimeUiLanguage* value; decltype(release) destroy; ~Owner(){destroy(value);} } owner{handle,release};
        std::map<std::string,std::wstring,std::less<>> messages;
        for(const char* key:{"MYIME 输入模式","MYIME：英文（左键切换尚未实现）","MYIME：中文（左键切换尚未实现）","设置","按分组使用学习习惯","全局学习（分组仅作标签）","跟随应用分组","共享学习不可用；基础输入可继续","共享设置不可用；使用本地设置"}) {
            MyimeText value{}; messages.emplace(key,get(handle,key,&value)?wide(MyimeText{key,std::char_traits<char>::length(key)}):wide(value));
        }
        messages_=std::move(messages);
    }
    const wchar_t* text(const char* key) const noexcept {
        const auto found=messages_.find(key); return found==messages_.end()?L"MYIME":found->second.c_str();
    }
};
