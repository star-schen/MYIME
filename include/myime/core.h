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
int32_t myime_destroy(MyimeCore*);
int32_t myime_deploy(const char* shared, const char* user);
int32_t myime_key(MyimeCore*, int32_t keysym, int32_t rime_mask, uint32_t* eaten);
int32_t myime_select(MyimeCore*, size_t page_index);
int32_t myime_clear(MyimeCore*);
int32_t myime_ack_commit(MyimeCore*);
int32_t myime_state(MyimeCore*, MyimeState*);
int32_t myime_candidate(MyimeCore*, size_t page_index, MyimeCandidate*);
int32_t myime_load_config(MyimeCore*, const char* product_config_path);
int32_t myime_apply_profile(MyimeCore*, const char* executable_basename, uint32_t* enabled);
#ifdef __cplusplus
}
#endif
