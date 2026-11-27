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
#include <cstring>
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

#include <network/Socket.h>
#include <network/TcpSocket.h>

#include "VncSession.h"

static core::LogWriter vlog("VncSession");

/* 固定像素格式：32bpp，小端内存布局 B,G,R,X。 */
static const rfb::PixelFormat fixedPF(32, 24, false, true,
                                      255, 255, 255,
                                      16, 8, 0);

/* 事件循环等待网络可读的超时（微秒）。同时用作停止请求的轮询间隔。 */
static const int pollTimeoutUsec = 100 * 1000;

/* 凭据缓冲大小（用户名/密码）。 */
static const size_t credentialBufSize = 512;

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
    if (error != nullptr)
      *error = "unknown error";
    if (sock_ != nullptr) {
      delete sock_;
      sock_ = nullptr;
    }
    return false;
  }
}

void VncSession::startLoop()
{
  if (sock_ == nullptr)
    return;

  if (loopStarted_.exchange(true))
    return;

  loopThread_ = std::thread([this]() { runLoop(); });
}

void VncSession::requestStop()
{
  stop_.store(true);

  if (!loopThread_.joinable())
    return;

  /* 不允许在事件循环线程里 join 自己（例如回调里同步调用 vnc_stop）。 */
  if (std::this_thread::get_id() == loopThread_.get_id())
    return;

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

  if (fb_ == nullptr || internalFb_ == nullptr)
    return;

  int width = std::min(fbWidth_, internalFb_->width());
  int height = std::min(fbHeight_, internalFb_->height());

  if (width <= 0 || height <= 0 || fbStride_ <= 0)
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

//////////////////////////// 输入 / 操作 ////////////////////////////

void VncSession::sendKey(int systemKeyCode, uint32_t keyCode,
                         uint32_t keySym)
{
  enqueue([this, systemKeyCode, keyCode, keySym]() {
    sendKeyPress(systemKeyCode, keyCode, keySym);
  });
}

void VncSession::sendKeyRelease(int systemKeyCode)
{
  enqueue([this, systemKeyCode]() {
    rfb::CConnection::sendKeyRelease(systemKeyCode);
  });
}

void VncSession::sendPointer(int x, int y, uint16_t buttonmask)
{
  enqueue([this, x, y, buttonmask]() {
    sendPointerEvent(core::Point(x, y), buttonmask);
  });
}

void VncSession::refresh()
{
  enqueue([this]() { refreshFramebuffer(); });
}

void VncSession::requestClipboard()
{
  enqueue([this]() { rfb::CConnection::requestClipboard(); });
}

void VncSession::announceClipboard(int available)
{
  enqueue([this, available]() {
    rfb::CConnection::announceClipboard(available != 0);
  });
}

void VncSession::sendClipboard(const char* text)
{
  std::string value = (text != nullptr) ? text : "";
  enqueue([this, value]() {
    rfb::CConnection::sendClipboardData(value.c_str());
  });
}

void VncSession::setEncoding(int encoding)
{
  enqueue([this, encoding]() { setPreferredEncoding(encoding); });
}

void VncSession::setCompressLevel(int level)
{
  enqueue([this, level]() { rfb::CConnection::setCompressLevel(level); });
}

void VncSession::setQualityLevel(int level)
{
  enqueue([this, level]() { rfb::CConnection::setQualityLevel(level); });
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

  int ok = cb_.on_credentials(cb_.ud, host_.c_str(), isSecure() ? 1 : 0,
                              userBuf.data(), userBuf.size(),
                              passBuf.data(), passBuf.size());

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

  return cb_.on_verify_cert(cb_.ud, status, certificate, length) != 0;
}

bool VncSession::verifyHostKey(const uint8_t* key, size_t length,
                               const char* fingerprint)
{
  if (cb_.on_verify_hostkey == nullptr) {
    vlog.error("Host key verification requested but no callback registered");
    return false;
  }

  return cb_.on_verify_hostkey(cb_.ud, key, length, fingerprint) != 0;
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
    cb_.on_server_init(cb_.ud, width, height, server.name(),
                       32, 24, 16, 8, 0);
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
    cb_.on_resize(cb_.ud, width, height);
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

  if (cb_.on_cursor != nullptr)
    cb_.on_cursor(cb_.ud, width, height, hotspot.x, hotspot.y, data);
}

void VncSession::setCursorPos(const core::Point& pos)
{
  if (cb_.on_cursor_pos != nullptr)
    cb_.on_cursor_pos(cb_.ud, pos.x, pos.y);
}

void VncSession::setName(const char* name)
{
  rfb::CConnection::setName(name);

  if (cb_.on_name != nullptr)
    cb_.on_name(cb_.ud, name);
}

void VncSession::bell()
{
  if (cb_.on_bell != nullptr)
    cb_.on_bell(cb_.ud);
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
  rfb::CConnection::framebufferUpdateStart();
}

void VncSession::framebufferUpdateEnd()
{
  rfb::CConnection::framebufferUpdateEnd();

  copyToExternal();

  if (cb_.on_frame_update != nullptr)
    cb_.on_frame_update(cb_.ud, 0, 0, server.width(), server.height());
}

bool VncSession::dataRect(const core::Rect& r, int encoding)
{
  return rfb::CConnection::dataRect(r, encoding);
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
    cb_.on_clipboard(cb_.ud, str);
}

void VncSession::setLEDState(unsigned int state)
{
  rfb::CConnection::setLEDState(state);

  if (cb_.on_led != nullptr)
    cb_.on_led(cb_.ud, state);
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
