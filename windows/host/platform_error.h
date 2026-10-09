#pragma once
#include <windows.h>
#include <stdexcept>
#include <system_error>

// Preserve native HRESULTs across C++ cleanup and the TSF COM boundary.
class PlatformError : public std::runtime_error {
    HRESULT result_;
public:
    PlatformError(const char* operation,HRESULT result) : std::runtime_error(operation),result_(FAILED(result)?result:E_FAIL) {}
    HRESULT result() const noexcept { return result_; }
};
inline HRESULT exception_result(const std::exception& error) noexcept {
    if (const auto native=dynamic_cast<const PlatformError*>(&error)) return native->result();
    if (const auto system=dynamic_cast<const std::system_error*>(&error)) {
        const auto value=system->code().value();
        if (value>0) return HRESULT_FROM_WIN32(value);
    }
    return E_FAIL;
}
