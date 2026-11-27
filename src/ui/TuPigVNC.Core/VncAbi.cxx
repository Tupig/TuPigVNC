/*
 * TuPigVNC · 原生核心 C ABI 实现（阶段 1 完整版）
 *
 * 所有 extern "C" 入口都做异常边界：任何 C++ 异常都在 DLL 内部
 * 转成错误码 / 静默吞掉，绝不跨越 C ABI 边界（否则会直接终止进程）。
 */

#include "VncAbi.h"

#include <cstring>
#include <exception>
#include <string>

/* 版本号由 CMake 注入；单独编译本文件时给个兜底，避免宏未定义 */
#ifndef TUPIGVNC_VERSION_STRING
#  define TUPIGVNC_VERSION_STRING "0.0.0"
#endif

#include <core/UpdateChecker.h>

#include "VncSession.h"

namespace {

/* 把 std::string 写进 C 风格缓冲（保证 '\0' 结尾，超长截断）。
   错误缓冲和结构体里的定长字符串共用这一份实现。 */
void copyError(char* dest, size_t capacity, const std::string& message)
{
  if (dest == nullptr || capacity == 0)
    return;

  const size_t n = (message.size() < capacity - 1) ? message.size() : capacity - 1;
  std::memcpy(dest, message.data(), n);
  dest[n] = '\0';
}

}  // namespace

extern "C" {

//////////////////////////// 会话生命周期 ////////////////////////////

VNC_API VncSession* vnc_create(const VncCallbacks* cb)
{
  try {
    return new VncSession(cb);
  } catch (...) {
    return nullptr;
  }
}

VNC_API int vnc_connect(VncSession* s, const char* host, int port,
                        char* err, size_t errLen)
{
  try {
    if (s == nullptr || host == nullptr || *host == '\0' ||
        port <= 0 || port > 65535) {
      copyError(err, errLen, "invalid argument");
      return -1;
    }

    std::string message;
    if (!s->connectTo(host, port, &message)) {
      copyError(err, errLen, message);
      return -2;
    }

    return 0;
  } catch (const std::exception& e) {
    copyError(err, errLen, e.what());
    return -5;
  } catch (...) {
    copyError(err, errLen, "unknown exception");
    return -5;
  }
}

VNC_API void vnc_start_loop(VncSession* s)
{
  try {
    if (s != nullptr)
      s->startLoop();
  } catch (const std::exception& e) {
    /* 线程起不来时事件循环完全不动，必须让调用方知道，
       否则界面会一直停在「连接中」。 */
    s->reportFatalError(std::string("failed to start event loop: ") + e.what());
  } catch (...) {
    s->reportFatalError("failed to start event loop: unknown exception");
  }
}

VNC_API void vnc_request_stop(VncSession* s)
{
  if (s) s->signalStop();
}

VNC_API int vnc_copy_frame(VncSession* s, uint8_t* data, size_t capacity,
                           int stride, int* width, int* height)
{
  try { return s ? s->copyFrame(data, capacity, stride, width, height) : -1; }
  catch (...) { return -5; }
}

VNC_API void vnc_stop(VncSession* s)
{
  try {
    if (s != nullptr)
      s->requestStop();
  } catch (...) {
  }
}

VNC_API void vnc_destroy(VncSession* s)
{
  if (s == nullptr)
    return;

  /* 关键：绝不能在事件循环线程里 delete 会话。
     那会析构一个仍然 joinable 的 std::thread（标准规定直接 std::terminate），
     就算侥幸不崩，runLoop 还在读已释放的 this，是明确的 use-after-free。
     C# 侧最自然的写法就是「收到致命错误回调 → 销毁会话」，正好踩中。

     这里退化成「只请求停止 + 标记待销毁」，由事件循环线程退出前自己释放。
     宁可晚一点释放，也不能崩进程。 */
  if (s->isOnEventLoopThread()) {
    s->requestStop();
    s->markForDeferredDelete();
    return;
  }

  try {
    delete s;
  } catch (...) {
  }
}

//////////////////////////// 帧缓冲 ////////////////////////////

VNC_API void vnc_set_framebuffer(VncSession* s, uint8_t* data,
                                 int width, int height, int stride)
{
  try {
    if (s != nullptr)
      s->setFramebuffer(data, width, height, stride);
  } catch (...) {
  }
}

//////////////////////////// 输入 ////////////////////////////

VNC_API void vnc_send_key(VncSession* s, int systemKeyCode,
                          uint32_t keyCode, uint32_t keySym)
{
  try {
    if (s != nullptr)
      s->sendKey(systemKeyCode, keyCode, keySym);
  } catch (...) {
  }
}

VNC_API void vnc_send_key_release(VncSession* s, int systemKeyCode)
{
  try {
    if (s != nullptr)
      s->sendKeyRelease(systemKeyCode);
  } catch (...) {
  }
}

VNC_API void vnc_send_pointer(VncSession* s, int x, int y,
                              uint16_t buttonmask)
{
  try {
    if (s != nullptr)
      s->sendPointer(x, y, buttonmask);
  } catch (...) {
  }
}

VNC_API void vnc_refresh(VncSession* s)
{
  try {
    if (s != nullptr)
      s->refresh();
  } catch (...) {
  }
}

//////////////////////////// 剪贴板 ////////////////////////////

VNC_API void vnc_request_clipboard(VncSession* s)
{
  try {
    if (s != nullptr)
      s->requestClipboard();
  } catch (...) {
  }
}

VNC_API void vnc_announce_clipboard(VncSession* s, int available)
{
  try {
    if (s != nullptr)
      s->announceClipboard(available);
  } catch (...) {
  }
}

VNC_API void vnc_send_clipboard(VncSession* s, const char* text)
{
  try {
    if (s != nullptr)
      s->sendClipboard(text);
  } catch (...) {
  }
}

//////////////////////////// 会话选项 ////////////////////////////

/* 监听模式：绑定端口等服务器反向连入。阻塞；超时或失败返回负值，
   原因写进 err 缓冲。对照原版 -listen。 */
VNC_API int vnc_listen(VncSession* s, int port, int acceptTimeoutMs,
                       char* err, size_t errLen)
{
  try {
    /* 负超时没有意义，当参数错误拒绝，别让它变成「永远等」。 */
    if (s == nullptr || port <= 0 || port > 65535 || acceptTimeoutMs < 0) {
      copyError(err, errLen, "invalid port or timeout");
      return -1;
    }
    std::string message;
    if (!s->listenAndAccept(port, acceptTimeoutMs, &message)) {
      copyError(err, errLen, message);
      return -2;
    }
    return 0;
  } catch (const std::exception& e) {
    copyError(err, errLen, e.what());
    return -5;
  } catch (...) {
    copyError(err, errLen, "unknown exception");
    return -5;
  }
}

VNC_API void vnc_set_shared(VncSession* s, int shared)
{
  try {
    if (s != nullptr)
      s->setSharedFlag(shared != 0);
  } catch (...) {
  }
}

//////////////////////////// 安全类型 ////////////////////////////

VNC_API int vnc_set_security_types(VncSession* s, const char* types)
{
  try {
    if (s == nullptr || types == nullptr)
      return -1;
    return s->setSecurityTypes(types);
  } catch (...) {
    return -1;
  }
}

VNC_API void vnc_set_x509_ca(VncSession* s, const char* path)
{
  try {
    if (s != nullptr)
      s->setX509CA(path);
  } catch (...) {
  }
}

VNC_API void vnc_set_x509_crl(VncSession* s, const char* path)
{
  try {
    if (s != nullptr)
      s->setX509CRL(path);
  } catch (...) {
  }
}

//////////////////////////// 音频 ////////////////////////////

VNC_API void vnc_set_audio_enabled(VncSession* s, int enabled)
{
  try {
    if (s != nullptr)
      s->setAudioEnabled(enabled != 0);
  } catch (...) {
  }
}

VNC_API int vnc_audio_available(VncSession* s)
{
  try {
    return (s != nullptr && s->isAudioAvailable()) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

//////////////////////////// 网络统计 ////////////////////////////

VNC_API void vnc_get_stats(VncSession* s, unsigned* updates,
                           unsigned* pixels, unsigned* bytes,
                           unsigned long long* bitsPerSecond)
{
  try {
    if (s == nullptr)
      return;
    const VncSession::Stats stats = s->copyStats();
    if (updates != nullptr) *updates = stats.updates;
    if (pixels != nullptr) *pixels = stats.pixels;
    if (bytes != nullptr) *bytes = stats.bytes;
    if (bitsPerSecond != nullptr) *bitsPerSecond = stats.bitsPerSecond;
  } catch (...) {
  }
}

VNC_API void vnc_set_auto_select_quality(VncSession* s, int enabled)
{
  try {
    if (s != nullptr)
      s->setAutoSelectQuality(enabled != 0);
  } catch (...) {
  }
}

/* 本核心编译进去的安全类型，写进调用方缓冲。返回写入的字节数（不含结尾 '\0'），
   缓冲不够时截断并返回 -1。 */
VNC_API int vnc_security_types(char* buffer, size_t capacity)
{
  try {
    const std::string types = VncSession::availableSecurityTypes();

    /* 缓冲不够时**什么都不写**，直接返回 -1。
       原实现先截断写入再报错，调用方拿到的是一份被截断的类型串，
       照着它去协商会漏掉类型——这类「看起来成功」的失败必须避免。 */
    if (buffer == nullptr || capacity == 0 || types.size() >= capacity)
      return -1;

    copyError(buffer, capacity, types);
    return static_cast<int>(types.size());
  } catch (...) {
    return -1;
  }
}

//////////////////////////// 编码 / 画质 ////////////////////////////

VNC_API void vnc_set_encoding(VncSession* s, int encoding)
{
  try {
    if (s != nullptr)
      s->setEncoding(encoding);
  } catch (...) {
  }
}

VNC_API void vnc_set_compress_level(VncSession* s, int level)
{
  try {
    if (s != nullptr)
      s->setCompressLevel(level);
  } catch (...) {
  }
}

VNC_API void vnc_set_quality_level(VncSession* s, int level)
{
  try {
    if (s != nullptr)
      s->setQualityLevel(level);
  } catch (...) {
  }
}

//////////////////////////// 多显示器 ////////////////////////////

VNC_API void vnc_request_screen_layout(VncSession* s, int width, int height,
                                        const int* screens, int count)
{
  try {
    if (s != nullptr)
      s->requestScreenLayout(width, height, screens, count);
  } catch (...) {
  }
}

/* 把远程桌面设成单个指定尺寸的屏幕。供「窗口变化时同步远程分辨率」用。
   服务器不支持 SetDesktopSize 时核心会忽略——调用方不能假装成功。 */
VNC_API void vnc_request_remote_size(VncSession* s, int width, int height)
{
  try {
    if (s == nullptr || width <= 0 || height <= 0)
      return;
    /* 单个屏幕，从 (0,0) 铺满 width×height。 */
    const int screen[4] = { 0, 0, width, height };
    s->requestScreenLayout(width, height, screen, 1);
  } catch (...) {
  }
}

//////////////////////////// 测试 / 元信息 ////////////////////////////

VNC_API int vnc_test_add(int a, int b)
{
  try {
    return a + b;
  } catch (...) {
    return -5;
  }
}

VNC_API const char* vnc_version(void)
{
  /* 静态字符串，调用方只读，不涉及所有权。
     版本号来自 CMake 注入的 TUPIGVNC_VERSION_STRING，不在源码里硬编码。 */
  return "TuPigVNC.Core " TUPIGVNC_VERSION_STRING " (stage 1)";
}

//////////////////////////// 自动更新 ////////////////////////////

VNC_API int vnc_check_update(VncReleaseInfo* info, char* err, size_t errLen)
{
  if (info == nullptr) {
    copyError(err, errLen, "release info buffer is null");
    return -1;
  }

  try {
    core::ReleaseInfo release;
    std::string error;
    if (!core::UpdateChecker::fetchLatestRelease(&release, &error)) {
      copyError(err, errLen, error.empty() ? "failed to check for updates" : error);
      return -2;
    }

    copyError(info->version, sizeof(info->version), release.version);
    copyError(info->name, sizeof(info->name), release.name);
    copyError(info->publishedAt, sizeof(info->publishedAt), release.publishedAt);
    copyError(info->htmlUrl, sizeof(info->htmlUrl), release.htmlUrl);
    copyError(info->downloadUrl, sizeof(info->downloadUrl), release.downloadUrl);
    copyError(info->checksumUrl, sizeof(info->checksumUrl), release.checksumUrl);
    info->size = release.size;

    /* 发行说明用调用方给的可变缓冲，长度由它自己定。 */
    if (info->notes != nullptr && info->notesCapacity > 0) {
      const size_t len = release.notes.size() < info->notesCapacity - 1
                             ? release.notes.size()
                             : info->notesCapacity - 1;
      memcpy(info->notes, release.notes.data(), len);
      info->notes[len] = '\0';
    }
    return 0;
  } catch (const std::exception& e) {
    copyError(err, errLen, e.what());
    return -5;
  } catch (...) {
    copyError(err, errLen, "unknown error while checking for updates");
    return -5;
  }
}

VNC_API int vnc_compare_versions(const char* a, const char* b)
{
  try {
    return core::UpdateChecker::compareVersions(a != nullptr ? a : "",
                                                b != nullptr ? b : "");
  } catch (...) {
    /* 比较失败不能崩，双方各不相同就当相等。 */
    return 0;
  }
}

VNC_API int vnc_download_and_install(const char* url, const char* checksumUrl,
                                     const char* version,
                                     char* err, size_t errLen)
{
  if (url == nullptr || *url == '\0') {
    copyError(err, errLen, "download url is empty");
    return -1;
  }

  try {
    std::string error;
    /* 原生实现自己会做域名白名单、版本号合法性、checksumUrl 为空的拒绝。 */
    if (!core::UpdateChecker::downloadAndInstall(
            url, checksumUrl != nullptr ? checksumUrl : "",
            version != nullptr ? version : "", &error)) {
      copyError(err, errLen, error.empty() ? "update failed" : error);
      return -2;
    }
    return 0;
  } catch (const std::exception& e) {
    copyError(err, errLen, e.what());
    return -5;
  } catch (...) {
    copyError(err, errLen, "unknown error while installing update");
    return -5;
  }
}

}  // extern "C"
