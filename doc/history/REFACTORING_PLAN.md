# 重构实施计划

> 生成时间：2026-11-25
> 当前版本：v0.0.1
> 状态：阶段 1、3 已完成；阶段 2 已启动

## 总览

| 阶段 | 内容 | 工期 | 风险 | 状态 |
|------|------|------|------|------|
| 1 | 目录结构标准化 | — | 低 | ✅ 完成 |
| 2 | 平台抽象层迁移（24 个文件） | 2~3 周 | 中 | 🔄 进行中 |
| 3 | 日志路径统一 | — | 低 | ✅ 完成 |
| 4 | 现代 C++ 迁移 | 1~2 月 | 中高 | ⏳ 待启动 |
| 5 | CMake target 模块化 | 2~3 月 | 高 | ⏳ 待启动 |

**不动的部分**：RFB 协议层、编解码器、安全层（TLS/VeNCrypt）。

---

## 阶段 2：平台抽象层迁移

### 已建立的基础

`src/common/core/Platform.h` 定义了统一接口：

```cpp
class Platform {
public:
    virtual std::string executablePath() const = 0;
    virtual std::string tempDirectory() const = 0;
    virtual bool openInBrowser(const std::string& url) const = 0;
    virtual unsigned int cpuCount() const = 0;
    virtual const char* name() const = 0;
};
Platform& getPlatform();
```

### 迁移清单（24 个文件，按优先级）

#### P0：高价值、低风险（先做）

| 文件 | 平台代码 | 迁移方式 |
|------|---------|---------|
| `src/vncviewer/vncviewer.cxx` | 11 处 | 窗口/图标/SSH 路径 → Platform |
| `src/vncviewer/parameters.cxx` | 4 处 | 注册表路径 → Platform::registryPath() |
| `src/common/network/Socket.cxx` | 3 处 | 错误码 → Platform::socketError() |

#### P1：中等价值

| 文件 | 平台代码 | 迁移方式 |
|------|---------|---------|
| `src/vncviewer/ServerDialog.cxx` | 3 处 | 文件对话框 → Platform |
| `src/common/network/TcpSocket.cxx` | 2 处 | 套接字选项 → Platform |
| `src/vncviewer/fltk/theme.cxx` | 2 处 | 字体加载 → Platform |
| `src/common/rfb/SSecurityRSAAES.cxx` | 2 处 | 随机源 → Platform |
| `src/common/rfb/H264DecoderContext.cxx` | 2 处 | 硬件解码 → Platform |

#### P2：低优先（错误消息等）

| 文件 | 说明 |
|------|------|
| `src/common/core/Exception.cxx` | 错误码转换（平台特定，保留现状） |
| `src/common/rfb/SSecurityPlain.cxx` | 认证（协议规定） |
| 其余 15 个文件 | 分散的小改动 |

### 迁移模式

**改造前**：
```cpp
#ifdef WIN32
    path = getenv("APPDATA");
#else
    path = getenv("HOME");
#endif
```

**改造后**：
```cpp
    path = core::getPlatform().configPath();
```

### 验证方法

每个文件迁移后：
1. 本地 `g++ -std=c++20 -fsyntax-only` 检查
2. CI 完整构建（Windows）
3. 单元测试通过
4. 手动 smoke test（连接 VNC）

### 本地 MSVC 快速验证

CI 单次构建约 40 分钟，迭代太慢。本机装有 Visual Studio 2022
BuildTools，可先用它做语法检查（`/Zs` 只解析，不生成目标文件）：

```bat
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d G:\VNCView
cl /nologo /std:c++20 /utf-8 /EHsc /Zs /I src\common /I src\contrib\msvc\include /FI msvc_compat.h %1
```

把上面存成 `log\msvc_check.bat`，然后 `cmd //c "G:\VNCView\log\msvc_check.bat 源文件.cxx"`。

注意：
- 依赖 pixman、gettext 的文件（`Region.cxx`、`i18n.cxx`）本地会因
  缺头文件失败，属正常现象，以 CI 为准。
- `Unix*.cxx`、`X11*.cxx` 等 Unix 专用文件在 Windows 上不参与编译。

---

## 阶段 4：现代 C++ 迁移

### 4.1 已升级

- C++ 标准：C++11 → **C++20**
- C 标准：C99 → **C17**

### 4.2 迁移清单

#### P0：`std::string_view`（参数传递）

```cpp
// 之前
void setLogParams(const char* params);
void setLogParams(const std::string& params);

// 之后
void setLogParams(std::string_view params);
```

**目标文件**：`LogWriter.h`、`Configuration.h`、`parameters.h`

**收益**：消除临时 `std::string` 构造，减少内存分配。

#### P1：`std::filesystem`（路径处理）

```cpp
// 之前
std::string path = dir + "/" + name;
if (stat(path.c_str(), &st) != 0) { ... }

// 之后
auto path = std::filesystem::path(dir) / name;
if (!std::filesystem::exists(path)) { ... }
```

**目标文件**：`xdgdirs.cxx`、`Logger_file.cxx`、`parameters.cxx`

**注意**：Windows 上 `std::filesystem` 需要 `char8_t` 或宽字符转换。

#### P1：`std::optional`（返回值）

```cpp
// 之前
bool getSetting(const char* name, std::string* value);

// 之后
std::optional<std::string> getSetting(std::string_view name);
```

#### P2：`std::span`（缓冲传递）

```cpp
// 之前
void write(const uint8_t* data, size_t len);

// 之后
void write(std::span<const uint8_t> data);
```

**目标**：网络层、编解码器接口（**谨慎**，性能敏感）。

#### P2：结构化绑定、`if constexpr`

```cpp
for (const auto& [key, value] : settings) { ... }
if constexpr (std::is_same_v<T, uint8_t>) { ... }
```

### 4.3 注意事项

| 风险 | 说明 |
|------|------|
| `char8_t` | C++20 引入，`u8"..."` 类型变化，需检查 |
| `std::filesystem` | 需要链接 `stdc++fs`（GCC 8 以下） |
| ABI 兼容 | 库接口变化影响下游 |

### 4.4 验证

- 每批迁移后 CI 完整构建
- 性能基准：`tests/perf` 对比迁移前后

---

## 阶段 5：CMake target 模块化

### 5.1 当前结构

```cmake
core (静态库)      <- 工具、日志
rdr (静态库)       <- 流
network (静态库)   <- 套接字
rfb (静态库)       <- 协议
rfbclient (动态库) <- 客户端
rfbserver (动态库) <- 服务端
vncviewer (可执行) <- 查看器
```

### 5.2 目标结构

```cmake
tupigvnc::core       <- 现代 CMake target 命名空间
tupigvnc::network
tupigvnc::protocol
tupigvnc::client
tupigvnc::server
tupigvnc::viewer
```

### 5.3 改造步骤

1. **添加命名空间**：
   ```cmake
   add_library(tupigvnc_core STATIC ...)
   add_library(tupigvnc::core ALIAS tupigvnc_core)
   ```

2. **导出配置**：
   ```cmake
   install(TARGETS tupigvnc_core EXPORT TuPigVNCTargets ...)
   install(EXPORT TuPigVNCTargets NAMESPACE tupigvnc:: ...)
   ```

3. **使用要求**：
   ```cmake
   target_link_libraries(vncviewer PRIVATE tupigvnc::core tupigvnc::client)
   ```

4. **包配置**：
   ```cmake
   include(CMakePackageConfigHelpers)
   configure_package_config_file(...)
   ```

### 5.4 收益

- 下游可 `find_package(TuPigVNC)`
- 依赖关系显式化
- 支持安装后复用

### 5.5 风险

- 改动面大（每个 CMakeLists）
- 需要保持向后兼容（旧 target 名）

---

## 执行建议

### 时间分配

| 会话 | 内容 |
|------|------|
| 第 1 次 | 阶段 2 P0（3 个文件） |
| 第 2 次 | 阶段 2 P1（5 个文件） |
| 第 3 次 | 阶段 4 P0（`string_view`） |
| 第 4 次 | 阶段 4 P1（`filesystem`/`optional`） |
| 第 5 次 | 阶段 5（CMake 命名空间） |

### 每次会话的节奏

1. 选 2~3 个文件迁移
2. 本地语法检查
3. 提交 + 推送
4. 等 CI 通过
5. 继续下一批

### 质量门禁

- ✅ 每次提交必须可编译
- ✅ 单元测试必须通过
- ✅ CodeQL 不得新增告警
- ✅ 性能不得回退（`tests/perf` 对比）

---

## 当前进度

| 项目 | 状态 |
|------|------|
| 阶段 1 目录结构 | ✅ |
| 阶段 2 抽象层接口 | ✅ |
| 阶段 2 迁移 | 🔄 3/24 |
| 阶段 3 日志统一 | ✅ |
| 阶段 4 C++20 | ✅（标准已升级，代码待迁移） |
| 阶段 5 模块化 | ⏳ |

**已完成迁移**：

| 文件 | 内容 |
|------|------|
| `parameters.cxx` | 4 处注册表路径硬编码 → 常量 |
| `PlatformWin32.cxx` / `UpdateCheckerWin32.cxx` | `widen`/`narrow` 与 `executablePath` 去重 |
| `xdgdirs.cxx` | `mkdir_p` 改用 `std::filesystem`，修复 Windows 多级目录 |
| `vncviewer.cxx` | `FLTK_PREFS_ROOT` 改用 `core::getvncconfigdir()` |

**vncviewer.cxx 逐处评估（13 处 `#ifdef`）**：

| 行 | 内容 | 结论 |
|----|------|------|
| 37-40 | `<core/winerrno.h>`、`<direct.h>` | 保留：Windows 专有头文件 |
| 71-74 | `resource.h`、`win32.h` | 保留：资源与 Win32 辅助 |
| 266-320 | 图标加载（`LoadImage` vs PNG 文件） | 保留：FLTK 层差异，非通用平台抽象 |
| 343-347 | `Fl_File_Chooser::filesystems_label` | 保留：属于 i18n 范畴 |
| 382-394 | `AllocConsole` 控制台创建 | 保留：GUI 子系统专有逻辑 |
| 396-413 | usage 文本差异（displayNum vs unix socket） | 保留：协议能力差异 |
| 432-435 | `Sleep(INFINITE)` | 保留：等待用户关闭控制台 |
| 447-457 | Unix socket 检测 | 保留：平台专有 |
| 482-490 | 弃用提示的路径分隔符 | 保留：文本差异 |
| 523-562 | `getenv("VNC_VIA_CMD")` | 保留：跨平台环境变量 |
| 570-583 | `FLTK_PREFS_ROOT` | **已迁移** |
| 675-695 | `-via`/`-listen` 互斥检查 | 保留：Unix 专有 |
| 740-760 | `mktunnel()` | 保留：Unix 专有 |

**评估后保留（不迁移）**：

| 文件 | 原因 |
|------|------|
| `Socket.cxx` | `errorNumber`/`closesocket`/`SHUT_RD` 是 C 层宏，热路径虚函数化反而增加开销 |
| `vncviewer.cxx` 其余 12 处 | 见上表：语音差异、FLTK 层、协议能力各不相同，抽象后只会更难读 |

**结论**：阶段 2 的有效迁移已全部完成。剩余 `#ifdef` 属于「平台差异本就该存在」的范畴，
继续机械抽象会降低可读性并引入间接调用开销。
