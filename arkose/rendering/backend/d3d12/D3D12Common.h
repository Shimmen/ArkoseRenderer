#pragma once

// Core D3D12 API
#include <d3d12.h>

// D3D12 "helper" library
#include <d3dx12/d3dx12.h>

// For COM interfacing
#include <wrl.h>
using namespace Microsoft::WRL;

// For various string conversions, as Windows likes utf16
#include <locale>
#include <codecvt>
#include <string>

inline std::wstring convertToWideString(char const* utf8string)
{
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(utf8string);
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
    static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.to_bytes(utf16string);
}

inline std::string convertFromWideString(std::wstring utf16string)
{
    return convertFromWideString(utf16string.data());
}
