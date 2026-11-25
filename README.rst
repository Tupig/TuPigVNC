================
TuPigVNC
================

.. image:: https://github.com/TuPig/TuPigVNC/actions/workflows/msvc.yml/badge.svg
   :target: https://github.com/TuPig/TuPigVNC/actions/workflows/msvc.yml
   :alt: 构建状态

.. image:: https://img.shields.io/github/v/release/TuPig/TuPigVNC
   :target: https://github.com/TuPig/TuPigVNC/releases
   :alt: 最新版本

.. image:: https://img.shields.io/github/license/TuPig/TuPigVNC
   :target: https://github.com/TuPig/TuPigVNC/blob/main/LICENCE.TXT
   :alt: 许可证


简介
====

TuPigVNC 是 TigerVNC 的中文品牌化版本，基于 TigerVNC 1.16 代码库进行现代化改造，
完全使用 C/C++ 实现。它提供高性能的 VNC 客户端与服务器，界面全中文，Windows x64
版本以静态单文件安装包分发，无需额外的 DLL 依赖。项目主页：
https://github.com/TuPig/TuPigVNC 。


特性
====

- **纯 C/C++ 实现**：删除 59,903 行 Java 代码，项目只保留 C 与 C++。
- **完整中文本地化**：931 条界面字符串、全部文档与 man 手册页翻译为简体中文。
- **自更新功能**：检查 GitHub Releases、自动下载、SHA-256 校验、静默安装并自动重启。
- **静态构建**：Windows x64 单文件安装包，无 DLL 依赖。
- **品牌图标**：基于品牌原图生成全套应用图标（PNG 16~512、ICO、ICNS、SVG）。
- **界面图标**：采用 Lucide 图标集（ISC 许可），菜单项带图标。
- **Apple 简约风主题**：现代圆角与柔和配色方案。
- **编译优化**：启用 LTO 链接时代码生成，可选 AVX2 指令集。
- **现代构建**：CMake 3.21 与 C++20，目录结构遵循 Modern CMake 布局。
- **工程化保障**：GitHub Actions 自动构建与发版、CodeQL 安全扫描、Dependabot 依赖更新。


快速开始
========

Windows 用户可以直接从 `GitHub Releases <https://github.com/TuPig/TuPigVNC/releases>`_
下载最新的 ``TuPigVNC64-<版本>.exe`` 安装包，双击后按向导完成安装。

安装完成后，通过开始菜单启动 TuPigVNC 查看器，填写服务器地址即可连接远程桌面。
如需更新，使用「检查更新」菜单：程序会访问 GitHub Releases 获取最新版本，下载后
进行 SHA-256 校验，校验通过则静默安装并自动重启。


从源码构建
==========

Windows x64 构建使用 MSVC 2022、CMake 3.21 或更高版本以及 vcpkg，依赖以
静态三元组 ``x64-windows-static`` 安装。以下命令与 ``.github/workflows/msvc.yml``
中的 CI 配置一致。

安装依赖（在源码根目录执行）::

    vcpkg install --triplet x64-windows-static ^
      zlib libjpeg-turbo pixman fltk nettle gtest "gettext[tools]"

配置与编译，假设 vcpkg 安装在 ``C:\vcpkg``：

.. code-block:: bat

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

运行单元测试：

.. code-block:: bat

    ctest --test-dir build/tests/unit -C Release --output-on-failure

构建 Windows 安装程序需要预先安装 Inno Setup 6，并执行：

.. code-block:: bat

    cmake --build build --config Release --target installer

生成的安装包位于 ``build\release`` 目录。更详细的各平台构建说明参见
`doc/BUILDING.txt <doc/BUILDING.txt>`_ 。


组件
====

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - 组件
     - 用途
   * - ``vncviewer``
     - 跨平台 TuPigVNC 查看器，使用 FLTK 编写，可连接 VNC 服务器并操作远程桌面。
   * - ``Xvnc``
     - Unix 平台的 VNC 服务器，同时是带虚拟帧缓冲的 X 服务器，通常由 vncserver 服务启动。
   * - ``x0vncserver``
     - 持续轮询任意 X 显示的 VNC 服务器，主要用于演示简单 VNC 服务器的实现方式。
   * - ``w0vncserver``
     - 面向 Wayland 合成器的 VNC 服务器，可控制本地 Wayland 会话。
   * - ``winvnc``
     - Windows 平台的 VNC 服务器，允许通过 VNC 查看器远程访问 Windows 桌面。
   * - ``vncconfig``
     - 配置和控制正在运行的 Xvnc 或 winvnc 实例。
   * - ``vncpasswd``
     - 设置访问 VNC 服务器会话所需的密码；启用 VNC 认证时，必须先运行它。


文档
====

- `doc/BUILDING.txt <doc/BUILDING.txt>`_ ：各平台构建指南。
- `doc/README.md <doc/README.md>`_ ：文档索引与技术报告清单。
- `CONTRIBUTING.md <CONTRIBUTING.md>`_ ：贡献流程与代码规范。
- `SECURITY.md <SECURITY.md>`_ ：安全策略与漏洞报告方式。
- `CHANGELOG.md <CHANGELOG.md>`_ ：版本变更记录。


贡献
====

参与开发前，先搜索已有 issue 以避免重复。提交代码的流程如下：

1. Fork 本仓库。
2. 从 ``main`` 创建特性分支，例如 ``git checkout -b feat/your-feature``。
3. 按 ``CONTRIBUTING.md`` 的代码规范编写代码，并补充或更新测试。
4. 提交 Pull Request。

提交信息使用中文，格式为 ``type: 简短说明``，类型包括 ``feat``、``fix``、``docs``、
``refactor``、``perf``、``test``、``build``、``ci``、``chore`` 等。

如果发现安全漏洞，**不要**通过公开 issue 报告，请按 `SECURITY.md <SECURITY.md>`_
的说明私下提交。


许可证
======

TuPigVNC 依据 GNU 通用公共许可证第 2 版（GPL-2.0）发布，完整条款见
`LICENCE.TXT <LICENCE.TXT>`_ 。提交代码即表示同意以同一许可证发布你的贡献。


致谢
====

TuPigVNC 基于 TigerVNC 项目，并继承其上游代码与贡献者成果。以下版权清单并不完整，
且通常未及时更新::

        Copyright (C) 1999 AT&T Laboratories Cambridge
        Copyright (C) 2002-2005 RealVNC Ltd.
        Copyright (C) 2000-2006 TightVNC Group
        Copyright (C) 2005-2006 Martin Koegler
        Copyright (C) 2005-2006 Sun Microsystems, Inc.
        Copyright (C) 2006 OCCAM Financial Technology
        Copyright (C) 2000-2008 Constantin Kaplinsky
        Copyright (C) 2004-2017 Peter Astrand for Cendio AB
        Copyright (C) 2010 Antoine Martin
        Copyright (C) 2010 m-privacy GmbH
        Copyright (C) 2009-2011 D. R. Commander
        Copyright (C) 2009-2011 Pierre Ossman for Cendio AB
        Copyright (C) 2004, 2009-2011 Red Hat, Inc.
        Copyright (C) 2009-2026 TuPigVNC team
        All Rights Reserved.

本软件依据自由软件基金会发布的 GNU 通用公共许可证分发。有关本软件的许可条件，
请参阅 `LICENCE.TXT <LICENCE.TXT>`_ 文件。TuPigVNC 还包含来自其他来源的代码，
其许可条件详见各源文件中的说明。
