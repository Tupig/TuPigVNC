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

#ifndef __CORE_UPDATECHECKER_H__
#define __CORE_UPDATECHECKER_H__

#include <string>

namespace core {

/* 远程发行版信息 */
struct ReleaseInfo {
    std::string version;      /* 版本号，如 "0.0.2" */
    std::string name;         /* 发行版名称 */
    std::string notes;        /* 发行说明（Markdown） */
    std::string htmlUrl;      /* 发行版页面地址 */
    std::string downloadUrl;  /* 当前平台的安装包下载地址 */
    std::string checksumUrl;  /* 安装包 SHA-256 校验文件地址 */
    std::string publishedAt;  /* 发布日期（ISO 8601） */
    long long size = 0;       /* 安装包大小（字节） */
};

/* 自更新检查器。
 *
 * 通过 GitHub Releases API 查询最新版本，并与当前版本比较。
 * 网络操作均为阻塞式，调用方应在独立线程中执行以免阻塞界面。
 */
class UpdateChecker {
public:
    /* GitHub 仓库标识 */
    static const char* REPO_OWNER;
    static const char* REPO_NAME;
    static const char* API_URL;

    /* 查询最新发行版。
     *
     * 成功时填充 info 并返回 true；失败时填充 error 并返回 false。
     */
    static bool fetchLatestRelease(ReleaseInfo* info, std::string* error);

    /* 比较两个语义化版本号。
     *
     * 返回 -1 表示 a < b，0 表示相等，1 表示 a > b。
     * 支持 "v" 前缀与 "-prerelease" 后缀。
     */
    static int compareVersions(const std::string& a, const std::string& b);

    /* 判断当前版本是否有可用更新。 */
    static bool isNewer(const std::string& remote,
                        const std::string& current);

    /* 下载文件到指定路径。
     *
     * 成功返回 true；失败返回 false 并填充 error。
     */
    static bool downloadFile(const std::string& url,
                             const std::string& destPath,
                             std::string* error);

    /* 下载并静默安装更新，随后自动启动新版本。
     *
     * 全自动流程：下载安装包 → 校验 SHA-256 → 静默安装 → 启动新版本。
     * checksumUrl 为空时跳过校验。
     * 成功返回 true；失败返回 false 并填充 error。
     */
    static bool downloadAndInstall(const std::string& url,
                                   const std::string& checksumUrl,
                                   const std::string& version,
                                   std::string* error);

    /* 计算文件的 SHA-256 十六进制摘要。 */
    static std::string sha256OfFile(const std::string& path);

    /* 返回当前程序可执行文件的完整路径。 */
    static std::string executablePath();

    /* 返回系统临时目录。 */
    static std::string tempDirectory();

    /* 在系统文件管理器中打开指定路径或网址。 */
    static bool openInBrowser(const std::string& url);

    /* 当前平台对应的安装包文件名匹配关键字。
     * 例如 "win64"、"linux-x86_64"、"macos"。
     */
    static std::string platformKeyword();
};

} // namespace core

#endif // __CORE_UPDATECHECKER_H__
