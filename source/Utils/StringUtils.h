#pragma once

#include <windows.h>

#include <string>
#include <vector>
#include <algorithm>
#include <sstream>

namespace Str
{
    //--------------------------------------------------------------
    // ToNarrow — wstring → string (UTF-8)
    //--------------------------------------------------------------
    inline std::string ToNarrow(const std::wstring& wide)
    {
        if (wide.empty())
            return {};

        int size = WideCharToMultiByte(
            CP_UTF8, 0,
            wide.c_str(), static_cast<int>(wide.size()),
            nullptr, 0,
            nullptr, nullptr
        );

        std::string out(size, 0);
        WideCharToMultiByte(
            CP_UTF8, 0,
            wide.c_str(), static_cast<int>(wide.size()),
            out.data(), size,
            nullptr, nullptr
        );

        return out;
    }

    //--------------------------------------------------------------
    // ToWide — string → wstring (UTF-8)
    //--------------------------------------------------------------
    inline std::wstring ToWide(const std::string& narrow)
    {
        if (narrow.empty())
            return {};

        int size = MultiByteToWideChar(
            CP_UTF8, 0,
            narrow.c_str(), static_cast<int>(narrow.size()),
            nullptr, 0
        );

        std::wstring out(size, 0);
        MultiByteToWideChar(
            CP_UTF8, 0,
            narrow.c_str(), static_cast<int>(narrow.size()),
            out.data(), size
        );

        return out;
    }

    //--------------------------------------------------------------
    // ToUpper — in-place uppercase (ASCII)
    //--------------------------------------------------------------
    inline std::string ToUpper(const std::string& str)
    {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(), ::toupper);
        return out;
    }

    inline std::wstring ToUpper(const std::wstring& str)
    {
        std::wstring out = str;
        std::transform(out.begin(), out.end(), out.begin(), ::towupper);
        return out;
    }

    //--------------------------------------------------------------
    // ToLower
    //--------------------------------------------------------------
    inline std::string ToLower(const std::string& str)
    {
        std::string out = str;
        std::transform(out.begin(), out.end(), out.begin(), ::tolower);
        return out;
    }

    inline std::wstring ToLower(const std::wstring& str)
    {
        std::wstring out = str;
        std::transform(out.begin(), out.end(), out.begin(), ::towlower);
        return out;
    }

    //--------------------------------------------------------------
    // Trim — strips leading and trailing whitespace
    //--------------------------------------------------------------
    inline std::string Trim(const std::string& str)
    {
        size_t start = str.find_first_not_of(" \t\r\n");
        size_t end = str.find_last_not_of(" \t\r\n");

        return (start == std::string::npos)
            ? std::string{}
        : str.substr(start, end - start + 1);
    }

    inline std::wstring Trim(const std::wstring& str)
    {
        size_t start = str.find_first_not_of(L" \t\r\n");
        size_t end = str.find_last_not_of(L" \t\r\n");

        return (start == std::wstring::npos)
            ? std::wstring{}
        : str.substr(start, end - start + 1);
    }

    //--------------------------------------------------------------
    // Split — splits string by delimiter into a vector
    //--------------------------------------------------------------
    inline std::vector<std::string> Split(const std::string& str, char delim)
    {
        std::vector<std::string> out;
        std::istringstream ss(str);
        std::string token;

        while (std::getline(ss, token, delim))
        {
            token = Trim(token);
            if (!token.empty())
                out.push_back(token);
        }

        return out;
    }

    inline std::vector<std::wstring> Split(const std::wstring& str, wchar_t delim)
    {
        std::vector<std::wstring> out;
        std::wistringstream ss(str);
        std::wstring token;

        while (std::getline(ss, token, delim))
        {
            token = Trim(token);
            if (!token.empty())
                out.push_back(token);
        }

        return out;
    }

    //--------------------------------------------------------------
    // Contains — case-sensitive substring check
    //--------------------------------------------------------------
    inline bool Contains(const std::string& str, const std::string& sub)
    {
        return str.find(sub) != std::string::npos;
    }

    inline bool Contains(const std::wstring& str, const std::wstring& sub)
    {
        return str.find(sub) != std::wstring::npos;
    }

    //--------------------------------------------------------------
    // StartsWith / EndsWith
    //--------------------------------------------------------------
    inline bool StartsWith(const std::wstring& str, const std::wstring& prefix)
    {
        return str.size() >= prefix.size() &&
            str.compare(0, prefix.size(), prefix) == 0;
    }

    inline bool EndsWith(const std::wstring& str, const std::wstring& suffix)
    {
        return str.size() >= suffix.size() &&
            str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    //--------------------------------------------------------------
    // IsEmpty — trims then checks
    //--------------------------------------------------------------
    inline bool IsEmpty(const std::wstring& str)
    {
        return Trim(str).empty();
    }

    inline bool IsEmpty(const std::string& str)
    {
        return Trim(str).empty();
    }
}