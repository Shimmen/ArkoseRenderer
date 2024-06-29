#pragma once

#include "core/Types.h"

// Core D3D12 API
#include <d3d12.h>

// For COM interfacing
#include <wrl.h>
using namespace Microsoft::WRL;

// For various string conversions, as Windows likes utf16
#include <Windows.h>

inline std::wstring convertToWideString(char const* utf8string)
{
    size_t utf8Length = std::strlen(utf8string);
    size_t requiredLength = ::MultiByteToWideChar(CP_UTF8, 0, utf8string, narrow_cast<int>(utf8Length), nullptr, 0);

    if (requiredLength == 0) {
        return std::wstring();
    }

    std::wstring wideString(requiredLength, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8string, narrow_cast<int>(utf8Length), wideString.data(), narrow_cast<int>(wideString.length()));

    return wideString;
}

inline std::wstring convertToWideString(std::string_view utf8string)
{
    return convertToWideString(utf8string.data());
}

inline std::wstring convertToWideString(std::string const& utf8string)
{
    return convertToWideString(utf8string.data());
}

inline std::string convertFromWideString(wchar_t* utf16string)
{
    size_t utf16Length = std::wcslen(utf16string);
    size_t requiredLength = ::WideCharToMultiByte(CP_UTF8, 0, utf16string, narrow_cast<int>(utf16Length), nullptr, 0, nullptr, nullptr);

    if (requiredLength == 0) {
        return std::string();
    }

    std::string utf8string(requiredLength, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, utf16string, narrow_cast<int>(utf16Length), utf8string.data(), narrow_cast<int>(utf8string.size()), nullptr, nullptr);

    return utf8string;
}

inline std::string convertFromWideString(std::wstring utf16string)
{
    return convertFromWideString(utf16string.data());
}
