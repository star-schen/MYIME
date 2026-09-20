// Mechanical adapter to the versioned official API. No input policy here.
#define RIME_IMPORTS
#include <rime_api.h>
#include <new>
#include <cstring>
#include <atomic>
extern "C" {
int rb_initialize(const char* shared, const char* user, int first) {
    auto api = rime_get_api();
    if (!RIME_API_AVAILABLE(api, select_candidate_on_current_page)) return 0;
    RIME_STRUCT(RimeTraits, traits);
    traits.shared_data_dir = shared;
    traits.user_data_dir = user;
    traits.distribution_name = "MYIME";
    traits.distribution_code_name = "myime";
    traits.distribution_version = "0.1.0";
    traits.app_name = "rime.myime";
    traits.min_log_level = 2;
    if (first) api->setup(&traits);
    api->initialize(&traits);
    return 1;
}
void rb_finalize() { rime_get_api()->finalize(); }
int rb_deploy() {
    auto api = rime_get_api();
    std::atomic<int> result{0};
    api->set_notification_handler([](void* context, RimeSessionId, const char* type, const char* value) {
        if (std::strcmp(type,"deploy")!=0) return;
        if (std::strcmp(value,"success")==0) static_cast<std::atomic<int>*>(context)->store(1);
        if (std::strcmp(value,"failure")==0) static_cast<std::atomic<int>*>(context)->store(-1);
    }, &result);
    if (api->start_maintenance(1)) api->join_maintenance_thread();
    api->set_notification_handler(nullptr,nullptr);
    return result.load()==1;
}
uintptr_t rb_create() { return rime_get_api()->create_session(); }
void rb_destroy(uintptr_t id) { rime_get_api()->destroy_session(id); }
int rb_key(uintptr_t id, int key, int mask) { return rime_get_api()->process_key(id, key, mask); }
int rb_select(uintptr_t id, size_t index) { return rime_get_api()->select_candidate_on_current_page(id, index); }
int rb_schema(uintptr_t id, const char* schema) { return rime_get_api()->select_schema(id, schema); }
void rb_option(uintptr_t id, const char* name, int value) { rime_get_api()->set_option(id, name, value); }
int rb_get_option(uintptr_t id, const char* name) { return rime_get_api()->get_option(id, name); }
void rb_clear(uintptr_t id) { rime_get_api()->clear_composition(id); }
// Opaque output owners prevent any dependency on Rime's struct layout in Rust.
RimeContext* rb_context(uintptr_t id) {
    auto p = new(std::nothrow) RimeContext{};
    if (!p) return nullptr;
    RIME_STRUCT_INIT(RimeContext, *p);
    if (!rime_get_api()->get_context(id, p)) { delete p; return nullptr; }
    return p;
}
void rb_free_context(RimeContext* p) { rime_get_api()->free_context(p); delete p; }
const char* rb_preedit(RimeContext* p) { return p->composition.preedit; }
int rb_caret(RimeContext* p) { return p->composition.cursor_pos; }
int rb_count(RimeContext* p) { return p->menu.num_candidates; }
int rb_selected(RimeContext* p) { return p->menu.highlighted_candidate_index; }
int rb_page(RimeContext* p) { return p->menu.page_no; }
int rb_page_size(RimeContext* p) { return p->menu.page_size; }
int rb_last_page(RimeContext* p) { return p->menu.is_last_page; }
const char* rb_candidate(RimeContext* p, size_t i) { return p->menu.candidates[i].text; }
const char* rb_comment(RimeContext* p, size_t i) { return p->menu.candidates[i].comment; }
const char* rb_label(RimeContext* p, size_t i) { return p->select_labels ? p->select_labels[i] : nullptr; }
char rb_select_key(RimeContext* p, size_t i) { return p->menu.select_keys && i < std::strlen(p->menu.select_keys) ? p->menu.select_keys[i] : 0; }
RimeCommit* rb_commit(uintptr_t id) {
    auto p = new(std::nothrow) RimeCommit{};
    if (!p) return nullptr;
    RIME_STRUCT_INIT(RimeCommit, *p);
    if (!rime_get_api()->get_commit(id, p)) { delete p; return nullptr; }
    return p;
}
const char* rb_commit_text(RimeCommit* p) { return p->text; }
void rb_free_commit(RimeCommit* p) { rime_get_api()->free_commit(p); delete p; }
}
