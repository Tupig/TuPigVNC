/*
 * TuPigVNC · 原生核心会话实现（阶段 1）
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/select.h>
#include <sys/time.h>
#include <cerrno>
#endif

#include <algorithm>
#include <cctype>
#include <cstring>
#include <list>
#include <stdexcept>
#include <utility>
#include <vector>

#include <core/LogWriter.h>
#include <core/Rect.h>
#include <core/string.h>

#include <rdr/FdInStream.h>
#include <rdr/FdOutStream.h>
#include <rdr/InStream.h>
#include <rdr/OutStream.h>

#include <rfb/Exception.h>
#include <rfb/PixelBuffer.h>
#include <rfb/PixelFormat.h>
#include <rfb/ScreenSet.h>
#include <rfb/Security.h>
#ifdef HAVE_GNUTLS
#include <rfb/CSecurityTLS.h>
#endif

#include <network/Socket.h>
#include <network/TcpSocket.h>

#include "../vncviewer/AudioOutput.h"

#include "VncSession.h"

static core::LogWriter vlog("VncSession");

/* 固定像素格式：32bpp，小端内存布局 B,G,R,X。 */
static const rfb::PixelFormat fixedPF(32, 24, false, true,
                                      255, 255, 255,
                                      16, 8, 0);

/* 事件循环等待网络可读的超时（微秒）。同时用作停止请求的轮询间隔。 */
static const int pollTimeoutUsec = 100 * 1000;

/* 输入/操作队列上限。1000 个事件已经远超一个人能产生的速率，
   到这里只说明事件循环卡住了，再攒没有意义。 */
static const size_t kMaxQueuedEvents = 1000;

/* 凭据缓冲大小（用户名/密码）。 */
static const size_t credentialBufSize = 512;

/* 名字是否在本核心编译进去的安全类型里（逗号分隔串，大小写不敏感）。
   不用迭代器：Security.h 只前向声明 EnumListParameter，迭代器类型不完整。 */
static bool containsSecurityName(const std::string& name)
{
  const std::string all = rfb::SecurityClient::secTypes.getDefaultStr();
  size_t start = 0;
  while (start <= all.size()) {
    size_t comma = all.find(',', start);
    if (comma == std::string::npos)
      comma = all.size();
    if (comma - start == name.size() &&
        strncasecmp(all.c_str() + start, name.c_str(), name.size()) == 0)
      return true;
    start = comma + 1;
  }
  return false;
}

VncSession::VncSession(const VncCallbacks* cb)
{
  if (cb != nullptr)
    cb_ = *cb;
  else
    std::memset(&cb_, 0, sizeof(cb_));

  /* 与旧版 CConn 保持一致的服务器能力协商。 */
  supportsLocalCursor = true;
  supportsCursorPosition = true;
  supportsDesktopResize = true;
  supportsLEDState = true;
}

VncSession::~VncSession()
{
  requestStop();

  /* 在流仍然有效时释放协议对象（reader/writer/framebuffer）。 */
  try {
    rfb::CConnection::close();
  } catch (...) {
    /* 析构里绝不外抛 */
  }

  internalFb_ = nullptr;  /* 已由 close() 释放 */

  /* 音频后端必须在事件循环完全停下后再释放：waveOutReset 会等
     所有回调返回，早释放会有回调打到已释放对象上。 */
  delete audio_;
  audio_ = nullptr;

  if (sock_ != nullptr) {
    try {
      sock_->shutdownWrite();
    } catch (...) {
    }
    delete sock_;
    sock_ = nullptr;
  }
}

//////////////////////////// 连接 / 事件循环 ////////////////////////////

bool VncSession::connectTo(const char* host, int port, std::string* error)
{
  if (host == nullptr || *host == '\0' || port <= 0 || port > 65535) {
    if (error != nullptr)
      *error = "invalid host or port";
    return false;
  }

  try {
    network::initSockets();

    /* 重复 connect（重连逻辑写岔、先 connect 再 listen）时，
       旧 socket 会被直接覆盖，TCP fd 就漏了。先清掉。 */
    if (sock_ != nullptr) {
      try {
        sock_->shutdownRead();
        sock_->shutdownWrite();
      } catch (...) {
      }
      delete sock_;
      sock_ = nullptr;
    }

    auto* tcp = new network::TcpSocket(host, port);
    /* TCP 保活：检测半开连接（网络闪断、NAT 超时）。 */
    tcp->enableKeepAlive(true);
    sock_ = tcp;

    host_ = host;
    port_ = port;

    setServerName(host_.c_str());
    setStreams(&sock_->inStream(), &sock_->outStream());
    initialiseProtocol();

    vlog.info("Connected to %s:%d", host_.c_str(), port_);
    return true;
  } catch (std::exception& e) {
    if (error != nullptr)
      *error = e.what();
    vlog.error("Failed to connect to %s:%d: %s", host, port, e.what());
    if (sock_ != nullptr) {
      delete sock_;
      sock_ = nullptr;
    }
    return false;
  } catch (...) {
    /* 非 std::exception 的异常（SEH 等）也要留下痕迹，
       否则排查「连不上但没有任何原因」时完全无从下手。 */
    vlog.error("Failed to connect to %s:%d: unknown exception", host, port);
    if (error != nullptr)
      *error = "unknown error";
    if (sock_ != nullptr) {
      delete sock_;
      sock_ = nullptr;
    }
    return false;
  }
}

bool VncSession::listenAndAccept(int port, int acceptTimeoutMs, std::string* error)
{
  if (port <= 0 || port > 65535) {
    if (error != nullptr) *error = "invalid port";
    return false;
  }

  /* 监听套接字用 RAII 管理：无论异常还是正常返回都不会漏。
     createTcpListeners 可能创建多个（IPv4/IPv6），全部要释放。 */
  std::list<network::SocketListener*> listeners;
  network::Socket* accepted = nullptr;

  auto cleanupListeners = [&listeners]() {
    for (network::SocketListener* listener : listeners)
      delete listener;
    listeners.clear();
  };

  try {
    network::initSockets();

    /* 只监听回环：反向连接通常来自本机隧道或可信网络，
       绑 0.0.0.0 会把机器暴露在公网上，这是原版没有的安全隐患。 */
    network::createTcpListeners(&listeners, "127.0.0.1", port);
    if (listeners.empty()) {
      if (error != nullptr) *error = "无法绑定监听端口";
      return false;
    }

    vlog.info("Listening for reverse connection on port %d", port);

    /* 关键：SocketListener::accept() 是**阻塞**的（底层 ::accept 没有
       非阻塞标志），不能直接调，否则会永远卡住、取消也退不出来。
       这里用 select 带超时先等可读，再 accept，才能响应停止请求。 */
    const auto deadline = acceptTimeoutMs > 0
        ? std::chrono::steady_clock::now() + std::chrono::milliseconds(acceptTimeoutMs)
        : std::chrono::steady_clock::time_point::max();

    while (!stop_.load() && std::chrono::steady_clock::now() < deadline) {
      fd_set rfds;
      FD_ZERO(&rfds);
      int maxFd = -1;
      for (network::SocketListener* listener : listeners) {
        const int fd = listener->getFd();
        if (fd < 0)
          continue;
        FD_SET(fd, &rfds);
        if (fd > maxFd) maxFd = fd;
      }
      if (maxFd < 0) break;

      /* 100ms 超时：既能及时响应取消，又不会空转烧 CPU。 */
      struct timeval timeout;
      timeout.tv_sec = 0;
      timeout.tv_usec = 100 * 1000;

      const int ready = ::select(maxFd + 1, &rfds, nullptr, nullptr, &timeout);
      if (ready < 0) {
        /* 被信号打断是正常的，继续等；真正的错误才退出。
           Windows 的 select 失败不设置 CRT errno，要查 WSAGetLastError()，
           否则 errno 是陈旧值——碰巧等于 EINTR 就会无限空转。 */
#ifdef _WIN32
        if (WSAGetLastError() == WSAEINTR)
          continue;
#else
        if (errno == EINTR)
          continue;
#endif
        if (error != nullptr) *error = "select 等待连接失败";
        cleanupListeners();
        return false;
      }
      if (ready == 0)
        continue;   /* 超时：回到循环顶部检查 stop_ */

      for (network::SocketListener* listener : listeners) {
        const int fd = listener->getFd();
        if (fd < 0 || !FD_ISSET(fd, &rfds))
          continue;
        /* 有连接待接受，此时 accept() 不会阻塞。 */
        accepted = listener->accept();
        if (accepted != nullptr)
          break;
      }
      if (accepted != nullptr)
        break;
    }

    cleanupListeners();

    if (stop_.load()) {
      /* 用户取消：接受到的连接也算无效，一并释放。 */
      if (accepted != nullptr) {
        delete accepted;
        accepted = nullptr;
      }
      if (error != nullptr) *error = "cancelled";
      return false;
    }

    if (accepted == nullptr) {
      if (error != nullptr) *error = "等待反向连接超时";
      return false;
    }

    auto* tcp = dynamic_cast<network::TcpSocket*>(accepted);
    if (tcp != nullptr)
      tcp->enableKeepAlive(true);
    sock_ = accepted;

    host_ = "listen";
    port_ = port;

    setServerName(host_.c_str());
    setStreams(&sock_->inStream(), &sock_->outStream());
    initialiseProtocol();

    vlog.info("Accepted reverse connection on port %d", port);
    return true;
  } catch (std::exception& e) {
    cleanupListeners();
    if (error != nullptr) *error = e.what();
    vlog.error("Listen failed on port %d: %s", port, e.what());
    /* accepted 可能已经赋给 sock_，也可能还没（setServerName 之前就抛了）。
       没赋值时 sock_ 是 nullptr，得单独删 accepted，否则 fd 泄漏。 */
    if (sock_ == nullptr && accepted != nullptr) {
      try {
        accepted->shutdownRead();
        accepted->shutdownWrite();
      } catch (...) {
      }
      delete accepted;
    }
    accepted = nullptr;
    if (sock_ != nullptr) {
      delete sock_;
      sock_ = nullptr;
    }
    return false;
  } catch (...) {
    cleanupListeners();
    if (error != nullptr) *error = "unknown error";
    vlog.error("Listen failed on port %d: unknown exception", port);
    if (sock_ == nullptr && accepted != nullptr) {
      try {
        accepted->shutdownRead();
        accepted->shutdownWrite();
      } catch (...) {
      }
      delete accepted;
    }
    accepted = nullptr;
    if (sock_ != nullptr) {
      delete sock_;
      sock_ = nullptr;
    }
    return false;
  }
}

bool VncSession::isOnEventLoopThread() const
{
  return loopThread_.joinable() &&
         std::this_thread::get_id() == loopThread_.get_id();
}

void VncSession::markForDeferredDelete()
{
  deferredDelete_.store(true);
}

void VncSession::startLoop()
{
  if (sock_ == nullptr)
    return;

  if (loopStarted_.exchange(true))
    return;

  try {
    loopThread_ = std::thread([this]() { runLoop(); });
  } catch (...) {
    /* 线程创建失败（资源耗尽等）时把标志回滚，
       否则之后再也无法重试启动，会话永远静默不动。 */
    loopStarted_.store(false);
    throw;
  }
}

void VncSession::requestStop()
{
  stop_.store(true);

  if (!loopThread_.joinable())
    return;

  /* 不允许在事件循环线程里 join 自己（例如回调里同步调用 vnc_stop）。
     那种情况下对象销毁必须由调用方延迟到循环退出之后，
     否则 joinable 的 thread 在析构里会触发 std::terminate。 */
  if (std::this_thread::get_id() == loopThread_.get_id())
    return;

  /* 光靠 stop_ 标志停不下来：事件循环可能正阻塞在 recv()/send() 里，
     那两处看不见停止标志，只能等 TCP keepalive 超时（几十秒）。
     先把 socket 读写都断掉，让阻塞的调用立刻以错误返回。
     两个方向分别关：只关一个，另一个方向的阻塞调用照样卡住。 */
  if (sock_ != nullptr) {
    try {
      sock_->shutdownRead();
    } catch (...) {
    }
    try {
      sock_->shutdownWrite();
    } catch (...) {
      /* 连接已经断了；继续 join 即可。 */
    }
  }

  loopThread_.join();
}

void VncSession::runLoop()
{
  vlog.debug("Event loop started");

  while (!stop_.load()) {
    /* 1) 执行排队的输入/操作。 */
    if (!drainInput())
      break;

    if (stop_.load())
      break;

    /* 2) 冲刷出站数据。 */
    if (!flushOutput())
      break;

    /* 3) 等待网络可读（带超时，兼作停止轮询）。 */
    int fd = sock_->getFd();

    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(static_cast<decltype(fd)>(fd), &rfds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = pollTimeoutUsec;

    int n = select(fd + 1, &rfds, nullptr, nullptr, &tv);

    if (stop_.load())
      break;

    if (n < 0) {
#ifdef _WIN32
      if (WSAGetLastError() == WSAEINTR)
        continue;
#else
      if (errno == EINTR)
        continue;
#endif
      reportError("select() failed", 1);
      break;
    }

    if (n == 0)
      continue;

    /* 4) 处理所有当前可用的消息。 */
    if (!processNetwork())
      break;
  }

  vlog.debug("Event loop stopped");

  /* 有人在回调里销毁了会话：现在循环已经退出，由本线程负责释放。
     detach 再 delete —— joinable 的 thread 直接析构会 terminate，
     而这里删的是 this，成员 loopThread_ 也跟着析构，所以必须先 detach。 */
  if (deferredDelete_.load()) {
    loopThread_.detach();
    delete this;
  }
}

bool VncSession::drainInput()
{
  std::deque<std::function<void()>> local;

  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    local.swap(queue_);
  }

  for (auto& fn : local) {
    if (stop_.load())
      break;
    try {
      fn();
    } catch (std::exception& e) {
      reportError(e.what(), 1);
      return false;
    } catch (...) {
      reportError("input dispatch failed", 1);
      return false;
    }
  }

  return true;
}

bool VncSession::flushOutput() noexcept
{
  try {
    if (sock_->outStream().hasBufferedData())
      sock_->outStream().flush();
    return true;
  } catch (std::exception& e) {
    reportError(e.what(), 1);
  } catch (...) {
    reportError("failed to flush output", 1);
  }
  return false;
}

bool VncSession::processNetwork()
{
  while (!stop_.load()) {
    bool again = false;

    try {
      again = processMsg();
    } catch (rdr::end_of_stream& e) {
      reportError(e.what(), 1);
      return false;
    } catch (rfb::auth_cancelled&) {
      reportError("Authentication cancelled", 1);
      return false;
    } catch (rfb::auth_error& e) {
      reportError(e.what(), 1);
      return false;
    } catch (std::exception& e) {
      reportError(e.what(), 1);
      return false;
    } catch (...) {
      reportError("unknown protocol error", 1);
      return false;
    }

    if (!again)
      break;
  }

  return true;
}

void VncSession::enqueue(std::function<void()> fn)
{
  {
    std::lock_guard<std::mutex> lock(queueMutex_);
    /* 事件循环卡住（正阻塞在 recv/send 里）时，持续发送的键鼠事件会
       无限堆积。设上限，满了直接丢弃——积压到这个程度数据已经没意义，
       继续攒只会把内存吃光。 */
    if (queue_.size() >= kMaxQueuedEvents) {
      ++droppedEvents_;
      return;
    }
    queue_.push_back(std::move(fn));
  }
}

void VncSession::reportError(const std::string& msg, int fatal)
{
  vlog.error("%s", msg.c_str());
  if (cb_.on_error == nullptr)
    return;

  /* 回调绝不能让异常穿透内部线程（那会触发 std::terminate）。 */
  try {
    cb_.on_error(cb_.ud, msg.c_str(), fatal);
  } catch (...) {
  }
}

template <typename Fn>
bool VncSession::safeCallback(const char* name, Fn&& fn)
{
  /* 托管侧回调（Dispatcher.Invoke 被取消、OutOfMemory 等）随时可能抛异常。
     异常一旦穿透 C++ 栈，/EHsc 下栈上对象不会析构，协议状态和 rdr/rfb
     对象全部泄漏；没被接住就直接 std::terminate 整个进程。
     所有回调都必须经过这里。 */
  try {
    fn();
    return true;
  } catch (const std::exception& e) {
    vlog.error("%s callback threw: %s", name, e.what());
  } catch (...) {
    vlog.error("%s callback threw an unknown exception", name);
  }
  return false;
}

//////////////////////////// 帧缓冲 ////////////////////////////

void VncSession::setFramebuffer(uint8_t* data, int width, int height,
                                int stride)
{
  std::lock_guard<std::mutex> lock(fbMutex_);
  fb_ = data;
  fbWidth_ = width;
  fbHeight_ = height;
  fbStride_ = stride;
}

void VncSession::copyToExternal()
{
  std::lock_guard<std::mutex> lock(fbMutex_);

  if (internalFb_ == nullptr)
    return;

  // 发布独立快照，UI 不读取仍在解码的缓冲。
  const int sw = internalFb_->width();
  const int sh = internalFb_->height();
  if (sw <= 0 || sh <= 0 || sw > 32768 || sh > 32768 ||
      static_cast<size_t>(sw) * sh > 67108864)
    throw std::runtime_error("framebuffer exceeds supported size");
  int sourceStride = 0;
  const uint8_t* pixels = internalFb_->getBuffer(internalFb_->getRect(), &sourceStride);
  snapshot_.resize(static_cast<size_t>(sw) * sh * 4);
  for (int row = 0; row < sh; ++row)
    std::memcpy(snapshot_.data() + static_cast<size_t>(row) * sw * 4,
                pixels + static_cast<size_t>(row) * sourceStride * 4,
                static_cast<size_t>(sw) * 4);
  snapshotWidth_ = sw;
  snapshotHeight_ = sh;
  if (fb_ == nullptr)
    return;

  int width = std::min(fbWidth_, internalFb_->width());
  int height = std::min(fbHeight_, internalFb_->height());

  if (width <= 0 || height <= 0 || fbStride_ < width * 4)
    return;

  int srcStridePx = 0;
  const uint8_t* src =
      internalFb_->getBuffer(internalFb_->getRect(), &srcStridePx);
  const int srcStrideBytes = srcStridePx * 4;
  const int rowBytes = width * 4;

  if (fbStride_ == rowBytes && srcStrideBytes == rowBytes) {
    std::memcpy(fb_, src, static_cast<size_t>(rowBytes) * height);
    return;
  }

  for (int y = 0; y < height; y++) {
    std::memcpy(fb_ + static_cast<size_t>(y) * fbStride_,
                src + static_cast<size_t>(y) * srcStrideBytes,
                rowBytes);
  }
}

int VncSession::copyFrame(uint8_t* data, size_t capacity, int stride,
                          int* width, int* height)
{
  if (!width || !height) return -1;
  std::lock_guard<std::mutex> lock(fbMutex_);
  *width = snapshotWidth_;
  *height = snapshotHeight_;
  if (snapshot_.empty()) return 1;
  if (!data || stride < snapshotWidth_ * 4 ||
      capacity < static_cast<size_t>(stride) * snapshotHeight_) return -1;
  for (int row = 0; row < snapshotHeight_; ++row)
    std::memcpy(data + static_cast<size_t>(row) * stride,
                snapshot_.data() + static_cast<size_t>(row) * snapshotWidth_ * 4,
                static_cast<size_t>(snapshotWidth_) * 4);
  return 0;
}

//////////////////////////// 输入 / 操作 ////////////////////////////

void VncSession::sendKey(int systemKeyCode, uint32_t keyCode,
                         uint32_t keySym)
{
  enqueue([this, systemKeyCode, keyCode, keySym]() {
    if (state() == RFBSTATE_NORMAL)
      sendKeyPress(systemKeyCode, keyCode, keySym);
  });
}

void VncSession::sendKeyRelease(int systemKeyCode)
{
  enqueue([this, systemKeyCode]() {
    if (state() == RFBSTATE_NORMAL)
      rfb::CConnection::sendKeyRelease(systemKeyCode);
  });
}

void VncSession::sendPointer(int x, int y, uint16_t buttonmask)
{
  enqueue([this, x, y, buttonmask]() {
    if (state() == RFBSTATE_NORMAL)
      sendPointerEvent(core::Point(x, y), buttonmask);
  });
}

void VncSession::refresh()
{
  enqueue([this]() {
    if (state() == RFBSTATE_NORMAL)
      refreshFramebuffer();
  });
}

void VncSession::requestClipboard()
{
  enqueue([this]() {
    if (state() == RFBSTATE_NORMAL)
      rfb::CConnection::requestClipboard();
  });
}

void VncSession::announceClipboard(int available)
{
  enqueue([this, available]() {
    if (state() == RFBSTATE_NORMAL)
      rfb::CConnection::announceClipboard(available != 0);
  });
}

void VncSession::sendClipboard(const char* text)
{
  std::string value = (text != nullptr) ? text : "";
  enqueue([this, value]() {
    if (state() == RFBSTATE_NORMAL)
      rfb::CConnection::sendClipboardData(value.c_str());
  });
}

void VncSession::setEncoding(int encoding)
{
  enqueue([this, encoding]() {
    if (state() == RFBSTATE_NORMAL)
      setPreferredEncoding(encoding);
  });
}

void VncSession::setCompressLevel(int level)
{
  enqueue([this, level]() {
    if (state() == RFBSTATE_NORMAL)
      rfb::CConnection::setCompressLevel(level);
  });
}

void VncSession::setQualityLevel(int level)
{
  enqueue([this, level]() {
    if (state() == RFBSTATE_NORMAL)
      rfb::CConnection::setQualityLevel(level);
  });
}

void VncSession::setSharedFlag(bool shared)
{
  /* 必须同步设置：ClientInit 在 connect 里就已经发出去了，
     排进队列会晚于协议初始化，对本次会话无效。 */
  setShared(shared);
}

int VncSession::setSecurityTypes(const char* types)
{
  /* 同样必须同步：安全类型在 SecurityTypes 消息处理时读取，
     晚于事件循环启动就来不及。 */
  if (types == nullptr)
    return -1;

  std::list<uint32_t> enabled;
  const char* cursor = types;
  while (*cursor != '\0') {
    const char* comma = std::strchr(cursor, ',');
    const size_t length = comma != nullptr
        ? static_cast<size_t>(comma - cursor) : std::strlen(cursor);

    /* 去掉两端空白，和 ListParameter::setParam 的解析保持一致。 */
    size_t begin = 0;
    while (begin < length && std::isspace(static_cast<unsigned char>(cursor[begin])))
      begin++;
    size_t end = length;
    while (end > begin && std::isspace(static_cast<unsigned char>(cursor[end - 1])))
      end--;

    if (end > begin) {
      std::string name(cursor + begin, end - begin);

      /* 必须对照「本核心实际编译进去的枚举表」校验，而不是 secTypeNum。
         secTypeNum 认识所有协议名，但 GnuTLS/Nettle 没编进来时
         对应类型在握手阶段才会抛 "Security type not supported"——
         用户只会看到连不上，不知道为什么。这里提前拒绝，整份失败，
         绝不静默丢掉某一项（否则会出现「以为启用了 VncAuth，
         实际只剩 None」这种降级）。
         枚举表的默认值就是构造时传入的「编译进去的类型」，
         直接用 getDefaultStr() 取逗号分隔串再比对，避免依赖迭代器类型。 */
      if (!containsSecurityName(name))
        return -1;

      enabled.push_back(rfb::secTypeNum(name.c_str()));
    }

    if (comma == nullptr)
      break;
    cursor = comma + 1;
  }

  if (enabled.empty())
    return -1;

  security.SetSecTypes(enabled);
  return 0;
}

std::string VncSession::availableSecurityTypes()
{
  /* 枚举表的默认值就是「本核心编译进去的安全类型」，
     正好给界面用来把不支持的勾选项置灰。 */
  return rfb::SecurityClient::secTypes.getValueStr();
}

void VncSession::setX509CA(const char* path)
{
#ifdef HAVE_GNUTLS
  if (path != nullptr)
    rfb::CSecurityTLS::X509CA.setParam(path);
#else
  (void)path;
#endif
}

void VncSession::setX509CRL(const char* path)
{
#ifdef HAVE_GNUTLS
  if (path != nullptr)
    rfb::CSecurityTLS::X509CRL.setParam(path);
#else
  (void)path;
#endif
}

void VncSession::requestScreenLayout(int width, int height,
                                     const int* screens, int count)
{
  std::vector<int> copy;
  if (screens != nullptr && count > 0)
    copy.assign(screens, screens + static_cast<size_t>(count) * 4);

  enqueue([this, width, height, copy]() {
    rfb::ScreenSet layout;
    const int screenCount = static_cast<int>(copy.size() / 4);
    for (int i = 0; i < screenCount; i++) {
      const int* s = &copy[static_cast<size_t>(i) * 4];
      layout.add_screen(rfb::Screen(static_cast<uint32_t>(i),
                                    s[0], s[1], s[2], s[3], 0));
    }
    rfb::CConnection::requestScreenLayout(width, height, layout);
  });
}

//////////////////////////// CConnection 回调 ////////////////////////////

void VncSession::getUserPasswd(std::string* user, std::string* password)
{
  if (cb_.on_credentials == nullptr) {
    vlog.error("Server requires credentials but no callback is registered");
    throw rfb::auth_cancelled();
  }

  std::vector<char> userBuf(credentialBufSize, '\0');
  std::vector<char> passBuf(credentialBufSize, '\0');

  int ok = 0;
  /* 凭据回调嵌在认证协议栈最深处，异常穿透的破坏最大。 */
  if (!safeCallback("on_credentials", [&]() {
        ok = cb_.on_credentials(cb_.ud, host_.c_str(), isSecure() ? 1 : 0,
                                user ? userBuf.data() : nullptr, user ? userBuf.size() : 0,
                                password ? passBuf.data() : nullptr,
                                password ? passBuf.size() : 0);
      })) {
    throw rfb::auth_cancelled();
  }
  userBuf.back() = '\0';
  passBuf.back() = '\0';

  if (ok == 0)
    throw rfb::auth_cancelled();

  if (password != nullptr)
    *password = std::string(passBuf.data());
  if (user != nullptr)
    *user = std::string(userBuf.data());
}

bool VncSession::verifyCertificate(unsigned int status,
                                   const uint8_t* certificate,
                                   size_t length)
{
  if (cb_.on_verify_cert == nullptr) {
    vlog.error("Certificate verification requested but no callback registered");
    return false;
  }

  /* 验证失败（含回调抛异常）一律按「拒绝」处理，绝不放行。 */
  bool accepted = false;
  if (!safeCallback("on_verify_cert", [&]() {
        accepted = cb_.on_verify_cert(cb_.ud, status, certificate, length) != 0;
      }))
    return false;
  return accepted;
}

bool VncSession::verifyHostKey(const uint8_t* key, size_t length,
                               const char* fingerprint)
{
  if (cb_.on_verify_hostkey == nullptr) {
    vlog.error("Host key verification requested but no callback registered");
    return false;
  }

  bool accepted = false;
  if (!safeCallback("on_verify_hostkey", [&]() {
        accepted = cb_.on_verify_hostkey(cb_.ud, key, length, fingerprint) != 0;
      }))
    return false;
  return accepted;
}

void VncSession::initDone()
{
  const int width = server.width();
  const int height = server.height();

  vlog.info("Connection established, remote desktop %dx%d", width, height);

  auto* fb = new rfb::ManagedPixelBuffer(fixedPF, width, height);
  rfb::CConnection::setFramebuffer(fb);
  internalFb_ = fb;

  /* 强制服务器切换到我们固定的 32bpp BGRX 格式。 */
  setPF(fixedPF);

  if (cb_.on_server_init != nullptr) {
    safeCallback("on_server_init", [&]() {
      cb_.on_server_init(cb_.ud, width, height, server.name(),
                         32, 24, 16, 8, 0);
    });
  }
}

void VncSession::resizeFramebuffer()
{
  const int width = server.width();
  const int height = server.height();

  if (width <= 0 || height <= 0)
    return;

  auto* fb = new rfb::ManagedPixelBuffer(fixedPF, width, height);
  rfb::CConnection::setFramebuffer(fb);
  internalFb_ = fb;

  if (cb_.on_resize != nullptr)
    safeCallback("on_resize", [&]() { cb_.on_resize(cb_.ud, width, height); });
}

void VncSession::setExtendedDesktopSize(unsigned reason, unsigned result,
                                        int w, int h,
                                        const rfb::ScreenSet& layout)
{
  /* 基类会在需要时调用 resizeFramebuffer()，由它触发 on_resize。 */
  rfb::CConnection::setExtendedDesktopSize(reason, result, w, h, layout);
}

void VncSession::setCursor(int width, int height,
                           const core::Point& hotspot,
                           const uint8_t* data)
{
  rfb::CConnection::setCursor(width, height, hotspot, data);

  if (cb_.on_cursor != nullptr) {
    safeCallback("on_cursor", [&]() {
      cb_.on_cursor(cb_.ud, width, height, hotspot.x, hotspot.y, data);
    });
  }
}

void VncSession::setCursorPos(const core::Point& pos)
{
  if (cb_.on_cursor_pos != nullptr)
    safeCallback("on_cursor_pos", [&]() { cb_.on_cursor_pos(cb_.ud, pos.x, pos.y); });
}

void VncSession::setName(const char* name)
{
  rfb::CConnection::setName(name);

  if (cb_.on_name != nullptr)
    safeCallback("on_name", [&]() { cb_.on_name(cb_.ud, name); });
}

void VncSession::bell()
{
  if (cb_.on_bell != nullptr)
    safeCallback("on_bell", [&]() { cb_.on_bell(cb_.ud); });
}

void VncSession::fence(uint32_t flags, unsigned len, const uint8_t data[])
{
  rfb::CConnection::fence(flags, len, data);
}

void VncSession::endOfContinuousUpdates()
{
  rfb::CConnection::endOfContinuousUpdates();
}

void VncSession::serverInit(int width, int height,
                            const rfb::PixelFormat& pf, const char* name)
{
  rfb::CConnection::serverInit(width, height, pf, name);
}

bool VncSession::readAndDecodeRect(const core::Rect& r, int encoding,
                                    const rfb::PixelFormat& pf,
                                    rfb::ModifiablePixelBuffer* pb)
{
  return rfb::CConnection::readAndDecodeRect(r, encoding, pf, pb);
}

void VncSession::framebufferUpdateStart()
{
  /* 记下这一帧开始时的字节位置和时间，用来算吞吐。 */
  if (sock_ != nullptr) {
    try { updateStartPos_ = sock_->inStream().pos(); } catch (...) { }
  }
  updateStartTime_ = std::chrono::steady_clock::now();

  rfb::CConnection::framebufferUpdateStart();
}

void VncSession::framebufferUpdateEnd()
{
  rfb::CConnection::framebufferUpdateEnd();

  copyToExternal();

  statUpdates_.fetch_add(1, std::memory_order_relaxed);

  /* 在事件循环线程里累加字节数：跨线程读 inStream().pos() 是竞态。 */
  if (sock_ != nullptr) {
    try {
      const unsigned long long pos = sock_->inStream().pos();
      const unsigned long long previous = updateStartPos_;
      if (pos >= previous)
        statBytes_.fetch_add(pos - previous, std::memory_order_relaxed);
    } catch (...) {
    }
  }

  updateBandwidthEstimate();

  if (cb_.on_frame_update != nullptr) {
    safeCallback("on_frame_update", [&]() {
      cb_.on_frame_update(cb_.ud, 0, 0, server.width(), server.height());
    });
  }
}

void VncSession::updateBandwidthEstimate()
{
  /* 逐条对照原版 CConn::framebufferUpdateEnd：用本次更新期间收到的
     字节数除以耗时得到瞬时带宽，再用带权重的指数滑动平均平滑。
     耗时越长权重越大（最多 20%），避免单帧抖动把估算带偏。 */
  if (sock_ == nullptr)
    return;

  unsigned long long nowPos = 0;
  try { nowPos = sock_->inStream().pos(); } catch (...) { return; }

  const auto now = std::chrono::steady_clock::now();
  const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
      now - updateStartTime_).count();
  const unsigned long long elapsed = elapsedUs > 0
      ? static_cast<unsigned long long>(elapsedUs) : 1ULL;

  /* 计数器可能回退（重连后归零），先判大小再相减，别绕回天文数字。 */
  const unsigned long long delta = nowPos >= updateStartPos_
      ? nowPos - updateStartPos_ : 0ULL;
  const unsigned long long bps = delta * 8ULL * 1000000ULL / elapsed;

  /* 权重 = 耗时 / 窗口，单位换算成百万分之一，上限 20%。
     对应原版 weight = elapsed * 1000 / bpsEstimateWindow，clamp 到 200000。 */
  constexpr unsigned long long kWindowUs = 1000000ULL;   /* 1 秒 */
  unsigned long long weight = elapsed * 1000000ULL / kWindowUs;
  if (weight > 200000ULL)
    weight = 200000ULL;

  const unsigned long long previous = bpsEstimate_.load(std::memory_order_relaxed);
  const unsigned long long smoothed =
      (previous * (1000000ULL - weight) + bps * weight) / 1000000ULL;
  bpsEstimate_.store(smoothed, std::memory_order_relaxed);

  /* 自动选择质量：12~16 Mbps 是滞回带，带宽在阈值附近抖动时
     不会每帧反复切换。对照原版 CConn::updateQualityLevel。 */
  if (autoSelectQuality_.load()) {
    const int current = getQualityLevel();
    int next;
    if (current == 8)
      next = (smoothed < 12000000ULL) ? 6 : 8;
    else
      next = (smoothed > 16000000ULL) ? 8 : 6;
    if (next != current) {
      try { setQualityLevel(next); } catch (...) { }
    }
  }
}

void VncSession::setAutoSelectQuality(bool enabled)
{
  autoSelectQuality_.store(enabled);
}

bool VncSession::dataRect(const core::Rect& r, int encoding)
{
  bool ret = rfb::CConnection::dataRect(r, encoding);
  /* 只有真正解码成功的矩形才计入像素数，和原版 CConn::dataRect 一致。 */
  if (ret)
    statPixels_.fetch_add(static_cast<unsigned>(r.area()),
                          std::memory_order_relaxed);
  return ret;
}

VncSession::Stats VncSession::copyStats() const
{
  Stats stats{};
  stats.updates = statUpdates_.load(std::memory_order_relaxed);
  stats.pixels = statPixels_.load(std::memory_order_relaxed);
  stats.bitsPerSecond = bpsEstimate_.load(std::memory_order_relaxed);
  /* 累计字节数用 64 位，避免大流量（>4GB）后回绕清零。 */
  const unsigned long long total = statBytes_.load(std::memory_order_relaxed);
  stats.bytes = total > 0xFFFFFFFFULL ? 0xFFFFFFFFu : static_cast<unsigned>(total);
  return stats;
}

void VncSession::setColourMapEntries(int firstColour, int nColours,
                                     uint16_t* rgbs)
{
  rfb::CConnection::setColourMapEntries(firstColour, nColours, rgbs);
}

void VncSession::serverCutText(const char* str)
{
  rfb::CConnection::serverCutText(str);

  if (cb_.on_clipboard != nullptr)
    safeCallback("on_clipboard", [&]() { cb_.on_clipboard(cb_.ud, str); });
}

void VncSession::setLEDState(unsigned int state)
{
  rfb::CConnection::setLEDState(state);

  if (cb_.on_led != nullptr)
    safeCallback("on_led", [&]() { cb_.on_led(cb_.ud, state); });
}

void VncSession::clipboardCaps(uint32_t flags, const uint32_t* lengths)
{
  rfb::CConnection::clipboardCaps(flags, lengths);
}

void VncSession::clipboardRequest(uint32_t flags)
{
  rfb::CConnection::clipboardRequest(flags);
}

void VncSession::clipboardPeek()
{
  rfb::CConnection::clipboardPeek();
}

void VncSession::clipboardNotify(uint32_t flags)
{
  rfb::CConnection::clipboardNotify(flags);
}

void VncSession::clipboardProvide(uint32_t flags, const size_t* lengths,
                                  const uint8_t* const* data)
{
  rfb::CConnection::clipboardProvide(flags, lengths, data);
}

/* 扩展剪贴板：服务器请求我们发送本地剪贴板。 */
void VncSession::handleClipboardRequest()
{
  if (cb_.on_clipboard_request != nullptr)
    safeCallback("on_clipboard_request", [&]() { cb_.on_clipboard_request(cb_.ud); });
}

/* 扩展剪贴板：远端剪贴板内容有变化（或已清空）。 */
void VncSession::handleClipboardAnnounce(bool available)
{
  if (cb_.on_clipboard_announce != nullptr) {
    safeCallback("on_clipboard_announce", [&]() {
      cb_.on_clipboard_announce(cb_.ud, available ? 1 : 0);
    });
  }
}

/* 扩展剪贴板：服务器把内容送过来了。和旧版 ServerCutText 走同一个出口。 */
void VncSession::handleClipboardData(const char* data)
{
  if (cb_.on_clipboard != nullptr)
    safeCallback("on_clipboard", [&]() { cb_.on_clipboard(cb_.ud, data); });
}

void VncSession::audioBegin()
{
  rfb::CConnection::audioBegin();
}

void VncSession::audioEnd()
{
  rfb::CConnection::audioEnd();
}

void VncSession::audioData(const uint8_t* data, size_t length)
{
  rfb::CConnection::audioData(data, length);
}

void VncSession::handleAudioBegin()
{
  /* CConnection 在真正收到 QEMU 音频伪编码后才调这里。
     没有后端（没开音频 / 没声卡）就安静忽略，不让协议层报错。 */
  if (audio_ == nullptr)
    return;
  audio_->start();
}

void VncSession::handleAudioEnd()
{
  if (audio_ == nullptr)
    return;
  audio_->stop();
}

void VncSession::handleAudioData(const uint8_t* data, size_t length)
{
  if (audio_ == nullptr || data == nullptr || length == 0)
    return;
  audio_->play(data, length);
}

void VncSession::setAudioEnabled(bool enabled)
{
  /* 必须在 connect 之前调用：supportsAudio 决定 SetEncodings 里
     要不要带 QEMU 音频伪编码，晚于初始化服务器根本不会送音频。 */
  audioEnabled_.store(enabled);
  if (enabled && audio_ == nullptr) {
    audio_ = AudioOutput::create();
    if (audio_ == nullptr) {
      /* 没有可用播放设备：保持关闭，不假装支持。 */
      audioEnabled_.store(false);
      vlog.info("No audio playback device available; audio disabled");
    }
  }
  /* 能力位在这里同步设置，connect 之后才设会太晚。 */
  supportsAudio = audioEnabled_.load();
}

bool VncSession::isAudioAvailable() const
{
  return audio_ != nullptr;
}
