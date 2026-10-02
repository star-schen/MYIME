#pragma once
#include <myime/theme.h>
#include <string>
// Owned Windows presentation data. Never retain Rust borrowed pointers.
struct CandidateTheme {
    std::wstring id,name,version,description,font_family,warning;
    MyimeThemeView style{}; // Numeric fields only; string pointers cleared.
};
