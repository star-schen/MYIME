#pragma once
#include <myime/core.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Presentation ABI 1. No Rime session required. No C++/Rust object layout.
 * Handles are thread-affine; views are borrowed until theme_destroy.
 * Dimensions use DIPs (96 DIPs/inch), colors are 0xRRGGBB, flags are 0/1.
 * Resolve returns embedded default + warning for a missing/invalid package.
 * Root directories are supplied by the platform; selectors are ids, not paths.
 */
typedef struct MyimeTheme MyimeTheme;
typedef struct {
    MyimeText id, name, version, description, font_family, warning;
    uint32_t font_size, font_weight, horizontal;
    uint32_t padding_x, padding_y, candidate_padding_x, candidate_padding_y;
    uint32_t spacing, border_width, border_radius, min_width, max_width, caret_gap;
    uint32_t background, text, muted, label, highlight, highlight_text, border;
    uint32_t shadow, shadow_size, shadow_opacity;
    uint32_t preedit, comments, labels, page_controls;
} MyimeThemeView;
uint32_t myime_theme_abi_version(void);
int32_t myime_theme_resolve(const char* id, const char* user_root,
                           const char* installed_root, MyimeTheme** out);
int32_t myime_theme_view(MyimeTheme*, size_t out_size, MyimeThemeView*);
int32_t myime_theme_destroy(MyimeTheme*);
#ifdef __cplusplus
}
#endif
