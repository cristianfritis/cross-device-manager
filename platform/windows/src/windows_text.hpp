#pragma once
#include <windows.h>

#include <cstddef>
#include <string>

// Internal to platform/windows — includes <windows.h> and is NOT part of the
// public devmgr/ surface, the same rule the Linux backend's internal headers
// follow.
namespace devmgr::platform_windows {

// Windows speaks UTF-16 and the rest of this program speaks UTF-8. Conversion
// happens here, at the boundary, so no wide string reaches the shared model.
inline std::string toUtf8(const wchar_t* text, int lengthChars) {
    if (text == nullptr || lengthChars == 0) return {};
    const int bytes =
        ::WideCharToMultiByte(CP_UTF8, 0, text, lengthChars, nullptr, 0, nullptr, nullptr);
    if (bytes <= 0) return {};
    std::string out(static_cast<std::size_t>(bytes), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, lengthChars, out.data(), bytes, nullptr, nullptr);
    return out;
}

inline std::string toUtf8(const std::wstring& text) {
    return toUtf8(text.c_str(), static_cast<int>(text.size()));
}

inline std::wstring toWide(const std::string& text) {
    if (text.empty()) return {};
    const int chars =
        ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (chars <= 0) return {};
    std::wstring out(static_cast<std::size_t>(chars), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(),
                          chars);
    return out;
}

}  // namespace devmgr::platform_windows
