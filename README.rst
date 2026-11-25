关于 TuPigVNC
==============

虚拟网络计算（Virtual Network Computing，VNC）是一种远程显示系统，允许用户查看并操作运行在网络中另一台计算机上的虚拟桌面环境。借助 VNC，用户可以在远程计算机上运行图形应用程序，并仅将这些应用程序的显示内容发送至本地计算机。VNC 与平台无关，可作为服务器与客户端运行于多种操作系统和体系结构之上。

TuPigVNC 是基于 RealVNC 4 与 X.org 代码库构建的高速 VNC 实现。TuPigVNC 最初是 TightVNC 在 Unix 与 Linux 平台上的下一代开发工作，后于 2009 年初从父项目中分离，以便 TightVNC 专注于 Windows 平台。TuPigVNC 支持一种经过改进的 Tight 编码，借助 libjpeg-turbo JPEG 编解码器实现了显著加速。

项目主页：https://github.com/TuPig/TuPigVNC


法律声明
========

以下版权清单并不完整，且通常未及时更新::

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

本软件依据自由软件基金会发布的 GNU 通用公共许可证分发。有关本软件的许可条件，请参阅 LICENCE.TXT 文件。TuPigVNC 还包含来自其他来源的代码，其许可条件详见下文“致谢”部分及各源文件中的说明。


全平台组件
==========

TuPigVNC 的所有版本均包含以下程序：

* vncviewer - 跨平台的 TuPigVNC 查看器，使用 FLTK 编写。
              vncviewer 可连接至 VNC 服务器，并与服务器所显示的远程桌面进行交互。
              VNC 服务器可运行于 Windows 或 Unix/Linux 计算机上。


Windows 平台组件
================

TuPigVNC 的 Windows 版本包含以下程序：

* winvnc - 适用于 Windows 的 TuPigVNC 服务器。winvnc 允许通过 VNC 查看器远程访问 Windows 桌面。

警告：winvnc 当前已停止维护，可能出现功能异常。

若启用了快速用户切换或远程桌面功能，winvnc 可能无法正常工作。


Unix/Linux 平台组件（不含 Mac）
===============================

TuPigVNC 的 Unix/Linux 版本包含以下程序：

* Xvnc - 适用于 Unix 的 TuPigVNC 服务器。Xvnc 同时是 VNC 服务器和带有“虚拟”帧缓冲的 X 服务器。通常应通过 vncserver 服务来启动 Xvnc。

* vncpasswd - 用于更改访问 VNC 服务器会话所需 VNC 密码的程序（前提是启用了 VNC 认证）。在使用任何服务器或服务进行 VNC 认证之前，必须先运行此命令以设置密码。

* vncconfig - 用于配置和控制正在运行的 Xvnc 实例的程序。

* x0vncserver - 一种效率较低的 VNC 服务器，持续轮询任意 X 显示，从而可通过 VNC 进行控制。它主要用作简单 VNC 服务器的示范。

* w0vncserver - 适用于 Wayland 合成器的 VNC 服务器，可控制本地 Wayland 会话。

此外还包含以下 systemd 服务：

* vncserver@.service - 用于结合 Xvnc 与系统上可用的桌面环境之一来启动用户会话的服务。


致谢
====

本发行版包含 Richard Outerbridge 的公有领域 DES 软件，其版权声明如下：

    Copyright (c) 1988,1989,1990,1991,1992 by Richard Outerbridge.
    (GEnie : OUTER; CIS : [71755,204]) Graven Imagery, 1992.


本发行版包含来自 X Window System 的软件，其许可声明如下（法律条文保留原文）：

 Copyright 1987, 1988, 1998  The Open Group

 Permission to use, copy, modify, distribute, and sell this software and its
 documentation for any purpose is hereby granted without fee, provided that
 the above copyright notice appear in all copies and that both that
 copyright notice and this permission notice appear in supporting
 documentation.

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

 Except as contained in this notice, the name of The Open Group shall not be
 used in advertising or otherwise to promote the sale, use or other dealings
 in this Software without prior written authorization from The Open Group.


 Copyright 1987, 1988 by Digital Equipment Corporation, Maynard, Massachusetts.

                         All Rights Reserved

 Permission to use, copy, modify, and distribute this software and its
 documentation for any purpose and without fee is hereby granted,
 provided that the above copyright notice appear in all copies and that
 both that copyright notice and this permission notice appear in
 supporting documentation, and that the name of Digital not be
 used in advertising or publicity pertaining to distribution of the
 software without specific, written prior permission.

 DIGITAL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE, INCLUDING
 ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL
 DIGITAL BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR
 ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION,
 ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 SOFTWARE.
