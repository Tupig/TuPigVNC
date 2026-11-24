# TuPigVNC 项目审查与重构建议

> 生成时间：2026-11-24
> 范围：品牌化改造后的代码库（纯 C/C++）

## 一、代码规模

| 类别 | 文件数 | 行数（约） |
|------|-------:|-----------:|
| C/C++ 源码 | 330 | 44,000 |
| 翻译文件（.po） | 31 | 约 9,300 条（中文 931 条） |
| 构建脚本（CMake） | 21 | — |
| 文档 | — | 约 1,200 行 |

## 二、发现的问题

### 2.1 硬编码绝对路径（5 处）

| 位置 | 内容 | 评估 |
|------|------|------|
| `unix/xserver/hw/vnc/xvnc.c:246` | `/usr/spool/sockets/X11/%d` | X11 标准路径，可接受 |
| `vncviewer/vncviewer.cxx:537` | `/usr/bin/ssh` | 可通过 `VNC_VIA_CMD` 环境变量覆盖，可接受 |
| `vncviewer/vncviewer.cxx:598` | `C:\temp`（日志回退） | 建议改用 `%LOCALAPPDATA%\TuPigVNC\logs` |
| `win/winvnc/winvnc.cxx:258,261` | `C:\temp\WinVNC4.log` | 建议改用用户配置目录 |

**建议**：统一通过 `core::xdgdirs` 或 Windows `SHGetKnownFolderPath` 获取日志目录。

### 2.2 代码格式

- **行尾空白**：1,430 处（上游历史遗留）
- **连续空行（3 行以上）**：4 处
- **TODO/FIXME 标记**：169 处

**建议**：
1. 引入 `.clang-format` 统一格式（TigerVNC 上游已有代码风格）
2. 添加 `pre-commit` 钩子自动清理行尾空白
3. 逐步梳理 169 处 TODO/FIXME，转为 issue 跟踪

### 2.3 死代码与重复

- C++ 版本 `vncviewer`（20,140 行）与已删除的 Java 版本存在大量重复逻辑
- `common/rfb` 中的编码器/解码器接口存在模板化重复
- 部分平台特定代码（Win32/X11/macOS）可通过抽象层收敛

### 2.4 国际化

- 目前仅有 `zh_CN` 完整翻译（931 条）
- 其余 30 个语言文件仍为上游内容，品牌名已同步替换
- **建议**：将 `tigervnc.pot` 重命名为 `tupigvnc.pot`，统一 gettext domain

### 2.5 构建

- 当前仅 Windows x64 参与 CI
- 依赖通过 vcpkg 管理，版本未锁定（建议添加 `vcpkg.json` 清单模式）

## 三、重构方向建议

### 3.1 目录结构精简

当前结构（`common/`、`unix/`、`win/`、`vncviewer/`）是上游历史遗留，建议：

```
src/
  core/          核心工具（日志、配置、字符串）
  network/       TCP/Unix 套接字
  protocol/      RFB 协议（原 rfb/）
  client/        查看器（原 vncviewer/）
  server/
    unix/        Xvnc、x0vncserver、w0vncserver
    windows/     winvnc
  platform/      平台抽象层
```

**收益**：消除 `unix/` 与 `win/` 的割裂，平台代码集中管理。

**成本**：改动面大，需要同步更新 CMake、include 路径、打包脚本。

### 3.2 平台抽象层

当前 Win32/X11/macOS 代码通过 `#ifdef` 分散在各文件中。建议：

```cpp
// platform/Platform.h
class Platform {
public:
    virtual std::string configDir() = 0;
    virtual std::string logDir() = 0;
    virtual bool openBrowser(const std::string& url) = 0;
    // ...
};
```

**收益**：新增平台（如 Wayland 客户端）时无需修改核心代码。

### 3.3 现代 C++ 迁移

- 当前代码基线为 C++11，可迁移至 C++17/20
- 用 `std::string_view` 替换 `const char*` 参数
- 用 `std::filesystem` 替换手写路径处理
- 用 `std::optional` 替换返回码 + 输出参数

**收益**：减少样板代码，提升可读性与安全性。

### 3.4 测试体系

- 当前仅有 `tests/unit`（gtest）与 `tests/perf`
- **建议**：
  1. 增加协议层单元测试（编码/解码）
  2. 增加集成测试（启动服务器 + 客户端连接）
  3. 引入模糊测试（libFuzzer）针对协议解析

### 3.5 自更新架构

当前实现：
- Windows 通过 WinHTTP + 批处理脚本完成更新
- Unix 仅下载到临时目录

**改进方向**：
1. 引入增量更新（bsdiff）减少下载量
2. 签名验证（确保安装包未被篡改）
3. 回滚机制（更新失败自动还原）
4. 后台静默检查（定期检查，有更新时提示）

### 3.6 安全加固

- 启用编译时加固：`/GS`、`/guard:cf`（MSVC）、`-fstack-protector`（GCC）
- 启用 ASLR 与 DEP（Windows 默认开启）
- 签名验证：安装包与自更新包均需签名
- 依赖审计：定期运行 `vcpkg` 漏洞检查

### 3.7 构建与发布

- **vcpkg 清单模式**：添加 `vcpkg.json` 锁定依赖版本
- **构建缓存**：GitHub Actions 中缓存 vcpkg 与 CMake 构建产物
- **多配置**：支持 Debug/Release/RelWithDebInfo
- **签名**：发布前自动签名安装包

## 四、优先级建议

| 优先级 | 项目 | 预估工作量 |
|--------|------|-----------|
| P0 | 日志路径统一（消除 `C:\temp`） | 1 天 |
| P0 | 自更新签名验证 | 2 天 |
| P1 | 引入 `.clang-format` + pre-commit | 1 天 |
| P1 | 目录结构精简（3.1） | 2 周 |
| P2 | 平台抽象层（3.2） | 3 周 |
| P2 | 现代 C++ 迁移（3.3） | 1 个月 |
| P3 | 增量更新 + 回滚（3.5） | 2 周 |

## 五、结论

TuPigVNC 在品牌化与汉化后，已具备完整的 Windows 客户端能力。当前主要技术债来自上游的历史遗留（目录结构、平台代码分散、格式不统一）。

建议按「**先稳定、再重构**」的节奏推进：
1. 先完成自更新的签名与回滚（用户可感知的质量提升）
2. 再逐步引入格式化工具与测试体系（工程基础）
3. 最后进行目录结构与抽象层的重构（架构升级）
