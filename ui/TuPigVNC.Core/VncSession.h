/*
 * TuPigVNC · 原生核心会话（阶段 1）
 *
 * 把原 FLTK 客户端 CConn 的连接逻辑从 GUI 里剥出来，做成一个不依赖
 * FLTK 的纯 C++ 会话对象。它继承 rfb::CConnection，所有协议回调都转发
 * 给 C ABI 的回调表（VncCallbacks），供 C# WPF 前端 P/Invoke 使用。
 *
 * 线程模型：
 *   - connectTo() 在调用方线程同步建 TCP、挂流、初始化协议状态；
 *   - startLoop() 起一个内部线程跑 select() + processMsg()；
 *   - 所有网络回调（on_server_init / on_frame_update / ...）都在该
 *     内部线程上触发；
 *   - 输入/操作类方法（sendKey、sendPointer 等）由任意线程调用，内部
 *     只做「入队」，真正写 socket 由事件循环线程完成，避免多线程同时
 *     操作 rdr 缓冲区。
 */

#ifndef TUPIGVNC_VNCSESSION_H_
#define TUPIGVNC_VNCSESSION_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include <core/Rect.h>

#include <rfb/CConnection.h>
#include <rfb/PixelFormat.h>
#include <rfb/ScreenSet.h>

#include "VncAbi.h"

namespace network {
  class Socket;
}

namespace rfb {
  class ManagedPixelBuffer;
}

struct VncSession : public rfb::CConnection {
public:
  explicit VncSession(const VncCallbacks* cb);
  ~VncSession() override;

  /* ===== 对外操作（C ABI 适配层调用） ===== */

  /* 建 TCP 连接并初始化协议状态。阻塞；失败时把原因写进 error。 */
  bool connectTo(const char* host, int port, std::string* error);

  /* 启动内部事件循环线程（重复调用无副作用）。 */
  void startLoop();

  /* 请求停止事件循环并等待线程退出。 */
  void requestStop();

  /* 设置 C# 侧的像素缓冲（非托管内存，DLL 不释放）。 */
  void setFramebuffer(uint8_t* data, int width, int height, int stride);

  /* 输入 / 剪贴板 / 编码 —— 内部入队，由事件循环线程真正发送。 */
  void sendKey(int systemKeyCode, uint32_t keyCode, uint32_t keySym);
  void sendKeyRelease(int systemKeyCode);
  void sendPointer(int x, int y, uint16_t buttonmask);
  void refresh();
  void requestClipboard();
  void announceClipboard(int available);
  void sendClipboard(const char* text);
  void setEncoding(int encoding);
  void setCompressLevel(int level);
  void setQualityLevel(int level);
  void requestScreenLayout(int width, int height,
                           const int* screens, int count);

private:
  /* ===== 事件循环 ===== */

  void runLoop();
  bool processNetwork();
  bool drainInput();
  bool flushOutput() noexcept;

  void enqueue(std::function<void()> fn);
  void reportError(const std::string& msg, int fatal);

  /* ===== CConnection / CMsgHandler 回调 ===== */

  void getUserPasswd(std::string* user, std::string* password) override;
  bool verifyCertificate(unsigned int status,
                         const uint8_t* certificate,
                         size_t length) override;
  bool verifyHostKey(const uint8_t* key, size_t length,
                     const char* fingerprint) override;

  void initDone() override;

  void setExtendedDesktopSize(unsigned reason, unsigned result,
                              int w, int h,
                              const rfb::ScreenSet& layout) override;
  void setCursor(int width, int height, const core::Point& hotspot,
                 const uint8_t* data) override;
  void setCursorPos(const core::Point& pos) override;
  void setName(const char* name) override;
  void bell() override;

  void fence(uint32_t flags, unsigned len, const uint8_t data[]) override;
  void endOfContinuousUpdates() override;

  void serverInit(int width, int height, const rfb::PixelFormat& pf,
                  const char* name) override;
  bool readAndDecodeRect(const core::Rect& r, int encoding,
                         const rfb::PixelFormat& pf,
                         rfb::ModifiablePixelBuffer* pb) override;

  void framebufferUpdateStart() override;
  void framebufferUpdateEnd() override;
  bool dataRect(const core::Rect& r, int encoding) override;

  void setColourMapEntries(int firstColour, int nColours,
                           uint16_t* rgbs) override;
  void serverCutText(const char* str) override;

  void setLEDState(unsigned int state) override;

  void clipboardCaps(uint32_t flags, const uint32_t* lengths) override;
  void clipboardRequest(uint32_t flags) override;
  void clipboardPeek() override;
  void clipboardNotify(uint32_t flags) override;
  void clipboardProvide(uint32_t flags, const size_t* lengths,
                        const uint8_t* const* data) override;

  void audioBegin() override;
  void audioEnd() override;
  void audioData(const uint8_t* data, size_t length) override;

  void resizeFramebuffer() override;

  /* 把内部解码结果逐行拷进 C# 提供的缓冲。 */
  void copyToExternal();

private:
  VncCallbacks cb_{};                 /* 回调表副本 */
  network::Socket* sock_ = nullptr;   /* 拥有：析构时释放 */

  std::thread loopThread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> loopStarted_{false};

  /* 内部解码用帧缓冲；所有权交给 rfb::CConnection::setFramebuffer，
   * 这里只保留裸指针用于拷贝。 */
  rfb::ManagedPixelBuffer* internalFb_ = nullptr;

  /* C# 提供的像素缓冲（非托管，不释放）。 */
  uint8_t* fb_ = nullptr;
  int fbWidth_ = 0;
  int fbHeight_ = 0;
  int fbStride_ = 0;                  /* 字节步长 */
  std::mutex fbMutex_;

  /* 输入队列：任意线程入队，事件循环线程消费。 */
  std::mutex queueMutex_;
  std::deque<std::function<void()>> queue_;

  std::string host_;
  int port_ = 0;
};

#endif  /* TUPIGVNC_VNCSESSION_H_ */
