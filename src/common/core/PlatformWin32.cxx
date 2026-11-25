/* Copyright (C) 2026 TuPigVNC Team
 *
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
 */

#ifdef _WIN32

#include <core/Platform.h>

#include <windows.h>
#include <shlobj.h>

#include <string>

namespace core {

std::wstring widen(const std::string& s)
{
    if (s.empty())
        return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &out[0], len);
    return out;
}

std::string narrow(const std::wstring& s)
{
    if (s.empty())
        return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, nullptr, 0,
                                  nullptr, nullptr);
    std::string out(len > 0 ? len - 1 : 0, '\0');
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), -1, &out[0], len,
                            nullptr, nullptr);
    return out;
}

namespace {

class PlatformWin32 final : public Platform {
public:
    std::string executablePath() const override
    {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        if (len == 0 || len >= MAX_PATH)
            return std::string();
        return narrow(std::wstring(buffer, len));
    }

    std::string tempDirectory() const override
    {
        wchar_t buffer[MAX_PATH];
        DWORD len = GetTempPathW(MAX_PATH, buffer);
        if (len == 0 || len >= MAX_PATH)
            return ".";
        std::wstring dir(buffer, len);
        while (!dir.empty() && (dir.back() == L'\\' || dir.back() == L'/'))
            dir.pop_back();
        return narrow(dir);
    }

    bool openInBrowser(const std::string& url) const override
    {
        std::wstring wurl = widen(url);
        HINSTANCE result = ShellExecuteW(nullptr, L"open", wurl.c_str(),
                                         nullptr, nullptr, SW_SHOWNORMAL);
        return reinterpret_cast<INT_PTR>(result) > 32;
    }

    unsigned int cpuCount() const override
    {
        SYSTEM_INFO info;
        GetSystemInfo(&info);
        return info.dwNumberOfProcessors;
    }

    const char* name() const override { return "windows"; }
};

} // anonymous namespace

Platform& getPlatform()
{
    static PlatformWin32 instance;
    return instance;
}

} // namespace core

#endif // _WIN32
