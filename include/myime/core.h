#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
uint32_t myime_abi_version(void);
typedef struct MyimeCore MyimeCore;
typedef struct { const char* data; size_t len; } MyimeText;
typedef struct {
    MyimeText preedit, commit, schema;
    size_t caret, count, selected, page, page_size;
    uint32_t active, last_page, options;
} MyimeState;
typedef struct { MyimeText text, comment, label; size_t index; } MyimeCandidate;
/* All strings UTF-8. Handles are thread-affine. 0 success, -1 failure.
 * Views are borrowed until the next mutation/destroy of that handle.
 * Never free views. All output pointers must be valid; no concurrent handle use.
 * caret is a byte offset; selected/select use current-page index, candidate.index global.
 * options bits: ascii_mode, full_shape, simplification, ascii_punct.
 */
const char* myime_last_error(void);
int32_t myime_create(const char* shared, const char* user, const char* schema, MyimeCore** out);
/* Explicit shared learning directory, provisioned by the installer. Empty string
 * disables learning. If shared learning is inaccessible, deployed-dictionary
 * input remains usable; learning_available/status report the degraded state.
 * No fallback to a private dictionary. Legacy create() remains strict and
 * isolated under user/learning for offline tools and existing ABI callers. */
int32_t myime_create_shared(const char* shared, const char* user, const char* schema, const char* learning, MyimeCore** out);
int32_t myime_destroy(MyimeCore*);
int32_t myime_deploy(const char* shared, const char* user);
/* Offline maintenance only; caller owns an isolated writable workspace.
 * Compile one explicit schema after normal deployment; no live sessions.
 */
int32_t myime_deploy_schema(const char* shared, const char* user, const char* schema_file);
int32_t myime_key(MyimeCore*, int32_t keysym, int32_t rime_mask, uint32_t* eaten);
int32_t myime_select(MyimeCore*, size_t page_index);
int32_t myime_clear(MyimeCore*);
int32_t myime_ack_commit(MyimeCore*);
int32_t myime_state(MyimeCore*, MyimeState*);
int32_t myime_candidate(MyimeCore*, size_t page_index, MyimeCandidate*);
int32_t myime_load_config(MyimeCore*, const char* product_config_path);
int32_t myime_apply_profile(MyimeCore*, const char* executable_basename, uint32_t* enabled);
/* Additive ABI v1 export: theme id from last successfully applied config.
 * Read-only, borrowed until next Core mutation/destroy. Default is "default".
 */
int32_t myime_ui_theme_id(MyimeCore*, MyimeText*);
/* Additive ABI v1. focus: 0 retain original context, 1 cancel, 2 commit.
 * caps_lock: 0 uppercase literal composition, 1 commit then direct.
 * Commit text is still borrowed and must be acknowledged after document write.
 * Reload is called outside key callbacks; input-provider changes are deferred.
 */
typedef struct { uint32_t focus, caps_lock; } MyimeInputPolicy;
int32_t myime_input_policy(MyimeCore*, MyimeInputPolicy*);
int32_t myime_commit_current(MyimeCore*);
int32_t myime_reload_config(MyimeCore*, const char* path, uint32_t* enabled);
typedef struct { MyimeText group; size_t group_count; uint32_t habits_enabled; } MyimeLearningInfo;
/* Application is the actual full process executable path. Empty manual_group
 * follows configured mapping (unmapped=daily). Changes during composition are
 * applied after its commit is acknowledged, preserving that draft's tags.
 */
int32_t myime_learning_context(MyimeCore*, const char* application, const char* manual_group);
int32_t myime_learning_info(MyimeCore*, MyimeLearningInfo*);
int32_t myime_learning_group(MyimeCore*, size_t index, MyimeText* id, MyimeText* name);
/* Read and clear the process storage diagnostic; 0 means no recorded error,
 * -1 means learning was disabled at initialization; positive values are native
 * storage codes. This is separate from persistent availability. A storage error
 * never causes Host key replay or loss of a native commit. */
int32_t myime_learning_status(void);
/* Additive ABI v1 export: 1 if the active process runtime initialized learning
 * successfully; 0 if unavailable or inactive. No file I/O; does not clear errors.
 * Availability is conservative until the runtime is reinitialized. */
int32_t myime_learning_available(void);
/* Manual, offline tool process only. user is an isolated maintenance workspace;
 * operation 1 merges a closed legacy slot, 2 backs up to a new directory,
 * 3 performs a passive checkpoint. result=-2 means an active slot was skipped.
 * Existing files are retained. The normal Host never invokes this entry point. */
int32_t myime_maintain_learning(const char* shared, const char* user, const char* learning, int32_t operation, const char* argument, int32_t* result);
#ifdef __cplusplus
}
#endif
