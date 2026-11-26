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

#include <core/UpdateChecker.h>
#include <core/Platform.h>

#include <windows.h>
#include <bcrypt.h>
#include <shellapi.h>
#include <winhttp.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace core
{
namespace detail
{

/* URL 分解结果 */
struct UrlParts
{
  std::wstring scheme;
  std::wstring host;
  std::wstring path;
  INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
  bool https = true;
};

bool parseUrl(const std::string& url, UrlParts* parts)
{
  std::wstring wurl = widen(url);
  URL_COMPONENTS uc;
  ZeroMemory(&uc, sizeof(uc));
  uc.dwStructSize = sizeof(uc);

  wchar_t host[256] = {};
  wchar_t path[2048] = {};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 255;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2047;

  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
    return false;

  parts->host = host;
  parts->path = path;
  parts->port = uc.nPort;
  parts->https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
  return true;
}

bool httpGet(const std::string& url, std::string* response, std::string* error)
{
  UrlParts parts;
  if (!parseUrl(url, &parts))
  {
    if (error)
      *error = "无效的网址";
    return false;
  }

  HINTERNET session =
      WinHttpOpen(L"TuPigVNC-Updater/0.0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!session)
  {
    if (error)
      *error = "无法初始化 WinHTTP 会话";
    return false;
  }

  // 默认超时长达 30 秒，被墙时会让调用方（尤其是关窗时的析构）卡死。
  // 连接/发送/接收各 3 秒，整体 5 秒，超时即失败。
  WinHttpSetTimeouts(session, 3000, 3000, 3000, 5000);

  HINTERNET connection =
      WinHttpConnect(session, parts.host.c_str(), parts.port, 0);
  if (!connection)
  {
    WinHttpCloseHandle(session);
    if (error)
      *error = "无法连接到服务器";
    return false;
  }

  HINTERNET request = WinHttpOpenRequest(
      connection, L"GET", parts.path.c_str(), nullptr, WINHTTP_NO_REFERER,
      WINHTTP_DEFAULT_ACCEPT_TYPES, parts.https ? WINHTTP_FLAG_SECURE : 0);
  if (!request)
  {
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (error)
      *error = "无法创建 HTTP 请求";
    return false;
  }

  /* GitHub API 要求提供 User-Agent */
  WinHttpAddRequestHeaders(request,
                           L"User-Agent: TuPigVNC-Updater\r\nAccept: "
                           L"application/vnd.github+json\r\n",
                           static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);

  /* 忽略证书错误在此处不处理，保持默认校验 */

  if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                          WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
      !WinHttpReceiveResponse(request, nullptr))
  {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (error)
      *error = "HTTP 请求失败";
    return false;
  }

  DWORD status = 0;
  DWORD statusSize = sizeof(status);
  WinHttpQueryHeaders(request,
                      WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                      WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                      WINHTTP_NO_HEADER_INDEX);
  if (status < 200 || status >= 300)
  {
    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    if (error)
      *error = "服务器返回状态码 " + std::to_string(status);
    return false;
  }

  response->clear();

  /*
   * 读取循环必须区分「正常结束」与「读取失败」。
   * 原实现失败时只 break，循环后无条件 return true，会把截断的
   * 响应当成完整内容写盘；若发布没有 .sha256 资产，残缺的安装包
   * 会被直接执行。
   */
  bool failed = false;
  std::string failReason;
  for (;;)
  {
    DWORD available = 0;
    if (!WinHttpQueryDataAvailable(request, &available))
    {
      failed = true;
      failReason = "查询可用数据失败（错误码 " +
                   std::to_string(GetLastError()) + "）";
      break;
    }
    if (available == 0)
      break; /* 正常结束 */
    std::string buffer(available, '\0');
    DWORD read = 0;
    if (!WinHttpReadData(request, &buffer[0], available, &read))
    {
      failed = true;
      failReason = "读取响应数据失败（错误码 " +
                   std::to_string(GetLastError()) + "）";
      break;
    }
    response->append(buffer.data(), read);
  }

  WinHttpCloseHandle(request);
  WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);

  if (failed)
  {
    response->clear(); /* 丢弃残缺数据，避免被当成完整下载 */
    if (error)
      *error = "下载中断：" + failReason;
    return false;
  }
  return true;
}

} // namespace detail

bool UpdateChecker::downloadFile(const std::string& url,
                                 const std::string& destPath,
                                 std::string* error)
{
  std::string content;
  if (!detail::httpGet(url, &content, error))
    return false;

  FILE* file = _wfopen(widen(destPath).c_str(), L"wb");
  if (!file)
  {
    if (error)
      *error = "无法写入文件：" + destPath;
    return false;
  }
  size_t written = fwrite(content.data(), 1, content.size(), file);
  fclose(file);
  if (written != content.size())
  {
    if (error)
      *error = "写入文件时发生错误";
    return false;
  }
  return true;
}

bool UpdateChecker::openInBrowser(const std::string& url)
{
  std::wstring wurl = widen(url);
  HINSTANCE result = ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr,
                                   nullptr, SW_SHOWNORMAL);
  return reinterpret_cast<INT_PTR>(result) > 32;
}

std::string UpdateChecker::tempDirectory()
{
  wchar_t path[MAX_PATH];
  DWORD len = GetTempPathW(MAX_PATH, path);
  if (len == 0 || len >= MAX_PATH)
    return ".";
  std::wstring wdir(path);
  while (!wdir.empty() && (wdir.back() == L'\\' || wdir.back() == L'/'))
    wdir.pop_back();
  return narrow(wdir);
}

std::string UpdateChecker::executablePath()
{
  /* 与 Platform 的实现保持一致，直接复用，避免两份各自维护。 */
  return getPlatform().executablePath();
}

std::string UpdateChecker::sha256OfFile(const std::string& path)
{
  std::wstring wpath = widen(path);
  FILE* file = _wfopen(wpath.c_str(), L"rb");
  if (!file)
    return std::string();

  std::string result;
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) ==
      0)
  {
    DWORD objLen = 0;
    DWORD cbResult = 0;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen),
                          &cbResult, 0) == 0)
    {
      std::vector<unsigned char> obj(objLen);
      BCRYPT_HASH_HANDLE hash = nullptr;
      if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) == 0)
      {
        unsigned char buffer[65536];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), file)) > 0)
          BCryptHashData(hash, buffer, static_cast<ULONG>(read), 0);

        unsigned char digest[32];
        if (BCryptFinishHash(hash, digest, sizeof(digest), 0) == 0)
        {
          char hex[65];
          for (int i = 0; i < 32; ++i)
            sprintf(hex + i * 2, "%02x", digest[i]);
          hex[64] = '\0';
          result = hex;
        }
        BCryptDestroyHash(hash);
      }
    }
    BCryptCloseAlgorithmProvider(alg, 0);
  }

  fclose(file);
  return result;
}

namespace
{

/* 版本号只允许字母、数字、点、下划线和短横线，防止路径穿越与命令注入 */
bool isSafeVersionString(const std::string& version)
{
  if (version.empty())
    return false;
  for (char c : version)
  {
    bool safe = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') || c == '.' || c == '_' || c == '-';
    if (!safe)
      return false;
  }
  return true;
}

/* 下载地址只允许 GitHub 官方的 HTTPS 域名，防止被引导到恶意站点 */
bool isTrustedDownloadUrl(const std::string& url)
{
  static const wchar_t* const kTrustedHosts[] = {
      L"github.com", L"objects.githubusercontent.com", L"api.github.com",
      L"github-releases.githubusercontent.com"};
  detail::UrlParts parts;
  if (!detail::parseUrl(url, &parts) || !parts.https)
    return false;
  for (const wchar_t* host : kTrustedHosts)
  {
    if (_wcsicmp(parts.host.c_str(), host) == 0)
      return true;
  }
  return false;
}

} // anonymous namespace

bool UpdateChecker::downloadAndInstall(const std::string& url,
                                       const std::string& checksumUrl,
                                       const std::string& version,
                                       std::string* error)
{
  /* 1. 下载与执行前先做安全校验，任何一项不通过都直接拒绝 */
  if (!isTrustedDownloadUrl(url))
  {
    if (error)
      *error = "下载地址不是可信的 GitHub 官方域名，已取消更新";
    return false;
  }
  if (!isSafeVersionString(version))
  {
    if (error)
      *error = "版本号包含非法字符，已取消更新";
    return false;
  }
  if (checksumUrl.empty())
  {
    if (error)
      *error = "该版本未提供 SHA-256 校验文件，为安全起见已取消自动安装，"
               "请前往发布页手动下载";
    return false;
  }
  if (!isTrustedDownloadUrl(checksumUrl))
  {
    if (error)
      *error = "校验文件地址不是可信的 GitHub 官方域名，已取消更新";
    return false;
  }

  std::string tempDir = tempDirectory();
  std::string installer = tempDir + "\\TuPigVNC-" + version + "-setup.exe";

  /* 2. 下载安装包 */
  if (!downloadFile(url, installer, error))
    return false;

  /* 3. 下载校验文件并比对 SHA-256 */
  std::string checksumText;
  if (!detail::httpGet(checksumUrl, &checksumText, error))
  {
    DeleteFileW(widen(installer).c_str());
    return false;
  }

  /* 校验文件格式：<hash>  <filename> */
  std::string expected;
  size_t pos = checksumText.find_first_of(" \t\r\n");
  expected = checksumText.substr(0, pos);
  while (!expected.empty() && expected.back() == ' ')
    expected.pop_back();

  std::string actual = sha256OfFile(installer);
  if (expected.empty() || actual != expected)
  {
    DeleteFileW(widen(installer).c_str());
    if (error)
      *error = "安装包校验失败（SHA-256 不匹配）";
    return false;
  }

  std::string exePath = executablePath();
  if (exePath.empty())
  {
    if (error)
      *error = "无法确定当前程序路径";
    return false;
  }

  /* 4. 生成更新脚本：等待本进程退出 → 静默安装 → 启动新版本 → 自删除 */
  std::string batchPath = tempDir + "\\TuPigVNC-update.cmd";
  std::string script;
  script += "@echo off\r\n";
  /* 脚本以 UTF-8 写入，先切到 65001 代码页，中文路径才不会被 cmd 打乱 */
  script += "chcp 65001 >nul\r\n";
  script += "ping 127.0.0.1 -n 3 >nul\r\n";
  script +=
      "\"" + installer + "\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP-\r\n";
  script += "start \"\" \"" + exePath + "\"\r\n";
  script += "timeout /t 2 /nobreak >nul\r\n";
  script += "del /f /q \"" + installer + "\" 2>nul\r\n";
  script += "del /f /q \"%~f0\" 2>nul\r\n";

  FILE* file = _wfopen(widen(batchPath).c_str(), L"wb");
  if (!file)
  {
    if (error)
      *error = "无法创建更新脚本";
    return false;
  }
  fwrite(script.data(), 1, script.size(), file);
  fclose(file);

  /* 5. 以隐藏窗口启动更新脚本 */
  std::wstring wbatch = widen(batchPath);
  SHELLEXECUTEINFOW sei = {};
  sei.cbSize = sizeof(sei);
  sei.fMask = SEE_MASK_NOASYNC;
  sei.lpVerb = L"open";
  sei.lpFile = wbatch.c_str();
  sei.nShow = SW_HIDE;
  if (!ShellExecuteExW(&sei))
  {
    if (error)
      *error = "无法启动更新程序";
    return false;
  }
  return true;
}

} // namespace core

#endif // _WIN32
