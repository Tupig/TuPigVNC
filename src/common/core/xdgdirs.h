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

#ifndef CORE_XDGDIRS_H
#define CORE_XDGDIRS_H

#include <sys/stat.h>

namespace core {

  /*
   * 获取用户主目录。
   * 如果设置了 HOME 环境变量，则使用它。
   * 否则通过 getpwuid 函数获取主目录。
   *
   * 失败时返回 NULL。
   */
  const char* getuserhomedir();

  /*
   * 获取 VNC 配置目录。在类 Unix 系统上，为以下之一：
   * - $XDG_CONFIG_HOME/tigervnc
   * - $HOME/.config/tigervnc
   * 在 Windows 上，为安装目录下的 "User" 文件夹
   * （当该文件夹不可写时，回退到 %LOCALAPPDATA%/TuPigVNC）。
   *
   * 失败时返回 NULL。
   */
  const char* getvncconfigdir();

  /*
   * 获取用于 X.509 已知主机的 VNC 数据目录。
   * 在类 Unix 系统上，为以下之一：
   * - $XDG_DATA_HOME/tigervnc
   * - $HOME/.local/share/tigervnc
   * 在 Windows 上，为安装目录下的 "User" 文件夹
   * （当该文件夹不可写时，回退到 %LOCALAPPDATA%/TuPigVNC）。
   *
   * 失败时返回 NULL。
   */
  const char* getvncdatadir();

  /*
   * 获取 VNC 状态（日志）目录。在类 Unix 系统上，为以下之一：
   * - $XDG_STATE_HOME/tigervnc
   * - $HOME/.local/state/tigervnc
   * 在 Windows 上，为安装目录下的 "User" 文件夹
   * （当该文件夹不可写时，回退到 %LOCALAPPDATA%/TuPigVNC）。
   *
   * 失败时返回 NULL。
   */
  const char* getvncstatedir();

  /*
   * 获取 VNC 安装根目录（包含可执行文件的文件夹）。
   * 用于滚动日志目录。在 Windows 上为 exe 所在文件夹；
   * 其他平台返回 NULL。
   *
   * 失败时返回 NULL。
   */
  const char* getvncinstallroot();

  /*
   * 递归创建目录。便于创建上述目录所需的嵌套目录结构。
   */
  int mkdir_p(const char *path, mode_t mode);
}

#endif /* CORE_XDGDIRS_H */
