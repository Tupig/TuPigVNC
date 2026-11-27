# 贡献指南

感谢你对 TuPigVNC 的关注！本文档说明如何参与项目开发。

## 目录

- [行为准则](#行为准则)
- [如何贡献](#如何贡献)
- [开发环境](#开发环境)
- [代码规范](#代码规范)
- [提交规范](#提交规范)
- [测试要求](#测试要求)
- [文档规范](#文档规范)

---

## 行为准则

参与本项目即表示你同意遵守[贡献者公约](https://www.contributor-covenant.org/zh-cn/version/2/1/code_of_conduct/)。

## 如何贡献

### 报告缺陷

1. 先搜索[已有 issue](https://github.com/TuPig/TuPigVNC/issues)，避免重复
2. 使用[缺陷报告模板](ISSUE_TEMPLATE/bug_report.md)
3. 提供**可复现的最小示例**、操作系统与版本信息

### 提出功能请求

1. 使用[功能请求模板](ISSUE_TEMPLATE/feature_request.md)
2. 说明**使用场景**与**期望行为**

### 提交代码

1. Fork 本仓库
2. 从 `main` 创建特性分支：`git checkout -b feat/your-feature`
3. 按[代码规范](#代码规范)编写代码
4. 补充或更新测试
5. 提交 Pull Request

## 开发环境

### 依赖

| 依赖 | 版本 | 用途 |
|------|------|------|
| CMake | ≥ 3.10 | 构建系统 |
| C++ 编译器 | 支持 C++20 | MSVC 2022 / GCC 11+ / Clang 14+ |
| vcpkg | 最新 | 依赖管理 |

### 构建（Windows x64）

以下命令在仓库根目录执行。原生构建产物建议放在 `tmp/build/native`，CI 使用
`tmp/build/ci-native`；构建目录内的 `tests/unit`、`release` 等子布局不变。
完整说明见[构建指南](../doc/build/BUILDING.txt)。

```bash
cmake -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake \
  -DVCPKG_TARGET_TRIPLET=x64-windows-static \
  -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded \
  -S . -B tmp/build/native
cmake --build tmp/build/native --config Release
```

### 运行测试

```bash
ctest --test-dir tmp/build/native/tests/unit -C Release --output-on-failure
```

## 代码规范

### 语言标准

- **C++20**（`-std=c++20` / `/std:c++20`）
- **C17**（`-std=gnu17` / `/std:c17`）

### 命名约定

| 类型 | 风格 | 示例 |
|------|------|------|
| 类名 | 大驼峰 | `UpdateChecker` |
| 函数/变量 | 小驼峰 | `fetchLatestRelease` |
| 成员变量 | 小驼峰 | `downloadUrl` |
| 常量 | 全大写下划线 | `MAX_BUFFER_SIZE` |
| 头文件保护 | `__模块_文件_H__` | `__CORE_UPDATECHECKER_H__` |

### 代码风格

- **缩进**：2 个空格（不用 Tab）
- **行宽**：≤ 80 列（注释 ≤ 72 列）
- **花括号**：函数使用 Allman 风格（左括号独占一行），控制语句使用 K&R 风格
- **指针**：`int* ptr`（星号靠类型）
- **命名空间**：`core`、`rfb`、`network`、`rdr`

示例：

```cpp
bool UpdateChecker::downloadFile(const std::string& url,
                                 const std::string& destPath,
                                 std::string* error)
{
    if (url.empty()) {
        if (error != nullptr)
            *error = "空网址";
        return false;
    }

    // ...
    return true;
}
```

### 现代 C++ 用法

优先使用现代 C++ 特性：

```cpp
// ✅ 推荐
std::string_view name = getname();
auto it = std::find(vec.begin(), vec.end(), key);
std::optional<int> value = parse(input);
for (const auto& item : items) { }

// ❌ 避免
const char* name = getname();
for (int i = 0; i < vec.size(); ++i) { }
```

### 国际化

所有面向用户的字符串必须用 `_()` 包裹（gettext）：

```cpp
fl_alert(_("Update failed: %s"), error.c_str());
```

翻译文件位于 `src/i18n/zh_CN.po`，修改后运行：

```bash
xgettext ... # 提取新字符串
msgmerge -U src/i18n/zh_CN.po src/i18n/tupigvnc.pot
```

## 提交规范

### 提交信息格式

```
<类型>: <简短说明>

<可选正文>

Co-Authored-By: <贡献者>
```

**类型**：

| 类型 | 说明 |
|------|------|
| `feat` | 新功能 |
| `fix` | 缺陷修复 |
| `docs` | 文档更新 |
| `style` | 代码格式（不影响逻辑） |
| `refactor` | 重构（不新增功能、不修缺陷） |
| `perf` | 性能优化 |
| `test` | 测试相关 |
| `build` | 构建系统或依赖 |
| `ci` | CI 配置 |
| `chore` | 杂项 |

**示例**：

```
fix: 修复 FLTK 在磁盘根目录创建配置文件

将 FLTK_PREFS_ROOT 重定向到 %APPDATA%\TuPigVNC，
避免在 C:\FLTK\fltk.org\fltk.prefs 生成配置文件。

Co-Authored-By: Claude Code <noreply@anthropic.com>
```

### 提交粒度

- 一个提交**只做一件事**
- 提交前确保**可编译**
- 提交信息用**中文**描述

## 测试要求

| 测试类型 | 要求 |
|---------|------|
| 单元测试 | 新增功能必须补充 |
| 集成测试 | 修改协议/网络代码时需补充 |
| 性能测试 | 修改编解码路径时需对比 `src/tests/perf` |

**提交前自查**：

```bash
# 构建
cmake --build tmp/build/native --config Release

# 单元测试
ctest --test-dir tmp/build/native/tests/unit -C Release --output-on-failure

# 静态分析（CI 自动运行 CodeQL）
```

## 文档规范

- 面向用户的文档使用**简体中文**
- 技术术语保留英文（如 API、CMake、VNC）
- 代码注释使用**中文**说明
- 文档格式：Markdown（`.md`）或 reStructuredText（`.rst`）

### 文档位置

| 类型 | 位置 |
|------|------|
| 项目介绍 | [`doc/README.rst`](../doc/README.rst) |
| 构建指南 | [`doc/build/BUILDING.txt`](../doc/build/BUILDING.txt) |
| 更新日志 | [`doc/CHANGELOG.md`](../doc/CHANGELOG.md) |
| 文档索引 | [`doc/README.md`](../doc/README.md) |
| 技术报告 | `doc/reviews/`、`doc/upstream/` |
| 历史记录 | `doc/history/` |
| 测试文档 | `doc/testing/` |
| API 文档 | 头文件注释 |

## 许可证

提交代码即表示你同意以 [GPL-2.0](../LICENCE.TXT) 许可证发布你的贡献。

---

<div align="center">

**有问题？** [提交 Issue](https://github.com/TuPig/TuPigVNC/issues)

</div>
