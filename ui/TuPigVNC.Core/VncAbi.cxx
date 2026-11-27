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

#include "VncSession.h"

namespace {

/* 把 std::string 写进 C 风格错误缓冲（保证 '\0' 结尾）。 */
void copyError(char* err, size_t errLen, const std::string& message)
{
  if (err == nullptr || errLen == 0)
    return;

  const size_t n = (message.size() < errLen - 1) ? message.size() : errLen - 1;
  std::memcpy(err, message.data(), n);
  err[n] = '\0';
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
  } catch (...) {
    /* 启动失败静默；调用方可通过 on_error 得知 */
  }
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
  /* 静态字符串，调用方只读，不涉及所有权 */
  return "TuPigVNC.Core 0.0.5 (stage 1)";
}

}  // extern "C"
