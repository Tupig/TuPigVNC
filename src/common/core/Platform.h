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

#ifndef __CORE_PLATFORM_H__
#define __CORE_PLATFORM_H__

#include <string>

namespace core {

/*
 * 平台服务的统一抽象。
 *
 * 把散落在各处 #ifdef 中的系统调用集中到本接口，
 * 各平台分别在 PlatformWin32.cxx / PlatformUnix.cxx 中实现。
 * 调用方通过 getPlatform() 获取当前平台实例，无需条件编译。
 */
class Platform {
public:
    virtual ~Platform() = default;

    /* 当前可执行文件的完整路径。失败时返回空字符串。 */
    virtual std::string executablePath() const = 0;

    /* 系统临时目录（无末尾分隔符）。 */
    virtual std::string tempDirectory() const = 0;

    /* 在系统文件管理器中打开网址或路径。 */
    virtual bool openInBrowser(const std::string& url) const = 0;

    /* 本机 CPU 核心数。 */
    virtual unsigned int cpuCount() const = 0;

    /* 平台名称，如 "windows"、"linux"、"macos"。 */
    virtual const char* name() const = 0;
};

/* 返回当前平台的单例。 */
Platform& getPlatform();

#ifdef _WIN32
/*
 * Windows 专用：UTF-8 与 UTF-16 之间的转换。
 *
 * Win32 API 全部使用宽字符，而项目内部统一使用 UTF-8 的
 * std::string。这两个函数是两者之间唯一且共享的转换入口，
 * 供 Platform 实现与 UpdateChecker 等 Windows 专用代码复用。
 */
std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
#endif

} // namespace core

#endif // __CORE_PLATFORM_H__
