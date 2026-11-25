# TigerVNC 上游 Issue 分类报告

> 数据来源：https://github.com/TigerVNC/tigervnc/issues（open 状态）
> 生成时间：2026-11-24
> Issue 总数：175

## 一、总览

### 按类型

| 类型 | 数量 | 占比 |
|------|-----:|-----:|
| 功能需求 (enhancement) | 105 | 60.0% |
| 缺陷 (bug) | 50 | 28.6% |
| 未分类 | 15 | 8.6% |
| 非本项缺陷 (notourbug) | 5 | 2.9% |

### 按模块

| 模块 | 数量 |
|------|-----:|
| 其他 | 44 |
| Windows 客户端 (winvnc) | 27 |
| 查看器 (vncviewer) | 18 |
| 网络与连接 | 17 |
| X11 服务器 (Xvnc) | 15 |
| Wayland (w0vncserver) | 13 |
| 键盘与鼠标 | 11 |
| 多显示器 | 8 |
| 安全与认证 | 8 |
| macOS 客户端 | 7 |
| 剪贴板 | 3 |
| 编码 (Tight/JPEG/ZRLE) | 3 |
| 音频 | 1 |

### 按创建年份

| 年份 | 数量 |
|------|-----:|
| 2014 | 2 |
| 2015 | 12 |
| 2016 | 15 |
| 2017 | 12 |
| 2018 | 12 |
| 2019 | 12 |
| 2020 | 14 |
| 2021 | 21 |
| 2022 | 5 |
| 2023 | 15 |
| 2024 | 17 |
| 2025 | 19 |
| 2026 | 19 |

## 二、按模块详细清单

### 其他（44 个）

- [#64](https://github.com/TigerVNC/tigervnc/issues/64) Allow Alt_R to pop up the menu
  - 类型：enhancement ｜ 创建：2014-11-08 ｜ 评论：7
- [#210](https://github.com/TigerVNC/tigervnc/issues/210) No logs turn up when using the -Log command line
  - 类型：enhancement ｜ 创建：2015-10-06 ｜ 评论：0
- [#250](https://github.com/TigerVNC/tigervnc/issues/250) Complete VNC over SSH proposal
  - 类型：enhancement ｜ 创建：2015-12-07 ｜ 评论：23
- [#252](https://github.com/TigerVNC/tigervnc/issues/252) "unable to start the service: Access is denied. (5)" trying to start service
  - 类型：bug, winvnc ｜ 创建：2015-12-08 ｜ 评论：6
- [#323](https://github.com/TigerVNC/tigervnc/issues/323) Remote file manager
  - 类型：enhancement ｜ 创建：2016-06-11 ｜ 评论：10
- [#417](https://github.com/TigerVNC/tigervnc/issues/417) Implement a top menu/toolbar
  - 类型：enhancement ｜ 创建：2017-02-15 ｜ 评论：9
- [#490](https://github.com/TigerVNC/tigervnc/issues/490) Add  feedback to show encrypted session
  - 类型：enhancement ｜ 创建：2017-07-27 ｜ 评论：23
- [#569](https://github.com/TigerVNC/tigervnc/issues/569) Can't quit using cmd-q with only dialog
  - 类型：bug ｜ 创建：2018-01-04 ｜ 评论：16
- [#704](https://github.com/TigerVNC/tigervnc/issues/704) binaries are too hard to find
  - 类型：enhancement ｜ 创建：2018-08-29 ｜ 评论：9
- [#850](https://github.com/TigerVNC/tigervnc/issues/850) Online docs format command argument hyphens (U+002D) as minus sign (U+2212)
  - 类型：bug ｜ 创建：2019-07-15 ｜ 评论：5
- [#869](https://github.com/TigerVNC/tigervnc/issues/869) x0tigervncserver crashes on user action under VNC desktop
  - 类型：notourbug ｜ 创建：2019-09-13 ｜ 评论：46
- [#956](https://github.com/TigerVNC/tigervnc/issues/956) Configuration file detection is not evident
  - 类型：enhancement, help wanted ｜ 创建：2020-02-06 ｜ 评论：3
- [#979](https://github.com/TigerVNC/tigervnc/issues/979) Segmentation fault at address 0x38
  - 类型：bug ｜ 创建：2020-03-16 ｜ 评论：19
- [#1006](https://github.com/TigerVNC/tigervnc/issues/1006) dmg file in mac catalina is not working
  - 类型：- ｜ 创建：2020-04-28 ｜ 评论：7
- [#1047](https://github.com/TigerVNC/tigervnc/issues/1047) x0tigervncserver locks up frequently
  - 类型：bug ｜ 创建：2020-06-17 ｜ 评论：9
- [#1083](https://github.com/TigerVNC/tigervnc/issues/1083) Parameter "ScalingFactor" does nothing
  - 类型：- ｜ 创建：2020-08-27 ｜ 评论：4
- [#1149](https://github.com/TigerVNC/tigervnc/issues/1149) vncsession wrong exit code
  - 类型：enhancement ｜ 创建：2020-11-16 ｜ 评论：1
- [#1233](https://github.com/TigerVNC/tigervnc/issues/1233) Remote into a single app or window on a host machine
  - 类型：enhancement ｜ 创建：2021-04-10 ｜ 评论：0
- [#1283](https://github.com/TigerVNC/tigervnc/issues/1283) Make GitHub Action Artifacts available
  - 类型：enhancement ｜ 创建：2021-07-01 ｜ 评论：10
- [#1356](https://github.com/TigerVNC/tigervnc/issues/1356) F8 menu runs away
  - 类型：bug ｜ 创建：2021-10-13 ｜ 评论：13
- [#1364](https://github.com/TigerVNC/tigervnc/issues/1364) Add confirmation dialog when closing the window
  - 类型：enhancement ｜ 创建：2021-10-31 ｜ 评论：2
- [#1389](https://github.com/TigerVNC/tigervnc/issues/1389) IPv6 address in HostsFile not working
  - 类型：bug, help wanted ｜ 创建：2021-12-08 ｜ 评论：1
- [#1407](https://github.com/TigerVNC/tigervnc/issues/1407) Local printer sharing.
  - 类型：enhancement ｜ 创建：2022-01-11 ｜ 评论：1
- [#1408](https://github.com/TigerVNC/tigervnc/issues/1408) Local USB device passthrough.
  - 类型：enhancement ｜ 创建：2022-01-12 ｜ 评论：4
- [#1538](https://github.com/TigerVNC/tigervnc/issues/1538) Documentation for Tiger VNC configuration files is incomplete
  - 类型：enhancement ｜ 创建：2022-10-24 ｜ 评论：3
- [#1571](https://github.com/TigerVNC/tigervnc/issues/1571) [Bug] text doesn't fit inside some text fields for specific languages
  - 类型：bug ｜ 创建：2023-01-01 ｜ 评论：5
- [#1583](https://github.com/TigerVNC/tigervnc/issues/1583) Interactive toggle of ViewOnly mode
  - 类型：enhancement ｜ 创建：2023-01-11 ｜ 评论：0
- [#1603](https://github.com/TigerVNC/tigervnc/issues/1603) `vncsession` should emit an error when `vncpasswd` is missing and also required
  - 类型：enhancement ｜ 创建：2023-02-22 ｜ 评论：2
- [#1616](https://github.com/TigerVNC/tigervnc/issues/1616) Look is outdated
  - 类型：enhancement ｜ 创建：2023-04-07 ｜ 评论：3
- [#1687](https://github.com/TigerVNC/tigervnc/issues/1687) Instance identification in title bar
  - 类型：enhancement ｜ 创建：2023-10-21 ｜ 评论：9
- [#1740](https://github.com/TigerVNC/tigervnc/issues/1740) Ghost image on minimized Tk window resizing
  - 类型：bug ｜ 创建：2024-03-21 ｜ 评论：1
- [#1745](https://github.com/TigerVNC/tigervnc/issues/1745) Input system does scans only the first layout for symbols
  - 类型：enhancement ｜ 创建：2024-04-14 ｜ 评论：3
- [#1748](https://github.com/TigerVNC/tigervnc/issues/1748) Tab completion
  - 类型：enhancement ｜ 创建：2024-04-19 ｜ 评论：0
- [#1773](https://github.com/TigerVNC/tigervnc/issues/1773) OpenGL isn't accelerated with Nvidia drivers
  - 类型：bug ｜ 创建：2024-06-21 ｜ 评论：19
- [#1800](https://github.com/TigerVNC/tigervnc/issues/1800) vncserver doesn't start on Ubuntu if TigerVNC isn't active alternative
  - 类型：bug ｜ 创建：2024-07-31 ｜ 评论：4
- [#1905](https://github.com/TigerVNC/tigervnc/issues/1905) Open remote files locally
  - 类型：enhancement ｜ 创建：2025-02-05 ｜ 评论：2
- [#1907](https://github.com/TigerVNC/tigervnc/issues/1907) vncpasswd: ability to combine -t and -f flags
  - 类型：enhancement ｜ 创建：2025-02-07 ｜ 评论：1
- [#1971](https://github.com/TigerVNC/tigervnc/issues/1971) Please add SOCKS 5 proxy feature for TigerVNC client
  - 类型：enhancement ｜ 创建：2025-06-24 ｜ 评论：0
- [#2000](https://github.com/TigerVNC/tigervnc/issues/2000) [request] TigerVNC on flathub officially
  - 类型：enhancement ｜ 创建：2025-10-09 ｜ 评论：1
- [#2018](https://github.com/TigerVNC/tigervnc/issues/2018) Bad error message on UnknownOption=value
  - 类型：enhancement ｜ 创建：2025-11-22 ｜ 评论：3
- [#2099](https://github.com/TigerVNC/tigervnc/issues/2099) Compatibility with RealVNC for Android
  - 类型：- ｜ 创建：2026-05-12 ｜ 评论：13
- [#2117](https://github.com/TigerVNC/tigervnc/issues/2117) Crash (segfault) on exit
  - 类型：bug, notourbug ｜ 创建：2026-06-03 ｜ 评论：7
- [#2122](https://github.com/TigerVNC/tigervnc/issues/2122) Confusing error message for backward incompatible CLI options
  - 类型：bug ｜ 创建：2026-06-27 ｜ 评论：2
- [#2143](https://github.com/TigerVNC/tigervnc/issues/2143) Advertise servers with MDNS/DNS-SD
  - 类型：enhancement ｜ 创建：2026-08-08 ｜ 评论：6

### Windows 客户端 (winvnc)（27 个）

- [#105](https://github.com/TigerVNC/tigervnc/issues/105) Command line options on winvnc4.exe
  - 类型：enhancement, winvnc ｜ 创建：2015-01-21 ｜ 评论：9
- [#116](https://github.com/TigerVNC/tigervnc/issues/116) Connecting alters resolution on Windows
  - 类型：winvnc ｜ 创建：2015-02-06 ｜ 评论：10
- [#148](https://github.com/TigerVNC/tigervnc/issues/148) Option to disable tray icon (Windows)
  - 类型：enhancement, winvnc ｜ 创建：2015-03-06 ｜ 评论：12
- [#151](https://github.com/TigerVNC/tigervnc/issues/151) Unable to find 'TigerVNC' or 'VNC Server Version 4' in Windows 7 Firewall
  - 类型：enhancement, winvnc ｜ 创建：2015-03-14 ｜ 评论：4
- [#199](https://github.com/TigerVNC/tigervnc/issues/199) TigerVNC does not intercept Win+L on Windows 10
  - 类型：bug ｜ 创建：2015-09-21 ｜ 评论：11
- [#273](https://github.com/TigerVNC/tigervnc/issues/273) Windows 10: UAC closes VNC connection
  - 类型：bug, winvnc ｜ 创建：2016-02-23 ｜ 评论：3
- [#277](https://github.com/TigerVNC/tigervnc/issues/277) TigerVNC 1.6.0 on Windows 10 running as a Service is unbearably slow and does not work in user mode.
  - 类型：bug, winvnc ｜ 创建：2016-03-14 ｜ 评论：26
- [#319](https://github.com/TigerVNC/tigervnc/issues/319) Windows -help does not work in neither server nor client
  - 类型：enhancement ｜ 创建：2016-05-27 ｜ 评论：3
- [#393](https://github.com/TigerVNC/tigervnc/issues/393) mouse cursor incorrectly scaled on hidpi Windows system
  - 类型：bug, help wanted ｜ 创建：2016-12-28 ｜ 评论：5
- [#425](https://github.com/TigerVNC/tigervnc/issues/425) server side resize support on Windows
  - 类型：enhancement, winvnc ｜ 创建：2017-03-02 ｜ 评论：4
- [#494](https://github.com/TigerVNC/tigervnc/issues/494) Windows server support for HiDPI
  - 类型：enhancement, winvnc ｜ 创建：2017-08-11 ｜ 评论：7
- [#516](https://github.com/TigerVNC/tigervnc/issues/516) WinVNC UI shouldn't run as SYSTEM
  - 类型：bug, winvnc ｜ 创建：2017-09-29 ｜ 评论：2
- [#523](https://github.com/TigerVNC/tigervnc/issues/523) Switching users/session via WinVNC leads to disconnect
  - 类型：bug, winvnc ｜ 创建：2017-10-10 ｜ 评论：7
- [#571](https://github.com/TigerVNC/tigervnc/issues/571) Windows vncconfig option to change query timeout
  - 类型：enhancement, winvnc ｜ 创建：2018-01-05 ｜ 评论：3
- [#627](https://github.com/TigerVNC/tigervnc/issues/627) [Windows] Do not create the registry key if the server does not need it
  - 类型：enhancement, winvnc ｜ 创建：2018-04-10 ｜ 评论：2
- [#636](https://github.com/TigerVNC/tigervnc/issues/636) Windows 10 running TigerVNC as a service loses input after a few minutes.
  - 类型：bug, winvnc ｜ 创建：2018-04-29 ｜ 评论：1
- [#943](https://github.com/TigerVNC/tigervnc/issues/943) WinVNC crashes when client connects
  - 类型：bug, winvnc ｜ 创建：2020-01-15 ｜ 评论：12
- [#964](https://github.com/TigerVNC/tigervnc/issues/964) Read only password in WinVNC
  - 类型：enhancement, winvnc ｜ 创建：2020-02-12 ｜ 评论：4
- [#1003](https://github.com/TigerVNC/tigervnc/issues/1003) "Access denied" from WinVNC on connect
  - 类型：bug, help wanted, winvnc ｜ 创建：2020-04-23 ｜ 评论：7
- [#1226](https://github.com/TigerVNC/tigervnc/issues/1226) Windows taskbar shows on top of fullscreen vncviewer on secondary monitor
  - 类型：bug, help wanted ｜ 创建：2021-03-26 ｜ 评论：5
- [#1578](https://github.com/TigerVNC/tigervnc/issues/1578) Windows start/win key 'sticks' after switching between Windows virtual desktops
  - 类型：bug ｜ 创建：2023-01-10 ｜ 评论：5
- [#1750](https://github.com/TigerVNC/tigervnc/issues/1750) "-via" should be available on windows (cmdline and GUI)
  - 类型：enhancement ｜ 创建：2024-04-26 ｜ 评论：1
- [#1754](https://github.com/TigerVNC/tigervnc/issues/1754) screen freezes and black artefacts on Windows VNC server
  - 类型：bug, winvnc ｜ 创建：2024-05-05 ｜ 评论：7
- [#1844](https://github.com/TigerVNC/tigervnc/issues/1844) TigerVNC Viewer opens under (hidden by) Windows taskbar when taskbar is at top of screen
  - 类型：bug ｜ 创建：2024-10-08 ｜ 评论：1
- [#1851](https://github.com/TigerVNC/tigervnc/issues/1851) Windows TigerVNC server crashes on client connection
  - 类型：bug, winvnc ｜ 创建：2024-10-17 ｜ 评论：1
- [#1872](https://github.com/TigerVNC/tigervnc/issues/1872) [windows] Inno Setup installer only 1 file include 32 and 64 bit
  - 类型：enhancement ｜ 创建：2024-11-17 ｜ 评论：2
- [#2072](https://github.com/TigerVNC/tigervnc/issues/2072) Windows 11: with `Prompt local user to accept connections` and TigerVNC running as service, causes first connection to fail
  - 类型：bug, winvnc ｜ 创建：2026-03-01 ｜ 评论：0

### 查看器 (vncviewer)（18 个）

- [#153](https://github.com/TigerVNC/tigervnc/issues/153) Different server profiles in viewer
  - 类型：enhancement, help wanted ｜ 创建：2015-03-17 ｜ 评论：6
- [#283](https://github.com/TigerVNC/tigervnc/issues/283) fullscreen+maximize+minimize problem
  - 类型：bug ｜ 创建：2016-03-22 ｜ 评论：7
- [#398](https://github.com/TigerVNC/tigervnc/issues/398) Not all options are supported in vncviewer config file
  - 类型：bug ｜ 创建：2017-01-14 ｜ 评论：19
- [#400](https://github.com/TigerVNC/tigervnc/issues/400) `xtigervncviewer -listen` accepts only one connection
  - 类型：enhancement ｜ 创建：2017-01-18 ｜ 评论：2
- [#662](https://github.com/TigerVNC/tigervnc/issues/662) vncviewer should support the UltraVNC ServerInput extension (disable input, blank monitor)
  - 类型：enhancement ｜ 创建：2018-06-13 ｜ 评论：0
- [#822](https://github.com/TigerVNC/tigervnc/issues/822) creates C:\FLTK\fltk.org\fltk.prefs / ~/.fltk/fltk.org/fltk.prefs
  - 类型：bug ｜ 创建：2019-04-13 ｜ 评论：26
- [#835](https://github.com/TigerVNC/tigervnc/issues/835) send keys from viewer not present on local keyboard
  - 类型：enhancement ｜ 创建：2019-05-11 ｜ 评论：4
- [#839](https://github.com/TigerVNC/tigervnc/issues/839) vncviewer slow with unaccelerated X server (e.g. fbdev)
  - 类型：enhancement ｜ 创建：2019-05-25 ｜ 评论：22
- [#883](https://github.com/TigerVNC/tigervnc/issues/883) specify password for vncviewer from command line
  - 类型：enhancement ｜ 创建：2019-10-18 ｜ 评论：5
- [#947](https://github.com/TigerVNC/tigervnc/issues/947) Feature request: vncviewer: Return back to connection details window when closing a active connection window or connection can't established
  - 类型：enhancement ｜ 创建：2020-01-29 ｜ 评论：1
- [#1008](https://github.com/TigerVNC/tigervnc/issues/1008) Meaningful exit codes from vncviewer
  - 类型：enhancement, help wanted ｜ 创建：2020-05-01 ｜ 评论：9
- [#1190](https://github.com/TigerVNC/tigervnc/issues/1190) 2-finger scrolling in vncviewer: inconsistent buttons reported on remote host
  - 类型：- ｜ 创建：2021-01-21 ｜ 评论：11
- [#1354](https://github.com/TigerVNC/tigervnc/issues/1354) Provide the native vncviewer as an AppImage for Linux
  - 类型：enhancement ｜ 创建：2021-10-10 ｜ 评论：3
- [#1391](https://github.com/TigerVNC/tigervnc/issues/1391) Initial full screen isn't on "current" monitor
  - 类型：bug ｜ 创建：2021-12-10 ｜ 评论：2
- [#1594](https://github.com/TigerVNC/tigervnc/issues/1594) -RemoteResize with -Shared causes an infinite resize loop with mulitple viewers
  - 类型：bug ｜ 创建：2023-02-04 ｜ 评论：5
- [#1855](https://github.com/TigerVNC/tigervnc/issues/1855) Java VNC viewer is not able to accept clipboard data from QEMU VNC Server
  - 类型：- ｜ 创建：2024-10-21 ｜ 评论：6
- [#1949](https://github.com/TigerVNC/tigervnc/issues/1949) Please update for FLTK 1.4
  - 类型：enhancement ｜ 创建：2025-05-13 ｜ 评论：17
- [#1981](https://github.com/TigerVNC/tigervnc/issues/1981) vncviewer -DesktopSize doesn't support a resolution greater than a single display
  - 类型：enhancement ｜ 创建：2025-08-09 ｜ 评论：4

### 网络与连接（17 个）

- [#227](https://github.com/TigerVNC/tigervnc/issues/227) VNC server session management (one port, multiple sessions)
  - 类型：enhancement ｜ 创建：2015-10-26 ｜ 评论：1
- [#347](https://github.com/TigerVNC/tigervnc/issues/347) -via does not allow to specify a port
  - 类型：enhancement ｜ 创建：2016-09-03 ｜ 评论：2
- [#369](https://github.com/TigerVNC/tigervnc/issues/369) client support for HiDPI
  - 类型：enhancement ｜ 创建：2016-10-16 ｜ 评论：16
- [#451](https://github.com/TigerVNC/tigervnc/issues/451) VNC Client & Server should connect to VNC repeater
  - 类型：enhancement ｜ 创建：2017-05-01 ｜ 评论：6
- [#801](https://github.com/TigerVNC/tigervnc/issues/801) tigervncserver's vncserver perl script limited to 99 connections?
  - 类型：enhancement ｜ 创建：2019-03-08 ｜ 评论：2
- [#910](https://github.com/TigerVNC/tigervnc/issues/910) stdio client connections
  - 类型：enhancement ｜ 创建：2019-11-29 ｜ 评论：0
- [#922](https://github.com/TigerVNC/tigervnc/issues/922) Dropped connection not detected with ViewOnly
  - 类型：bug, help wanted ｜ 创建：2019-12-20 ｜ 评论：2
- [#999](https://github.com/TigerVNC/tigervnc/issues/999) Server process stops if "Accept connections on port" is unchecked
  - 类型：bug, help wanted, winvnc ｜ 创建：2020-04-18 ｜ 评论：0
- [#1229](https://github.com/TigerVNC/tigervnc/issues/1229) Support cli argument url/uri
  - 类型：enhancement ｜ 创建：2021-03-28 ｜ 评论：2
- [#1232](https://github.com/TigerVNC/tigervnc/issues/1232) Not context or detail to explain WHY "This connection is not secure"
  - 类型：enhancement ｜ 创建：2021-04-09 ｜ 评论：0
- [#1398](https://github.com/TigerVNC/tigervnc/issues/1398) New option QueryConnectTimeoutAction: Action to be performed after QueryConnectTimeout
  - 类型：enhancement ｜ 创建：2021-12-14 ｜ 评论：3
- [#1627](https://github.com/TigerVNC/tigervnc/issues/1627) more info at  Attempt to reconnect  dialog
  - 类型：enhancement, help wanted ｜ 创建：2023-05-02 ｜ 评论：5
- [#1658](https://github.com/TigerVNC/tigervnc/issues/1658) Client dark mode support
  - 类型：enhancement ｜ 创建：2023-08-20 ｜ 评论：3
- [#1676](https://github.com/TigerVNC/tigervnc/issues/1676) VNC Server connection fail 10054
  - 类型：winvnc ｜ 创建：2023-09-15 ｜ 评论：1
- [#1768](https://github.com/TigerVNC/tigervnc/issues/1768) WebSocket support
  - 类型：enhancement ｜ 创建：2024-06-19 ｜ 评论：1
- [#1953](https://github.com/TigerVNC/tigervnc/issues/1953) Proposal: Add i18n Support to QueryConnectDialog in TigerVNC
  - 类型：enhancement ｜ 创建：2025-05-25 ｜ 评论：0
- [#2148](https://github.com/TigerVNC/tigervnc/issues/2148) Occasional connection losses
  - 类型：- ｜ 创建：2026-08-27 ｜ 评论：22

### X11 服务器 (Xvnc)（15 个）

- [#328](https://github.com/TigerVNC/tigervnc/issues/328) Add -randr option to Xvnc
  - 类型：enhancement ｜ 创建：2016-07-07 ｜ 评论：6
- [#394](https://github.com/TigerVNC/tigervnc/issues/394) Xorg module libvnc.so needs documentation
  - 类型：enhancement ｜ 创建：2016-12-29 ｜ 评论：5
- [#528](https://github.com/TigerVNC/tigervnc/issues/528) x0vncserver does not generate fake shifts etc
  - 类型：enhancement ｜ 创建：2017-10-24 ｜ 评论：3
- [#599](https://github.com/TigerVNC/tigervnc/issues/599) x0vncserver doesn't support config files
  - 类型：enhancement ｜ 创建：2018-02-14 ｜ 评论：3
- [#663](https://github.com/TigerVNC/tigervnc/issues/663) x0vncserver should support the UltraVNC ServerInput extension (disable input, blank monitor)
  - 类型：enhancement ｜ 创建：2018-06-13 ｜ 评论：1
- [#697](https://github.com/TigerVNC/tigervnc/issues/697) x0vncserver: No reverse connection possible
  - 类型：enhancement ｜ 创建：2018-08-11 ｜ 评论：1
- [#961](https://github.com/TigerVNC/tigervnc/issues/961) Support for INCR in Xvnc (large clipboard)
  - 类型：enhancement ｜ 创建：2020-02-08 ｜ 评论：10
- [#1563](https://github.com/TigerVNC/tigervnc/issues/1563) Laggy cursor updates with x0vncserver
  - 类型：bug ｜ 创建：2022-12-15 ｜ 评论：5
- [#1733](https://github.com/TigerVNC/tigervnc/issues/1733) x0vncserver HDR (10-bit) support
  - 类型：enhancement ｜ 创建：2024-02-28 ｜ 评论：3
- [#1766](https://github.com/TigerVNC/tigervnc/issues/1766) When QueryConnect is enabled on x0vncserver, there may be exceptions when multiple connections are concurrent
  - 类型：bug ｜ 创建：2024-06-18 ｜ 评论：1
- [#1772](https://github.com/TigerVNC/tigervnc/issues/1772) Xvnc crashes with SIGBUS on cross-GPU DRI usage
  - 类型：bug ｜ 创建：2024-06-21 ｜ 评论：11
- [#1890](https://github.com/TigerVNC/tigervnc/issues/1890) Xvnc Record extension may cause spurious event flood in KDE Plasma 5/6
  - 类型：- ｜ 创建：2024-12-29 ｜ 评论：2
- [#1962](https://github.com/TigerVNC/tigervnc/issues/1962) x0vncserver resize doesn't respect -geometry
  - 类型：bug ｜ 创建：2025-06-10 ｜ 评论：7
- [#2089](https://github.com/TigerVNC/tigervnc/issues/2089) syslog logging only available in Xvnc
  - 类型：enhancement ｜ 创建：2026-04-19 ｜ 评论：1
- [#2193](https://github.com/TigerVNC/tigervnc/issues/2193) Xvnc/vncserver: Allow custom screen refresh rate (cvt mode)
  - 类型：- ｜ 创建：2026-11-06 ｜ 评论：5

### Wayland (w0vncserver)（13 个）

- [#158](https://github.com/TigerVNC/tigervnc/issues/158) Wayland?
  - 类型：enhancement ｜ 创建：2015-03-23 ｜ 评论：23
- [#1990](https://github.com/TigerVNC/tigervnc/issues/1990) w0vncserver: LED states are not synced
  - 类型：enhancement ｜ 创建：2025-09-25 ｜ 评论：0
- [#1991](https://github.com/TigerVNC/tigervnc/issues/1991) w0vncserver: Limited keysyms support
  - 类型：enhancement ｜ 创建：2025-09-25 ｜ 评论：3
- [#1992](https://github.com/TigerVNC/tigervnc/issues/1992) w0vncserver: Key repeats are not handled by the client
  - 类型：bug, notourbug ｜ 创建：2025-09-25 ｜ 评论：1
- [#1995](https://github.com/TigerVNC/tigervnc/issues/1995) w0vncserver: Desktop resizing does not work
  - 类型：enhancement ｜ 创建：2025-09-25 ｜ 评论：2
- [#2027](https://github.com/TigerVNC/tigervnc/issues/2027) w0vncserver: No multi-head support
  - 类型：enhancement ｜ 创建：2025-11-28 ｜ 评论：4
- [#2052](https://github.com/TigerVNC/tigervnc/issues/2052) w0vncserver: Scaling issues on GNOME/KDE
  - 类型：bug ｜ 创建：2026-01-28 ｜ 评论：4
- [#2067](https://github.com/TigerVNC/tigervnc/issues/2067) PortalDesktop: Primary selection doesn't work
  - 类型：enhancement ｜ 创建：2026-02-13 ｜ 评论：0
- [#2068](https://github.com/TigerVNC/tigervnc/issues/2068) w0vncserver as service dont work for each user?
  - 类型：enhancement ｜ 创建：2026-02-16 ｜ 评论：9
- [#2076](https://github.com/TigerVNC/tigervnc/issues/2076) w0vncserver: WaylandPixelBuffer does unnecessary buffer copy
  - 类型：enhancement ｜ 创建：2026-03-10 ｜ 评论：2
- [#2154](https://github.com/TigerVNC/tigervnc/issues/2154) w0vncserver: a compositor-closed PipeWire stream is never reported — the session freezes silently and forever
  - 类型：bug ｜ 创建：2026-09-02 ｜ 评论：1
- [#2188](https://github.com/TigerVNC/tigervnc/issues/2188) w0vncserver resizes a resizable (virtual) output to 1280x720 when a client connects
  - 类型：enhancement ｜ 创建：2026-10-25 ｜ 评论：1
- [#2189](https://github.com/TigerVNC/tigervnc/issues/2189) w0vncserver doesn't look at pipewire-serial
  - 类型：enhancement ｜ 创建：2026-10-26 ｜ 评论：0

### 键盘与鼠标（11 个）

- [#262](https://github.com/TigerVNC/tigervnc/issues/262) [request] an optional key combination to allow panning in a VNC window with the scroll wheel
  - 类型：enhancement ｜ 创建：2016-01-25 ｜ 评论：0
- [#512](https://github.com/TigerVNC/tigervnc/issues/512) Java: Incorrect rendering of full-color cursors on Linux
  - 类型：- ｜ 创建：2017-09-11 ｜ 评论：3
- [#619](https://github.com/TigerVNC/tigervnc/issues/619) [REQUEST] Mouse synchronization/grabbing/capture/wrapping
  - 类型：enhancement ｜ 创建：2018-03-25 ｜ 评论：30
- [#649](https://github.com/TigerVNC/tigervnc/issues/649) Java client issue with Alt-F4 key combo
  - 类型：- ｜ 创建：2018-05-25 ｜ 评论：0
- [#1337](https://github.com/TigerVNC/tigervnc/issues/1337) Mouse has slight delay/kick in time, making stylus usage impossible
  - 类型：bug ｜ 创建：2021-09-18 ｜ 评论：8
- [#1340](https://github.com/TigerVNC/tigervnc/issues/1340) Toggling "Show dot when no cursor" doesn't work when connected
  - 类型：bug ｜ 创建：2021-09-20 ｜ 评论：1
- [#1417](https://github.com/TigerVNC/tigervnc/issues/1417) Select keyboard language from client
  - 类型：enhancement ｜ 创建：2022-01-24 ｜ 评论：1
- [#1614](https://github.com/TigerVNC/tigervnc/issues/1614) Mouse cursor suddenly stops changing
  - 类型：bug ｜ 创建：2023-03-24 ｜ 评论：3
- [#1741](https://github.com/TigerVNC/tigervnc/issues/1741) Mouse scrollwheel extermely slow w/ Mac Sonoma 14.4.1
  - 类型：notourbug, apple remote desktop ｜ 创建：2024-03-29 ｜ 评论：20
- [#1977](https://github.com/TigerVNC/tigervnc/issues/1977) Emulate Middle Button press on Mac with Magic Mouse
  - 类型：enhancement ｜ 创建：2025-07-18 ｜ 评论：2
- [#2028](https://github.com/TigerVNC/tigervnc/issues/2028) Menu "Ctrl"/"Alt" sends multiple key events
  - 类型：bug ｜ 创建：2025-12-02 ｜ 评论：2

### 多显示器（8 个）

- [#16](https://github.com/TigerVNC/tigervnc/issues/16) Initial screen size not saved as available RRMode
  - 类型：enhancement ｜ 创建：2014-07-18 ｜ 评论：7
- [#274](https://github.com/TigerVNC/tigervnc/issues/274) scale remote screen to window
  - 类型：enhancement, help wanted ｜ 创建：2016-02-23 ｜ 评论：84
- [#1218](https://github.com/TigerVNC/tigervnc/issues/1218) libvnc.so screen not refreshing when glamor is used
  - 类型：notourbug ｜ 创建：2021-03-12 ｜ 评论：41
- [#1367](https://github.com/TigerVNC/tigervnc/issues/1367) Revert to original resolution after client disconnecting
  - 类型：enhancement ｜ 创建：2021-11-07 ｜ 评论：1
- [#1394](https://github.com/TigerVNC/tigervnc/issues/1394) Remote desktop resized on connection. Config option not retained. 
  - 类型：enhancement ｜ 创建：2021-12-12 ｜ 评论：2
- [#1397](https://github.com/TigerVNC/tigervnc/issues/1397) Display number of connected clients as overlay
  - 类型：enhancement ｜ 创建：2021-12-14 ｜ 评论：0
- [#1617](https://github.com/TigerVNC/tigervnc/issues/1617) wrong multiple monitors arrangement with inconsistant monitor scales
  - 类型：bug ｜ 创建：2023-04-10 ｜ 评论：4
- [#1938](https://github.com/TigerVNC/tigervnc/issues/1938) Remote resize doesn't work well with multiple clients
  - 类型：enhancement ｜ 创建：2025-04-02 ｜ 评论：5

### 安全与认证（8 个）

- [#307](https://github.com/TigerVNC/tigervnc/issues/307) Supporting the encryption of vino server (auth 18)
  - 类型：enhancement ｜ 创建：2016-05-10 ｜ 评论：8
- [#356](https://github.com/TigerVNC/tigervnc/issues/356) separate SecurityTypes option for reverse VNC connection
  - 类型：enhancement ｜ 创建：2016-09-14 ｜ 评论：0
- [#370](https://github.com/TigerVNC/tigervnc/issues/370) warn about maximum VNC password length
  - 类型：enhancement ｜ 创建：2016-10-19 ｜ 评论：5
- [#679](https://github.com/TigerVNC/tigervnc/issues/679) Missing documentation about SecurityTypes
  - 类型：enhancement ｜ 创建：2018-07-15 ｜ 评论：2
- [#787](https://github.com/TigerVNC/tigervnc/issues/787) X509 client authentication
  - 类型：enhancement, help wanted ｜ 创建：2019-01-14 ｜ 评论：8
- [#891](https://github.com/TigerVNC/tigervnc/issues/891) Support longer passwords
  - 类型：enhancement ｜ 创建：2019-10-24 ｜ 评论：1
- [#1327](https://github.com/TigerVNC/tigervnc/issues/1327) Please provide a file requester for the selection of a X509 CA certificate
  - 类型：enhancement ｜ 创建：2021-09-06 ｜ 评论：0
- [#1635](https://github.com/TigerVNC/tigervnc/issues/1635) Unsupported security types are not ignored from config or command line
  - 类型：bug ｜ 创建：2023-06-14 ｜ 评论：2

### macOS 客户端（7 个）

- [#517](https://github.com/TigerVNC/tigervnc/issues/517) Multi monitor support with macOS "separate spaces" active
  - 类型：enhancement ｜ 创建：2017-09-29 ｜ 评论：6
- [#878](https://github.com/TigerVNC/tigervnc/issues/878) "High Resolution" mode on macOS
  - 类型：enhancement, help wanted ｜ 创建：2019-10-10 ｜ 评论：23
- [#1399](https://github.com/TigerVNC/tigervnc/issues/1399) Monitor name hints not shown on Apple M1
  - 类型：bug ｜ 创建：2021-12-23 ｜ 评论：0
- [#1693](https://github.com/TigerVNC/tigervnc/issues/1693) Crash on MacOS with no "roman" layouts
  - 类型：- ｜ 创建：2023-11-09 ｜ 评论：27
- [#1912](https://github.com/TigerVNC/tigervnc/issues/1912) v1.15.0 client on macOS: Unable to connect to socket: No route to host (65)
  - 类型：notourbug ｜ 创建：2025-02-17 ｜ 评论：14
- [#2035](https://github.com/TigerVNC/tigervnc/issues/2035) VNC Viewer 1.15 does not work with multiple monitors on MacOS 26.1
  - 类型：- ｜ 创建：2026-01-02 ｜ 评论：8
- [#2187](https://github.com/TigerVNC/tigervnc/issues/2187) macOS: vncviewer window sometimes comes up ~170px and entirely off-screen
  - 类型：- ｜ 创建：2026-10-23 ｜ 评论：3

### 剪贴板（3 个）

- [#188](https://github.com/TigerVNC/tigervnc/issues/188) Copying text to clipboard from Microsoft Edge causes unresponsiveness
  - 类型：bug ｜ 创建：2015-08-18 ｜ 评论：3
- [#1896](https://github.com/TigerVNC/tigervnc/issues/1896) Server selection overwrites client clipboard
  - 类型：enhancement ｜ 创建：2025-01-15 ｜ 评论：5
- [#2139](https://github.com/TigerVNC/tigervnc/issues/2139) Paste text thru keyboard input (or emulated keypresses)
  - 类型：enhancement ｜ 创建：2026-08-02 ｜ 评论：2

### 编码 (Tight/JPEG/ZRLE)（3 个）

- [#945](https://github.com/TigerVNC/tigervnc/issues/945) Zstd encoding
  - 类型：enhancement, help wanted ｜ 创建：2020-01-22 ｜ 评论：2
- [#1662](https://github.com/TigerVNC/tigervnc/issues/1662) UTF-8 clipboard support for TightVNC server
  - 类型：enhancement ｜ 创建：2023-08-28 ｜ 评论：4
- [#2121](https://github.com/TigerVNC/tigervnc/issues/2121) Can't connect to a 4k server: TightDecoder: Too large rectangle (3840 pixels)
  - 类型：notourbug ｜ 创建：2026-06-15 ｜ 评论：28

### 音频（1 个）

- [#1211](https://github.com/TigerVNC/tigervnc/issues/1211) Sound through VNC
  - 类型：enhancement ｜ 创建：2021-02-28 ｜ 评论：10

## 三、建议处理优先级

### P0：影响稳定性的缺陷（优先修复）

- [#822](https://github.com/TigerVNC/tigervnc/issues/822) creates C:\FLTK\fltk.org\fltk.prefs / ~/.fltk/fltk.org/fltk.prefs（26 条评论）
- [#277](https://github.com/TigerVNC/tigervnc/issues/277) TigerVNC 1.6.0 on Windows 10 running as a Service is unbearably slow and does not work in user mode.（26 条评论）
- [#1773](https://github.com/TigerVNC/tigervnc/issues/1773) OpenGL isn't accelerated with Nvidia drivers（19 条评论）
- [#979](https://github.com/TigerVNC/tigervnc/issues/979) Segmentation fault at address 0x38（19 条评论）
- [#398](https://github.com/TigerVNC/tigervnc/issues/398) Not all options are supported in vncviewer config file（19 条评论）
- [#569](https://github.com/TigerVNC/tigervnc/issues/569) Can't quit using cmd-q with only dialog（16 条评论）
- [#1356](https://github.com/TigerVNC/tigervnc/issues/1356) F8 menu runs away（13 条评论）
- [#943](https://github.com/TigerVNC/tigervnc/issues/943) WinVNC crashes when client connects（12 条评论）
- [#1772](https://github.com/TigerVNC/tigervnc/issues/1772) Xvnc crashes with SIGBUS on cross-GPU DRI usage（11 条评论）
- [#199](https://github.com/TigerVNC/tigervnc/issues/199) TigerVNC does not intercept Win+L on Windows 10（11 条评论）
- [#1047](https://github.com/TigerVNC/tigervnc/issues/1047) x0tigervncserver locks up frequently（9 条评论）
- [#1337](https://github.com/TigerVNC/tigervnc/issues/1337) Mouse has slight delay/kick in time, making stylus usage impossible（8 条评论）
- [#2117](https://github.com/TigerVNC/tigervnc/issues/2117) Crash (segfault) on exit（7 条评论）
- [#1962](https://github.com/TigerVNC/tigervnc/issues/1962) x0vncserver resize doesn't respect -geometry（7 条评论）
- [#1754](https://github.com/TigerVNC/tigervnc/issues/1754) screen freezes and black artefacts on Windows VNC server（7 条评论）

### P1：高频需求的功能

- [#274](https://github.com/TigerVNC/tigervnc/issues/274) scale remote screen to window（84 条评论）
- [#619](https://github.com/TigerVNC/tigervnc/issues/619) [REQUEST] Mouse synchronization/grabbing/capture/wrapping（30 条评论）
- [#878](https://github.com/TigerVNC/tigervnc/issues/878) "High Resolution" mode on macOS（23 条评论）
- [#490](https://github.com/TigerVNC/tigervnc/issues/490) Add  feedback to show encrypted session（23 条评论）
- [#250](https://github.com/TigerVNC/tigervnc/issues/250) Complete VNC over SSH proposal（23 条评论）
- [#158](https://github.com/TigerVNC/tigervnc/issues/158) Wayland?（23 条评论）
- [#839](https://github.com/TigerVNC/tigervnc/issues/839) vncviewer slow with unaccelerated X server (e.g. fbdev)（22 条评论）
- [#1949](https://github.com/TigerVNC/tigervnc/issues/1949) Please update for FLTK 1.4（17 条评论）
- [#369](https://github.com/TigerVNC/tigervnc/issues/369) client support for HiDPI（16 条评论）
- [#148](https://github.com/TigerVNC/tigervnc/issues/148) Option to disable tray icon (Windows)（12 条评论）
- [#1283](https://github.com/TigerVNC/tigervnc/issues/1283) Make GitHub Action Artifacts available（10 条评论）
- [#1211](https://github.com/TigerVNC/tigervnc/issues/1211) Sound through VNC（10 条评论）
- [#961](https://github.com/TigerVNC/tigervnc/issues/961) Support for INCR in Xvnc (large clipboard)（10 条评论）
- [#323](https://github.com/TigerVNC/tigervnc/issues/323) Remote file manager（10 条评论）
- [#2068](https://github.com/TigerVNC/tigervnc/issues/2068) w0vncserver as service dont work for each user?（9 条评论）

