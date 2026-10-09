#pragma once
#include <myime/core.h>
#ifdef __cplusplus
extern "C" {
#endif
/* UI ABI 1. Thread-affine immutable snapshot; borrowed text lives until destroy.
 * Loading performs bounded file I/O at activation/settings refresh only.
 * user_root contains ui-language.txt and languages/<id>.json.
 * Missing or invalid external packs fall back to embedded Chinese messages.
 * This API does not change input modes, schemas, dictionaries or candidates. */
typedef struct MyimeUiLanguage MyimeUiLanguage;
uint32_t myime_ui_language_abi_version(void);
int32_t myime_ui_language_load(const char* user_root, const char* installed_root,
                               const char* fallback_id, MyimeUiLanguage** out);
int32_t myime_ui_language_text(MyimeUiLanguage*, const char* key, MyimeText* out);
int32_t myime_ui_language_destroy(MyimeUiLanguage*);
#ifdef __cplusplus
}
#endif
