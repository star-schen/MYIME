#pragma once
#include "encoding.h"
#include "candidate_theme.h"
#include <filesystem>
#include <algorithm>
#include <set>
#include <vector>

// Windows consumes copied presentation data. Rust owns parsing/default rules.
class ThemeCatalog {
    HMODULE module_=nullptr;
    std::filesystem::path user_,installed_;
    decltype(&myime_theme_resolve) resolve_=nullptr;
    decltype(&myime_theme_view) view_=nullptr;
    decltype(&myime_theme_destroy) destroy_=nullptr;
    decltype(&myime_last_error) error_=nullptr;
public:
    ThemeCatalog()=default;
    ThemeCatalog(const ThemeCatalog&)=delete;
    ThemeCatalog& operator=(const ThemeCatalog&)=delete;
    ~ThemeCatalog() { close(); }
    void close() noexcept {
        if (module_) FreeLibrary(module_);
        module_=nullptr; resolve_=nullptr; view_=nullptr; destroy_=nullptr; error_=nullptr;
    }
    void open(const std::filesystem::path& module_dir,const std::filesystem::path& data_root) {
        close(); user_=data_root/L"themes"; installed_=module_dir/L"themes";
        module_=LoadLibraryExW((module_dir/L"myime_core.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (!module_) throw std::runtime_error("Cannot load theme library");
        auto version=reinterpret_cast<decltype(&myime_theme_abi_version)>(GetProcAddress(module_,"myime_theme_abi_version"));
        resolve_=reinterpret_cast<decltype(resolve_)>(GetProcAddress(module_,"myime_theme_resolve"));
        view_=reinterpret_cast<decltype(view_)>(GetProcAddress(module_,"myime_theme_view"));
        destroy_=reinterpret_cast<decltype(destroy_)>(GetProcAddress(module_,"myime_theme_destroy"));
        error_=reinterpret_cast<decltype(error_)>(GetProcAddress(module_,"myime_last_error"));
        if (!version || version()!=1 || !resolve_ || !view_ || !destroy_ || !error_) {
            close(); throw std::runtime_error("Theme ABI mismatch");
        }
    }
    CandidateTheme resolve(const std::wstring& id) const {
        if (!module_) throw std::runtime_error("Theme catalog not open");
        MyimeTheme* handle=nullptr;
        if (resolve_(utf8(id).c_str(),utf8(user_.wstring()).c_str(),utf8(installed_.wstring()).c_str(),&handle))
            throw std::runtime_error(error_());
        struct Owner { MyimeTheme* value; decltype(&myime_theme_destroy) release; ~Owner() { release(value); } } owner{handle,destroy_};
        MyimeThemeView view{};
        if (view_(handle,sizeof(view),&view)) throw std::runtime_error(error_());
        CandidateTheme result{wide(view.id),wide(view.name),wide(view.version),wide(view.description),wide(view.font_family),wide(view.warning),view};
        result.style.id={}; result.style.name={}; result.style.version={}; result.style.description={};
        result.style.font_family={}; result.style.warning={};
        return result;
    }
    std::vector<std::wstring> ids() const {
        std::set<std::wstring> ids{L"default"};
        for (const auto& root:{installed_,user_}) {
            std::error_code error;
            auto iterator=std::filesystem::directory_iterator(root,error);
            if (error) continue;
            for (const auto& entry:iterator) {
                if (ids.size()>=128) break;
                const auto id=entry.path().filename().wstring();
                if (!id.empty() && id.size()<=64 && std::all_of(id.begin(),id.end(),[](wchar_t c) {
                    return (c>=L'a' && c<=L'z') || (c>=L'0' && c<=L'9') || c==L'-' || c==L'_';
                })) ids.insert(id);
            }
        }
        return {ids.begin(),ids.end()};
    }
};
