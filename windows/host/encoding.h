#pragma once
#include <windows.h>
#include <myime/core.h>
#include <string>
#include <stdexcept>
#include <limits>
inline std::wstring wide(MyimeText text) {
    if (!text.len) return {};
    if (!text.data || text.len>static_cast<size_t>((std::numeric_limits<int>::max)())) throw std::runtime_error("Invalid UTF-8 view");
    const auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data,static_cast<int>(text.len),nullptr,0);
    if (!count) throw std::runtime_error("Invalid UTF-8 from core");
    std::wstring result(count,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data,static_cast<int>(text.len),result.data(),count);
    return result;
}
inline std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    if (text.size()>static_cast<size_t>((std::numeric_limits<int>::max)())) throw std::runtime_error("UTF-16 text too large");
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if (!count) throw std::runtime_error("Invalid UTF-16 text");
    std::string result(count,'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count,nullptr,nullptr);
    return result;
}
