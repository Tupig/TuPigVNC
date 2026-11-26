/* Copyright (C) 2010 TightVNC Team.  All Rights Reserved.
 * Copyright 2021-2023 Pierre Ossman for Cendio AB
 *
 * 本软件为自由软件；你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （GNU General Public License）的条款（第 2 版，或你选择的任何更高版本）
 * 重新分发和/或修改本软件。
 *
 * 本软件的发布是希望它有用，但不附带任何担保；甚至不附带适销性或
 * 特定用途适用性的默示担保。详见 GNU 通用公共许可证。
 *
 * 你应已收到随本软件一同提供的 GNU 通用公共许可证副本；如果没有，
 * 请致信自由软件基金会：Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA。
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifndef WIN32
#include <pwd.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#else
#include <windows.h>
#include <wininet.h> /* MinGW 需要它 */
#include <shlobj.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <string>
#include <filesystem>
#include <system_error>
#define stat _stat
#define mkdir(path, mode) mkdir(path)
#endif

#include <core/xdgdirs.h>
#include <core/Platform.h>

static const char* getvncdir(bool userDir, const char *xdg_env, const char *xdg_def)
{
  static char dir[PATH_MAX], legacy[PATH_MAX];

#ifndef WIN32
  struct stat st;
  char *homedir, *xdgdir;
  uid_t uid;
  struct passwd *passwd;

  homedir = getenv("HOME");
  if (homedir == nullptr) {
    uid = getuid();
    passwd = getpwuid(uid);
    if (passwd == nullptr) {
      /* 这里要不要输出错误信息？ */
      return nullptr;
    }
    homedir = passwd->pw_dir;
  }

  if (userDir)
    return homedir;

  xdgdir = getenv(xdg_env);
  if (xdgdir != nullptr && xdgdir[0] == '/')
    snprintf(dir, sizeof(dir), "%s/tigervnc", xdgdir);
  else
    snprintf(dir, sizeof(dir), "%s/%s/tigervnc", homedir, xdg_def);

  snprintf(legacy, sizeof(legacy), "%s/.vnc", homedir);

  return (stat(dir, &st) != 0 && stat(legacy, &st) == 0) ? legacy : dir;
#else
  (void) xdg_def;
  (void) xdg_env;

  /* 用户主目录（~）：保持不变 */
  if (userDir) {
    if (SHGetSpecialFolderPath(nullptr, dir, CSIDL_PROFILE, FALSE) == FALSE)
      return nullptr;
    return dir;
  }

  /*
   * TuPigVNC：所有配置（选项、连接历史、日志、证书等）统一存到
   * 「安装目录\User」下，方便绿色迁移和备份。
   *
   * 如果安装目录不可写（例如装在 Program Files 但当前用户没有
   * 管理员权限），则回退到 %LOCALAPPDATA%\TuPigVNC，保证程序仍
   * 能正常保存配置。
   */
  {
    wchar_t exePathW[PATH_MAX];
    DWORD n = GetModuleFileNameW(nullptr, exePathW, PATH_MAX);
    if (n > 0 && n < PATH_MAX) {
      /* 去掉文件名，保留 exe 所在目录（含末尾分隔符） */
      wchar_t* slash = wcsrchr(exePathW, L'\\');
      if (slash == nullptr)
        slash = wcsrchr(exePathW, L'/');
      if (slash != nullptr) {
        *(slash + 1) = L'\0';
        std::string userDirUtf8 =
          core::narrow(std::wstring(exePathW) + L"User");
        snprintf(dir, sizeof(dir), "%s", userDirUtf8.c_str());
        core::mkdir_p(dir, 0755);

        /* 试写探测文件，确认目录真的可写 */
        char probe[PATH_MAX + 32];
        snprintf(probe, sizeof(probe), "%s\\.write_probe", dir);
        FILE* fp = fopen(probe, "w");
        if (fp != nullptr) {
          fclose(fp);
          remove(probe);
          return dir;
        }
      }
    }
  }

  /* 回退：%LOCALAPPDATA%\TuPigVNC（当前用户必然可写） */
  if (SHGetSpecialFolderPath(nullptr, dir, CSIDL_LOCAL_APPDATA, FALSE) == FALSE)
    return nullptr;
  if (strlen(dir) + strlen("\\TuPigVNC") >= sizeof(dir))
    return nullptr;
  strcat(dir, "\\TuPigVNC");
  return dir;
#endif
}

const char* core::getuserhomedir()
{
  return getvncdir(true, nullptr, nullptr);
}

const char* core::getvncconfigdir()
{
  return getvncdir(false, "XDG_CONFIG_HOME", ".config");
}

const char* core::getvncdatadir()
{
  return getvncdir(false, "XDG_DATA_HOME", ".local/share");
}

const char* core::getvncstatedir()
{
  return getvncdir(false, "XDG_STATE_HOME", ".local/state");
}

const char* core::getvncinstallroot()
{
#ifdef WIN32
  static char dir[PATH_MAX];
  wchar_t exePathW[PATH_MAX];
  DWORD n = GetModuleFileNameW(nullptr, exePathW, PATH_MAX);
  if (n == 0 || n >= PATH_MAX)
    return nullptr;
  wchar_t* slash = wcsrchr(exePathW, L'\\');
  if (slash == nullptr)
    slash = wcsrchr(exePathW, L'/');
  if (slash == nullptr)
    return nullptr;
  *slash = L'\0';
  std::string utf8 = core::narrow(std::wstring(exePathW));
  snprintf(dir, sizeof(dir), "%s", utf8.c_str());
  return dir;
#else
  return nullptr;
#endif
}

int core::mkdir_p(const char *path_, mode_t mode)
{
#ifdef WIN32
  /*
   * Windows 上路径分隔符既可能是 '\' 也可能是 '/'，还可能是盘符
   * 或 UNC 前缀，手写的逐级 mkdir 很容易漏判。改用标准库处理，
   * 这些情况全部由它负责。
   *
   * 路径按 UTF-8 处理，先转成宽字符再交给文件系统，否则用户名等
   * 含非 ASCII 字符时会被当成 ANSI 代码页而乱码。
   */
  (void)mode;

  std::error_code ec;
  std::filesystem::create_directories(core::widen(path_), ec);
  return ec ? -1 : 0;
#else
  char *path = strdup(path_);
  char *p;

  /* POSIX 路径必然以 '/' 分隔，逐级创建以应用正确的权限位。 */
  for (p = path + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      if (mkdir(path, mode) == -1) {
        if (errno != EEXIST) {
          free(path);
          return -1;
        }
      }
      *p = '/';
    }
  }

  if (mkdir(path, mode) == -1) {
    free(path);
    return -1;
  }

  free(path);

  return 0;
#endif
}
