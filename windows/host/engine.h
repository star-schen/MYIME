#pragma once
#include <windows.h>
#include <myime/core.h>
#include <filesystem>
#include <string>
#include <stdexcept>
#include "compatibility.h"
#include "user_data.h"
#include "diagnostics.h"
#include "encoding.h"
#include "workspace_paths.h"
#include "shared_profile.h"
#include <set>
#include <map>
class Engine {
    HMODULE dll_=nullptr;
    UserDataLease user_data_;
    std::set<MyimeCore*> sessions_;
    std::set<MyimeCore*> pending_;
    std::map<MyimeCore*,bool> enabled_values_;
    MyimeCore* unassigned_=nullptr;
    std::string shared_path_,user_path_,learning_path_;
    std::filesystem::path local_config_,shared_root_;
    const Diagnostics* diagnostics_=nullptr;
    mutable bool shared_settings_available_=false,presentation_checked_=false;
    bool presentation_disabled_=false;
    std::filesystem::path shared_presentation() const {
        if(shared_root_.empty() || presentation_disabled_)return {};
        try {
            auto generation=SharedProfile::presentation(shared_root_);
            const bool available=!generation.empty();
            if(!available && (!presentation_checked_ || shared_settings_available_) && diagnostics_)
                diagnostics_->event(L"Shared presentation not published; using local settings",HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND));
            shared_settings_available_=available;presentation_checked_=true;
            return generation;
        } catch(const std::exception& error) {
            if((!presentation_checked_ || shared_settings_available_) && diagnostics_)
                diagnostics_->failure(L"Shared presentation unavailable; using local settings",error);
            shared_settings_available_=false;presentation_checked_=true;
            return {};
        }
    }
    void load_product_config(MyimeCore* value) {
        const auto path=config_path();
        if(!load_config(value,utf8(path.wstring()).c_str()))return;
        const std::string detail=last_error();
        if(path==local_config_)throw std::runtime_error("Local product config failed: "+detail);
        if(diagnostics_)diagnostics_->failure(L"Shared product config unavailable; using local settings",std::runtime_error(detail));
        presentation_disabled_=true;shared_settings_available_=false;
        if(load_config(value,utf8(local_config_.wstring()).c_str()))throw std::runtime_error(last_error());
    }
    void report_learning_status() const {
        if(!diagnostics_)return;
        if(const auto status=learning_status()) {
            wchar_t message[160]{};
            swprintf_s(message,L"Shared learning initialization status=%d; input engine retained",status);
            diagnostics_->event(message,E_FAIL);
        }
        if(!shared_learning_available())diagnostics_->event(L"Shared learning unavailable; base dictionary input retained",E_FAIL);
    }
public:
    MyimeCore* handle=nullptr;
    bool enabled=true;
#define API(name) decltype(&myime_##name) name=nullptr
    API(create_shared); API(destroy); API(key); API(select); API(clear); API(state); API(candidate); API(ack_commit); API(last_error);
    API(load_config); API(apply_profile); API(ui_theme_id); API(input_policy); API(commit_current); API(reload_config);
    API(learning_context); API(learning_info); API(learning_group); API(learning_status); API(learning_available);
#undef API
    ~Engine() { close(); }
    bool profile() {
        uint32_t value=0;
        if (apply_profile(handle,utf8(CompatibilityLayer::executable()).c_str(),&value)) { enabled=false; return false; }
        enabled=value!=0;enabled_values_[handle]=enabled; return true;
    }
    std::filesystem::path config_path() const { auto generation=shared_presentation(); return generation.empty()?local_config_:generation/L"config.toml"; }
    std::filesystem::path theme_root() const { auto generation=shared_presentation(); return generation.empty()?local_config_.parent_path():generation; }
    bool shared_settings_available() const noexcept { return shared_settings_available_; }
    bool shared_learning_available() const { return dll_ && learning_available && learning_available()>0; }
    MyimeCore* new_session() {
        MyimeCore* result=nullptr;
        if (create_shared(shared_path_.c_str(),user_path_.c_str(),"pinyin_simp",learning_path_.c_str(),&result)) throw std::runtime_error(last_error());
        try { uint32_t value=0; load_product_config(result); if(apply_profile(result,utf8(CompatibilityLayer::executable()).c_str(),&value))throw std::runtime_error(last_error()); enabled_values_.emplace(result,value!=0);sessions_.insert(result);report_learning_status();return result; }
        catch (...) { sessions_.erase(result);enabled_values_.erase(result);destroy(result); throw; }
    }
    MyimeCore* take_session(){if(unassigned_){auto value=unassigned_;unassigned_=nullptr;return value;}return new_session();}
    void use_session(MyimeCore* value){handle=value;auto entry=enabled_values_.find(value);enabled=entry!=enabled_values_.end()&&entry->second;}
    void release_session(MyimeCore* value) { pending_.erase(value);enabled_values_.erase(value);if(unassigned_==value)unassigned_=nullptr;if (sessions_.erase(value)) destroy(value); if (handle==value) handle=nullptr; }
    void refresh() { const auto path=utf8(config_path().wstring()); for (auto value:sessions_) { uint32_t active=0; if (reload_config(value,path.c_str(),&active)) throw std::runtime_error(last_error()); enabled_values_[value]=active!=0;if (value==handle) enabled=active!=0; MyimeState view{};if(state(value,&view))throw std::runtime_error(last_error());if(view.active||view.commit.len)pending_.insert(value);else pending_.erase(value); } }
    void apply_pending(){
        for(auto iterator=pending_.begin();iterator!=pending_.end();){
            auto value=*iterator;MyimeState view{};if(state(value,&view))throw std::runtime_error(last_error());
            if(view.active||view.commit.len){++iterator;continue;}
            uint32_t active=0;if(apply_profile(value,utf8(CompatibilityLayer::executable()).c_str(),&active))throw std::runtime_error(last_error());
            enabled_values_[value]=active!=0;if(value==handle)enabled=active!=0;iterator=pending_.erase(iterator);
        }
    }
    MyimeInputPolicy policy() const { MyimeInputPolicy value{}; if (handle) input_policy(handle,&value); return value; }
    void close() { for (auto value:sessions_) destroy(value); sessions_.clear(); pending_.clear();enabled_values_.clear();unassigned_=nullptr;handle=nullptr; user_data_.release(); if (dll_) { FreeLibrary(dll_); dll_=nullptr; } shared_path_.clear();user_path_.clear();learning_path_.clear();local_config_.clear();shared_root_.clear();diagnostics_=nullptr;shared_settings_available_=false;presentation_checked_=false;presentation_disabled_=false; }
    void open(const std::filesystem::path& module_dir,const std::filesystem::path& data_root,const Diagnostics& diagnostics) {
        diagnostics_=&diagnostics;
        const wchar_t* stage=L"Loading Core and native dependencies";
        try {
            diagnostics.event(stage);
            dll_=LoadLibraryExW((module_dir/L"myime_core.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!dll_) throw PlatformError("Load Core DLL",HRESULT_FROM_WIN32(GetLastError()));
            diagnostics.module(L"myime_core.dll"); diagnostics.module(L"rime.dll");
#define LOAD(name) name=reinterpret_cast<decltype(name)>(GetProcAddress(dll_,"myime_" #name)); if (!name) throw PlatformError("Core ABI export missing: myime_" #name,HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND))
            LOAD(create_shared); LOAD(destroy); LOAD(key); LOAD(select); LOAD(clear); LOAD(state); LOAD(candidate); LOAD(ack_commit); LOAD(last_error);
            LOAD(load_config); LOAD(apply_profile); LOAD(ui_theme_id); LOAD(input_policy); LOAD(commit_current); LOAD(reload_config); LOAD(learning_context); LOAD(learning_info); LOAD(learning_group); LOAD(learning_status); LOAD(learning_available);
#undef LOAD
            auto abi=reinterpret_cast<decltype(&myime_abi_version)>(GetProcAddress(dll_,"myime_abi_version"));
            if (!abi || abi()!=1) throw std::runtime_error("Core ABI version mismatch");
            stage=L"Acquiring user directory lease";diagnostics.event(stage);
            const auto user_dir=user_data_.acquire(data_root/L"rime"/L"slots");
            diagnostics.event(user_dir.c_str());
            stage=L"Resolving deployed Rime workspace and selector";diagnostics.event(stage);
            const auto shared=user_data_.shared_data(module_dir,data_root);
            diagnostics.event(shared.c_str());
            stage=L"Encoding Rime workspace and user paths";diagnostics.event(stage);
            shared_path_=utf8(shared.wstring());user_path_=utf8(user_dir.wstring());local_config_=data_root/L"config.toml";
            stage=L"Checking deployed base dictionary";diagnostics.event(stage);
            if(!std::filesystem::is_regular_file(shared/L"build"/L"pinyin_simp.table.bin"))throw std::runtime_error("Deploy and stage Rime data before activation");
            stage=L"Resolving optional shared settings and learning";diagnostics.event(stage);
            try {
                shared_root_=SharedProfile::root(module_dir,&diagnostics);
                diagnostics.event(L"Encoding shared learning path");
                learning_path_=utf8((shared_root_/L"learning").wstring());
                diagnostics.event(L"Shared settings and learning paths resolved");
            } catch(const std::exception& error) {
                diagnostics.failure(L"Optional shared profile unavailable; retaining base input",error);
                shared_root_.clear();learning_path_.clear();shared_settings_available_=false;
            }
            stage=L"Creating Rime session from deployed data";diagnostics.event(stage);
            if(create_shared(shared_path_.c_str(),user_path_.c_str(),"pinyin_simp",learning_path_.c_str(),&handle))throw std::runtime_error(last_error());
            try { sessions_.insert(handle); } catch(...) { destroy(handle);handle=nullptr;throw; }
            unassigned_=handle;report_learning_status();
            stage=L"Loading product config";diagnostics.event(stage);
            load_product_config(handle);
            stage=L"Applying AppProfile";diagnostics.event(stage);
            if(!profile())throw std::runtime_error(last_error());
        } catch(const std::exception& error) { diagnostics.failure(stage,error);throw; }
    }
};
