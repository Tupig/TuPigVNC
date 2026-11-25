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

#ifndef _WIN32

#include <core/Platform.h>

#include <cstdlib>
#include <string>
#include <thread>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace core {

namespace {

/* 对 shell 参数做单引号转义 */
std::string shellQuote(const std::string& s)
{
    std::string out = "'";
    for (char c : s) {
        if (c == '\'')
            out += "'\\''";
        else
            out += c;
    }
    out += "'";
    return out;
}

class PlatformUnix final : public Platform {
public:
    std::string executablePath() const override
    {
#if defined(__linux__)
        char buffer[4096];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0';
            return std::string(buffer);
        }
#endif
        return std::string();
    }

    std::string tempDirectory() const override
    {
        const char* tmp = std::getenv("TMPDIR");
        if (tmp != nullptr && *tmp != '\0')
            return tmp;
        return "/tmp";
    }

    bool openInBrowser(const std::string& url) const override
    {
#if defined(__APPLE__)
        std::string command = "open " + shellQuote(url) +
                              " >/dev/null 2>&1 &";
#else
        std::string command = "xdg-open " + shellQuote(url) +
                              " >/dev/null 2>&1 &";
#endif
        return std::system(command.c_str()) == 0;
    }

    unsigned int cpuCount() const override
    {
        unsigned int count = std::thread::hardware_concurrency();
        return count == 0 ? 1 : count;
    }

    const char* name() const override
    {
#if defined(__APPLE__)
        return "macos";
#else
        return "linux";
#endif
    }
};

} // anonymous namespace

Platform& getPlatform()
{
    static PlatformUnix instance;
    return instance;
}

} // namespace core

#endif // !_WIN32
