# TuPigVNC 性能优化专项

> 生成时间：2026-11-25
> 版本：v0.0.1

## 一、已实施的优化

### 1.1 编译优化

| 优化项 | 状态 | 说明 |
|--------|------|------|
| `/O2` 优化 | ✅ 默认 | Release 构建默认开启 |
| **LTO（链接时代码生成）** | ✅ 已启用 | `/GL` + `/LTCG`，跨编译单元内联 |
| AVX2 指令集 | ⚙️ 可选 | `-DENABLE_AVX2=ON` 启用（需较新 CPU） |
| 静态链接 | ✅ 已启用 | 单文件发布，消除 DLL 加载开销 |

### 1.2 代码级优化（上游已具备）

| 模块 | 优化 | 说明 |
|------|------|------|
| `fillRect` | 行级 `memcpy` | 首行填充后按行复制，避免逐像素 |
| `imageRect` | 批量复制 | 按行 `memcpy`，无逐像素循环 |
| Tight 解码 | zlib 流复用 | 4 个独立 zlib 流，避免重建 |
| JPEG 解码 | 直接解码 | `getBufferRW` 直接写入帧缓冲，无中间拷贝 |

## 二、性能关键路径

### 2.1 客户端（vncviewer）

```
网络接收 → Tight/ZRLE/JPEG 解码 → 像素格式转换 → 帧缓冲写入 → 界面刷新
```

| 阶段 | 热点文件 | 开销占比（估算） |
|------|---------|----------------|
| 解码 | `common/rfb/TightDecoder.cxx` | 35% |
| 解压 | `common/rdr/ZlibInStream.cxx` | 25% |
| 像素转换 | `common/rfb/PixelBuffer.cxx` | 20% |
| 网络 I/O | `common/network/TcpSocket.cxx` | 15% |
| 界面刷新 | `vncviewer/PlatformPixelBuffer.cxx` | 5% |

### 2.2 服务器（vncserver）

```
屏幕捕获 → 差分检测 → 编码（Tight/JPEG）→ 发送
```

| 阶段 | 热点文件 | 开销占比（估算） |
|------|---------|----------------|
| 编码 | `common/rfb/TightEncoder.cxx` | 40% |
| 压缩 | `common/rdr/ZlibOutStream.cxx` | 30% |
| 屏幕捕获 | `platform/unix/xserver/hw/vnc/*` | 20% |
| 差分 | `common/rfb/EncodeManager.cxx` | 10% |

## 三、进一步优化建议

### 3.1 短期（低风险）

| 优化 | 预期收益 | 实现难度 |
|------|---------|---------|
| AVX2 编译 | 5~15% | 低（已提供开关） |
| PGO（配置文件引导优化） | 10~20% | 中（需采集 profile） |
| 大页内存（2MB pages） | 5~10% | 低 |

**AVX2 启用方式**：
```bash
cmake -DENABLE_AVX2=ON ...
```

**PGO 流程**：
1. 插桩编译：`/GL /prof-gen`
2. 运行典型负载
3. 优化编译：`/GL /prof-use`

### 3.2 中期（中风险）

| 优化 | 预期收益 | 说明 |
|------|---------|------|
| SIMD 像素转换 | 20~40% | 用 AVX2 重写 `bufferFromRGB`/`bufferToRGB` |
| 多线程解码 | 30~50% | 参考 `DecodeManager`，按矩形分片 |
| 零拷贝网络 | 10~15% | 减少 socket 读取的中间缓冲 |

### 3.3 长期（架构级）

| 优化 | 预期收益 | 说明 |
|------|---------|------|
| GPU 硬件解码 | 50~80% | 用 Direct3D/OpenGL 做 YUV 转换 |
| 增量更新（bsdiff） | 带宽 -60% | 自更新下载优化 |
| 自适应码率 | 体验提升 | 根据 RTT/丢包动态调整质量 |

## 四、基准测试方法

项目自带 `tests/perf` 性能测试工具：

```bash
# 生成测试数据
genrfb ui test.rfb --width 1280 --height 720 --frames 300

# 测试各编码性能
encperf encoding=Tight width=1280 height=720 format=rgb888 test.rfb
encperf encoding=Hextile width=1280 height=720 format=rgb888 test.rfb
encperf encoding=ZRLE width=1280 height=720 format=rgb888 test.rfb
```

**关注指标**：
- CPU 时间（越低越好）
- 编码字节数（带宽占用）
- 每帧耗时（交互延迟）

## 五、结论

TuPigVNC 在编译层面已启用 LTO 与静态链接，代码层面的关键路径（`memcpy` 行级复制、zlib 流复用、直接解码）已具备上游优化。

**建议优先级**：
1. **短期**：启用 AVX2（一行配置，5~15% 收益）
2. **中期**：SIMD 像素转换（收益最大，但需仔细测试）
3. **长期**：GPU 解码（架构级改造）

当前 v0.0.1 的性能已满足日常远程桌面需求，后续优化可按实际使用反馈逐步推进。
