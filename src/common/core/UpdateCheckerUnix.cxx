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

#include <core/UpdateChecker.h>

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace core
{
namespace detail
{

namespace
{

/* 对 shell 参数进行单引号转义 */
std::string shellQuote(const std::string& s)
{
  std::string out = "'";
  for (char c : s)
  {
    if (c == '\'')
      out += "'\\''";
    else
      out += c;
  }
  out += "'";
  return out;
}

} // anonymous namespace

bool httpGet(const std::string& url, std::string* response, std::string* error)
{
  std::string command =
      "curl -sSL --max-time 30 " + shellQuote(url) + " 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe)
  {
    if (error)
      *error = "无法执行 curl，请确认已安装 curl";
    return false;
  }

  response->clear();
  char buffer[4096];
  size_t read;
  while ((read = fread(buffer, 1, sizeof(buffer), pipe)) > 0)
    response->append(buffer, read);

  int status = pclose(pipe);
  if (status != 0)
  {
    if (error)
      *error = "curl 请求失败（退出码 " + std::to_string(status) + "）";
    return false;
  }
  return true;
}

} // namespace detail

bool UpdateChecker::downloadFile(const std::string& url,
                                 const std::string& destPath,
                                 std::string* error)
{
  std::string command = "curl -sSL --max-time 300 -o " +
                        detail::shellQuote(destPath) + " " +
                        detail::shellQuote(url) + " 2>/dev/null";
  int status = std::system(command.c_str());
  if (status != 0)
  {
    if (error)
      *error = "下载失败（退出码 " + std::to_string(status) + "）";
    return false;
  }
  return true;
}

bool UpdateChecker::openInBrowser(const std::string& url)
{
#if defined(__APPLE__)
  std::string command = "open " + detail::shellQuote(url);
#else
  std::string command =
      "xdg-open " + detail::shellQuote(url) + " >/dev/null 2>&1 &";
#endif
  return std::system(command.c_str()) == 0;
}

std::string UpdateChecker::tempDirectory()
{
  const char* tmp = std::getenv("TMPDIR");
  if (tmp && *tmp)
    return tmp;
  return "/tmp";
}

std::string UpdateChecker::executablePath()
{
#if defined(__linux__)
  char buffer[4096];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len > 0)
  {
    buffer[len] = '\0';
    return std::string(buffer);
  }
#endif
  return std::string();
}

std::string UpdateChecker::sha256OfFile(const std::string& path)
{
  std::string command =
      "sha256sum " + detail::shellQuote(path) + " 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe)
    return std::string();

  char buffer[128] = {};
  char* ok = fgets(buffer, sizeof(buffer), pipe);
  pclose(pipe);
  if (!ok)
    return std::string();

  std::string result(buffer);
  size_t pos = result.find_first_of(" \t\r\n");
  if (pos != std::string::npos)
    result = result.substr(0, pos);
  return result;
}

bool UpdateChecker::downloadAndInstall(const std::string& url,
                                       const std::string& checksumUrl,
                                       const std::string& version,
                                       std::string* error)
{
  /* Unix 平台安装通常需要 root 权限，无法完全静默。
   * 这里下载安装包到临时目录，并打开文件管理器供用户手动安装。 */
  std::string dest = tempDirectory() + "/TuPigVNC-" + version + ".tar.gz";
  if (!downloadFile(url, dest, error))
    return false;

  if (!checksumUrl.empty())
  {
    std::string checksumText;
    if (!detail::httpGet(checksumUrl, &checksumText, error))
      return false;

    std::string expected =
        checksumText.substr(0, checksumText.find_first_of(" \t\r\n"));
    std::string actual = sha256OfFile(dest);
    if (expected.empty() || actual != expected)
    {
      std::remove(dest.c_str());
      if (error)
        *error = "安装包校验失败（SHA-256 不匹配）";
      return false;
    }
  }

  openInBrowser("file://" + dest);
  return true;
}

} // namespace core

#endif // !_WIN32
