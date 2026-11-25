# 更新日志

本文件记录 TuPigVNC 的所有重要变更。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [0.0.1] - 2026-11-25

首个正式版本。基于 TigerVNC 1.16 代码库进行品牌化与现代化改造。

### 新增

- **品牌化**：项目更名为 TuPigVNC，官网地址指向 https://github.com/TuPig/TuPigVNC
- **完整中文本地化**：931 条界面字符串、全部文档与 man 手册页翻译为简体中文
- **自更新功能**：检查 GitHub Releases → 自动下载 → SHA-256 校验 → 静默安装 → 自动重启
- **品牌图标**：基于品牌原图生成全套应用图标（PNG 16~512、ICO、ICNS、SVG）
- **界面图标**：采用 Lucide 图标集（ISC 许可），菜单项带图标
- **Apple 简约风主题**：现代圆角、柔和的配色方案
- **静态构建**：Windows x64 单文件安装包，无 DLL 依赖
- **编译优化**：启用 LTO 链接时代码生成，可选 AVX2 指令集

### 变更

- 项目目录结构重构为标准 Modern CMake 布局（`src/`、`assets/`、`i18n/`）
- 版本号采用语义化版本（SemVer），从 0.0.1 起步
- 平台代码归拢至 `src/platform/`（unix、windows）
- Windows 构建改为仅 x64 静态链接

### 修复

- FLTK 在磁盘根目录（`C:\FLTK`）创建配置文件的问题（上游 #822）
- `zh_CN.po` 中 msgid 与 msgstr 换行不一致导致 `msgfmt` 编译失败

### 移除

- **Java 客户端**：删除 59,903 行 Java 代码，项目纯 C/C++
- **Linux 容器构建脚本**：仅保留 Windows 构建
- **Python/Shell 辅助脚本**：重写为 C++ 工具

### 工程化

- GitHub Actions：Windows x64 自动构建、打 tag 自动发布安装包
- CodeQL 安全扫描、Dependabot 依赖更新
- Secret Scanning、推送保护、分支保护
- 文档体系：上游 issue 分类、项目审查、安全审计、性能优化报告

---

[0.0.1]: https://github.com/TuPig/TuPigVNC/releases/tag/v0.0.1
