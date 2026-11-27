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
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

/* 音频播放后端（src/vncviewer/AudioOutput.h）。只前置声明，
   头文件依赖留在 .cxx 里。 */
class AudioOutput;

struct VncSession : public rfb::CConnection {
public:
  explicit VncSession(const VncCallbacks* cb);
  ~VncSession() override;

  /* ===== 对外操作（C ABI 适配层调用） ===== */

  /* 建 TCP 连接并初始化协议状态。阻塞；失败时把原因写进 error。 */
  bool connectTo(const char* host, int port, std::string* error);

  /* 监听模式：绑定端口，阻塞等待服务器反向连入，拿到连接后初始化协议。
     acceptTimeoutMs 为 0 表示一直等。失败时把原因写进 error。
     对照原版 vncviewer.cxx 的 listenMode 分支。 */
  bool listenAndAccept(int port, int acceptTimeoutMs, std::string* error);

  /* 启动内部事件循环线程（重复调用无副作用）。 */
  void startLoop();

  /* 当前线程是否就是事件循环线程。销毁这类对象必须走延迟路径。 */
  bool isOnEventLoopThread() const;

  /* 标记「退出事件循环后由循环自己 delete 自己」。
     只能在事件循环线程上调用。 */
  void markForDeferredDelete();

  /* 请求停止事件循环并等待线程退出。 */
  void requestStop();
  void signalStop() { stop_.store(true); }
  int copyFrame(uint8_t* data, size_t capacity, int stride,
                int* width, int* height);
  std::vector<uint8_t> snapshot_;
  int snapshotWidth_ = 0;
  int snapshotHeight_ = 0;

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
  /* 是否共享会话。必须在 connect 之前调用：标志随 ClientInit 一起发出。 */
  void setSharedFlag(bool shared);
  /* 安全类型（逗号分隔的 TigerVNC 名字，如 "None,VncAuth"）。
     返回 0 成功；-1 表示有名字不认识，整份拒绝、不静默丢弃。
     必须在 connect 之前调用。 */
  int setSecurityTypes(const char* types);
  /* 本核心实际编译进去的安全类型（逗号分隔），供界面把不支持的项置灰。 */
  static std::string availableSecurityTypes();
  /* X.509 的 CA 证书 / 吊销列表路径。未编译 GnuTLS 时静默忽略。 */
  void setX509CA(const char* path);
  void setX509CRL(const char* path);
  /* 是否协商并播放服务器音频。必须在 connect 之前调用：
     能力位随 SetEncodings 一起发出，晚于初始化就收不到音频。 */
  void setAudioEnabled(bool enabled);
  /* 音频后端是否可用（没有声卡时为 false）。 */
  bool isAudioAvailable() const;

  /* 把一条错误报告给托管侧（走 on_error 回调）。
     C ABI 适配层在启动事件循环失败等场景需要主动上报，不能静默。 */
  void reportFatalError(const std::string& msg) { reportError(msg, 1); }

  /* 网络统计快照（供会话窗口的迷你柱状图用）。
     对照原版 CConn::getUpdateCount / getPixelCount / getPosition。 */
  struct Stats
  {
    unsigned updates;   /* 累计帧更新次数 */
    unsigned pixels;    /* 累计解码像素数 */
    unsigned bytes;     /* 累计从服务器收到的字节数 */
    unsigned long long bitsPerSecond;  /* 带宽估算（滑动平均） */
  };
  Stats copyStats() const;

  /* 是否按实测带宽自动调整 JPEG 质量（原版 autoSelect 的行为）。
     必须在 connect 之前调用。 */
  void setAutoSelectQuality(bool enabled);
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

  /* 调用托管回调并吞掉所有异常。托管侧随时可能抛（Dispatcher 取消、
     OutOfMemory 等），异常穿透 C++ 栈会破坏协议状态甚至 terminate 进程。
     返回 false 表示回调抛了异常，调用方按「拒绝 / 中止」处理。 */
  template <typename Fn>
  bool safeCallback(const char* name, Fn&& fn);

  /* 每帧结束时更新带宽估算，必要时按滞回带调整 JPEG 质量。 */
  void updateBandwidthEstimate();

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

  /* 扩展剪贴板的三个业务钩子。基类里是空实现，这里转给托管侧。
     没有它们，新版 TigerVNC / Xvnc 走扩展协议时剪贴板完全不通——
     旧版 ServerCutText 只是兼容路径。 */
  void handleClipboardRequest() override;
  void handleClipboardAnnounce(bool available) override;
  void handleClipboardData(const char* data) override;

  /* 协议层的音频消息（CMsgHandler 纯虚）。转发到 handleAudio*，
     由子类决定是否真的播放。 */
  void audioBegin() override;
  void audioEnd() override;
  void audioData(const uint8_t* data, size_t length) override;

  /* 真正播放音频。只有开启音频且后端可用时才会被调用。
     对照原版 CConn::handleAudioBegin/End/Data。 */
  void handleAudioBegin() override;
  void handleAudioEnd() override;
  void handleAudioData(const uint8_t* data, size_t length) override;

  void resizeFramebuffer() override;

  /* 把内部解码结果逐行拷进 C# 提供的缓冲。 */
  void copyToExternal();

private:
  VncCallbacks cb_{};                 /* 回调表副本 */
  network::Socket* sock_ = nullptr;   /* 拥有：析构时释放 */

  /* 音频播放后端。只在 setAudioEnabled(true) 且后端可用时创建；
     回调运行在系统音频线程上，__play 本身是线程安全的。 */
  AudioOutput* audio_ = nullptr;
  /* 托管侧（UI 线程）写、事件循环线程读：必须原子，否则是数据竞态。 */
  std::atomic<bool> audioEnabled_{false};

  /* 网络统计。只在事件循环线程上写，任意线程可读。 */
  std::atomic<unsigned> statUpdates_{0};
  std::atomic<unsigned> statPixels_{0};
  /* 累计收到字节数。必须在事件循环线程里累加：
     跨线程读 FdInStream::pos() 是数据竞态，而且 unsigned 会在大流量下回绕。 */
  std::atomic<unsigned long long> statBytes_{0};

  /* 带宽估算（指数滑动平均，单位 bit/s）。逐条对照原版
     CConn::framebufferUpdateEnd 里的 bpsEstimate 算法。 */
  std::atomic<unsigned long long> bpsEstimate_{20000000ULL};
  unsigned long long updateStartPos_ = 0;
  std::chrono::steady_clock::time_point updateStartTime_;
  std::atomic<bool> autoSelectQuality_{false};

  std::thread loopThread_;
  std::atomic<bool> stop_{false};
  std::atomic<bool> loopStarted_{false};
  /* 事件循环线程请求「退出后自己 delete 自己」。
     用于「在回调线程里销毁会话」这条危险路径。 */
  std::atomic<bool> deferredDelete_{false};

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
  /* 队列满时丢弃的事件数。仅用于排查，不对外暴露。 */
  unsigned long long droppedEvents_ = 0;

  std::string host_;
  int port_ = 0;
};

#endif  /* TUPIGVNC_VNCSESSION_H_ */
