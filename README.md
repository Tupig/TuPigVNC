<div align="center">

# 🐷 TuPigVNC

**基于 TigerVNC 的现代化 VNC 远程桌面客户端**

一台干净的窗口，连上另一台电脑。

[![构建状态](https://github.com/TuPig/TuPigVNC/actions/workflows/msvc.yml/badge.svg)](https://github.com/TuPig/TuPigVNC/actions/workflows/msvc.yml)
[![最新版本](https://img.shields.io/github/v/release/TuPig/TuPigVNC)](https://github.com/TuPig/TuPigVNC/releases)
[![许可证](https://img.shields.io/github/license/TuPig/TuPigVNC)](https://github.com/TuPig/TuPigVNC/blob/main/LICENCE.TXT)
![平台](https://img.shields.io/badge/platform-Windows%2064--bit-6C5CE7)
![语言](https://img.shields.io/badge/C%2B%2B-20-00599C)

</div>

---

## 目录

- [项目简介](#项目简介)
- [功能特性](#功能特性)
- [界面预览](#界面预览)
- [快速开始](#快速开始)
- [从源码构建](#从源码构建)
- [目录结构](#目录结构)
- [配置与常见问题](#配置与常见问题)
- [许可证](#许可证)
- [贡献指南](#贡献指南)

---

## 项目简介

**TuPigVNC 是一个用 C++ 和 FLTK 写的 VNC 远程桌面客户端，Windows x64 一个安装包搞定，装完就能连。**

它基于 [TigerVNC](https://github.com/TigerVNC/tigervnc) 1.16 代码库做了彻底的品牌化与现代化改造——不是简单改个名字的 fork，而是删掉了全部 59,903 行 Java 代码、重构目录结构、重做界面并补齐中文文档后的独立项目。界面走 Material 3 极简风，全中文，静态链接单文件分发，不依赖任何外部 DLL。

> 项目主页：<https://github.com/TuPig/TuPigVNC>
> 当前版本：**0.0.2**（语义化版本，版本号统一在 `CMakeLists.txt` 的 `VERSION` 维护）

---

## 功能特性

### 🎨 界面与交互

| 特性 | 说明 |
| :--- | :--- |
| **Material 3 极简单页** | 单窗口、无侧栏、无标签页；顶部连接区 + 2×2 功能入口（连接历史 / 选项设置 / 自动更新 / 关于），点入口在下方**原地展开面板**，不弹窗、不跳页 |
| **选项设置** | 6 个分类（压缩 / 安全 / 输入 / 键盘 / 显示 / 杂项），直接读写全局参数，改完即生效 |
| **Apple 风格字体** | 优先 `SF Pro` / `PingFang SC`，回退 `Microsoft YaHei`，中文排版清爽 |
| **远程桌面自适应缩放** | Fit-to-window：服务器分辨率不变，画面按窗口缩放并居中，拖动窗口即时重算 |
| **Lucide 图标集** | 菜单与按钮统一使用 Lucide 线性图标（ISC 许可），风格一致 |
| **完整中文本地化** | 931 条界面字符串、文档与 man 手册页全部翻译为简体中文 |

### 🔗 连接与使用

| 特性 | 说明 |
| :--- | :--- |
| **连接历史** | 存于 `User/history.json`，最多 250 台，超出后按**使用次数最少**自动淘汰，支持旧数据迁移 |
| **网络状态浮层** | 远程桌面右下角常驻，显示下行带宽 / 帧率 / 像素率 / 26 根迷你柱状图 / 当前编码 / 会话时长，每 500ms 刷新一次 |
| **剪贴板互通** | 双向同步文本剪贴板，可分别开关收发 |
| **多显示器全屏** | 支持当前屏、全部屏、指定屏等多种全屏模式 |
| **键盘与输入** | 修饰键释放、全屏时发送系统键、鼠标中键模拟、触摸手势缩放 |
| **音频播放** | 通过 `ENABLE_AUDIO` 播放服务器端音频（Windows 使用 `waveOut`） |
| **H.264 编码** | 通过 `ENABLE_H264` 启用 H.264 RFB 编码，带宽紧张时更流畅 |
| **RSA-AES 安全** | 通过 `ENABLE_NETTLE` 支持 RSA-AES 安全类型 |

### 🛠 工程化

| 特性 | 说明 |
| :--- | :--- |
| **滚动日志** | 写入 `安装目录/logs/年月日时分秒.log`，跨日或单文件超过 **20 MB** 自动切分 |
| **配置统一落盘** | 默认存 `安装目录/User`；该目录不可写时回退 `%LOCALAPPDATA%\TuPigVNC` |
| **自动更新** | 后台查询 GitHub Releases，展示版本对比与更新说明；下载后 SHA-256 校验 → `/VERYSILENT` 静默安装 → 自动重启，一键全自动 |
| **静态单文件** | Windows x64 单文件安装包，无 DLL 依赖，免装运行库 |
| **编译优化** | Release 启用 LTO（`/GL` + `/LTCG`），可选 AVX2 指令集 |
| **现代构建** | CMake ≥ 3.21 + C++20，遵循 Modern CMake 目录布局 |
| **CI 与安全** | GitHub Actions 自动构建发版、CodeQL 安全扫描、Dependabot 依赖更新 |

---

## 界面预览

> **TODO**：截图待补充。请将实际运行截图放到 `docs/screenshot.png` 后即可正常显示。

![TuPigVNC 主界面截图](docs/screenshot.png)

---

## 快速开始

### 1. 下载安装包

前往 [**GitHub Releases**](https://github.com/TuPig/TuPigVNC/releases) 下载最新的 `TuPigVNC_x64.exe`。

> 主程序与安装器均为 **64 位 PE**，**不支持 32 位系统**。

### 2. 运行安装

双击安装包，按向导完成安装。安装程序会自动关闭并重启正在运行的旧实例，无需手动退出。

### 3. 连接远程桌面

1. 从开始菜单启动 **TuPigVNC**；
2. 在地址栏输入 `主机地址:端口`，例如 `192.168.1.100:5900`（VNC 默认端口 `5900`）；
3. 回车或点「连接」，按提示输入密码即可。

> 💡 需要「监听模式」接收连接时，使用开始菜单里的 **监听模式 TuPigVNC**（等价于 `-listen` 参数）。

### 4. 更新

程序内「检查更新」会访问 GitHub Releases 获取最新版本，下载后做 SHA-256 校验，校验通过即静默安装并自动重启，全程点一下就够。

---

## 从源码构建

以下命令与 [`.github/workflows/msvc.yml`](.github/workflows/msvc.yml) 的 CI 配置保持一致。

### 环境要求

| 依赖 | 版本 |
| :--- | :--- |
| Visual Studio | 2022（含 C++ 桌面开发工作负载） |
| CMake | ≥ 3.21 |
| vcpkg | 最新 |
| Inno Setup | 7（x64 版；仅构建安装包时需要） |

### 1. 安装 vcpkg 依赖

在源码根目录执行（本文假设 vcpkg 位于 `C:\vcpkg`）：

```bat
vcpkg install --triplet x64-windows-static ^
  zlib libjpeg-turbo pixman fltk nettle gtest "gettext[tools]"
```

### 2. 配置与编译

```bat
set VCPKG_ROOT=C:\vcpkg
set PATH=%VCPKG_ROOT%\installed\x64-windows-static\bin;%PATH%

cmake ^
  -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static ^
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded ^
  -DENABLE_NLS=ON ^
  -DENABLE_H264=ON ^
  -DENABLE_AUDIO=ON ^
  -DENABLE_GNUTLS=OFF ^
  -DENABLE_NETTLE=ON ^
  -DBUILD_VIEWER=ON ^
  -S . -B build

cmake --build build --config Release
```

> `-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` 表示静态 CRT。务必搭配 CMake ≥ 3.21，否则 `CMP0091` 仍为 `OLD`，该变量会被忽略并导致 `LNK2038` 链接错误。

### 3. 运行单元测试

```bat
ctest --test-dir build/tests/unit -C Release --output-on-failure
```

### 4. 构建安装包（可选）

先安装 [Inno Setup 7](https://jrsoftware.org/isinfo.php)（x64 版），然后：

```bat
cmake --build build --config Release --target installer
```

生成的安装包位于 `build\release\` 目录，文件名为 `TuPigVNC_x64.exe`。

> 更详细的各平台构建说明见 [`doc/BUILDING.txt`](doc/BUILDING.txt)。

---

## 目录结构

```text
TuPigVNC/
├── src/
│   ├── common/        # 公共静态库：core / network / rfb / rdr
│   ├── platform/      # 平台相关代码：windows / unix
│   ├── vncviewer/     # FLTK 查看器主程序（vncviewer.exe）
│   └── contrib/       # 第三方兼容层（MSVC 兼容头等）
├── assets/            # 品牌图标、UI 图标与安全状态图
├── i18n/              # gettext 翻译（zh_CN.po 等）
├── release/           # 打包脚本：Inno Setup / DMG / tarball
├── tests/             # 单元测试与性能测试
├── cmake/             # 自定义 CMake 模块
├── doc/               # 构建指南、性能与审查报告
└── User/              # 运行时生成：配置与连接历史（安装目录）
└── logs/              # 运行时生成：滚动日志（安装目录）
```

> 📌 `User/` 与 `logs/` 是程序**运行时在安装目录下自动创建**的，源码仓库里没有它们。

---

## 配置与常见问题

### 配置文件放在哪？

默认写在**安装目录的 `User` 文件夹**。如果安装目录不可写（例如装在 `Program Files` 且无权限），会自动回退到 `%LOCALAPPDATA%\TuPigVNC`。

| 文件 | 用途 |
| :--- | :--- |
| `User/history.json` | 连接历史（最多 250 台，按使用次数淘汰） |
| `User/` 目录 | 配置与状态数据根目录，由程序启动时自动创建 |

> Windows 上查看器的连接参数同时会写入注册表 `HKEY_CURRENT_USER\Software\TuPigVNC\vncviewer`。

### 日志在哪里？

日志写入 **`安装目录/logs/年月日时分秒.log`**。跨日或单个文件超过 20 MB 时自动切换新文件，不会无限膨胀。

> 排查连接问题时，把对应时间点的日志文件发到 issue 里会很有帮助。

### 为什么只有 64 位？

项目已彻底移除 32 位目标与 Java 客户端，构建、CI 与安装包均只面向 **Windows x64**（Linux / macOS 可编译源码，但非主要分发目标）。

### 需要装 VC++ 运行库吗？

不需要。发布版采用**静态 CRT** 链接，安装包为单文件，目标机器无需额外安装任何运行库。

### 自动更新卡住了怎么办？

自动更新走 GitHub Releases。若网络无法直连 GitHub，可手动下载最新 `TuPigVNC_x64.exe` 覆盖安装。安装程序会保留 `User` 之外的程序文件，并自动处理正在运行的实例。

### 「监听模式」是干什么的？

监听模式（`vncviewer -listen`）让本机作为服务端等待他人连接，适合需要被远程协助的场景。

---

## 许可证

TuPigVNC 依据 **GNU 通用公共许可证第 2 版（GPL-2.0）** 发布，完整条款见 [LICENCE.TXT](LICENCE.TXT)。

提交代码即表示同意以同一许可证发布你的贡献。

本项目基于 TigerVNC 上游代码与众多贡献者的成果，完整版权声明见 [LICENCE.TXT](LICENCE.TXT) 及各源文件头部。

---

## 贡献指南

欢迎参与！动手前请先看 [CONTRIBUTING.md](CONTRIBUTING.md)：

1. 先在 [Issues](https://github.com/TuPig/TuPigVNC/issues) 搜索是否已有相同问题；
2. Fork 仓库，从 `main` 拉特性分支：`git checkout -b feat/your-feature`；
3. 按代码规范（2 空格缩进、C++20、中文注释）编写，并补充测试；
4. 提交 Pull Request。

**提交信息格式**（中文）：

```text
type: 简短说明
```

`type` 取值：`feat`、`fix`、`docs`、`style`、`refactor`、`perf`、`test`、`build`、`ci`、`chore`。

> 发现安全漏洞请**不要**公开提 issue，按 [SECURITY.md](SECURITY.md) 的说明私下报告。

---

<div align="center">

**TuPigVNC** · 用 C++ 写的远程桌面，干净、直接、够用。

[项目主页](https://github.com/TuPig/TuPigVNC) · [问题反馈](https://github.com/TuPig/TuPigVNC/issues) · [版本发布](https://github.com/TuPig/TuPigVNC/releases) · [更新日志](CHANGELOG.md)

</div>
