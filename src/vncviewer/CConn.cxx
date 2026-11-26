/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
 * Copyright 2009-2014 Pierre Ossman for Cendio AB
 *
 * 本软件为自由软件；你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （GNU General Public License）的条款（第 2 版，或你选择的任何更高版本）
 * 重新分发和/或修改本软件。
 *
 * 本软件的发布是希望它有用，但不附带任何担保；甚至不附带适销性或
 * 特定用途适用性的默示担保。详见 GNU 通用公共许可证。
 *
 * 你应已收到随本软件一同提供的 GNU 通用公共许可证副本；如果没有，
 * 请致信自由软件基金会：Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA。
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
/* usleep 在 Windows 上由 src/contrib/msvc/include/unistd.h 提供包装
 * （内部转成 Sleep），Linux 用系统头，两边都无需平台分支。 */
#include <unistd.h>

#ifdef HAVE_GNUTLS
#include <gnutls/x509.h>
#endif

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/Timer.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/time.h>
#include <core/xdgdirs.h>

#include <rdr/FdInStream.h>
#include <rdr/FdOutStream.h>
#include <rdr/TLSException.h>

#include <rfb/CMsgWriter.h>
#include <rfb/CSecurity.h>
#include <rfb/Exception.h>
#include <rfb/Security.h>
#include <rfb/fenceTypes.h>
#include <rfb/obfuscate.h>
#include <rfb/screenTypes.h>

#include <network/TcpSocket.h>
#ifndef WIN32
#include <network/UnixSocket.h>
#endif

#include <FL/Fl.H>
#include <FL/fl_ask.H>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "AudioOutput.h"
#include "AuthDialog.h"
#include "CConn.h"
#include "OptionsDialog.h"
#include "DesktopWindow.h"
#include "PlatformPixelBuffer.h"
#include "parameters.h"
#include "vncviewer.h"

std::string CConn::savedUsername;
std::string CConn::savedPassword;

static core::LogWriter vlog("CConn");

// 8 色（每个分量 1 位）
static const rfb::PixelFormat verylowColourPF(8, 3,false, true,
                                              1, 1, 1, 2, 1, 0);
// 64 色（每个分量 2 位）
static const rfb::PixelFormat lowColourPF(8, 6, false, true,
                                          3, 3, 3, 4, 2, 0);
// 256 色（每个分量 2-3 位）
static const rfb::PixelFormat mediumColourPF(8, 8, false, true,
                                             7, 7, 3, 5, 2, 0);

// 新的带宽估计值所依据的加权时间窗口（单位：毫秒）
static const unsigned bpsEstimateWindow = 1000;

CConn::CConn()
  : serverPort(0), sock(nullptr),
    msgTimer(this, &CConn::processNextMsg), desktop(nullptr),
    audioOutput(nullptr),
    updateCount(0), pixelCount(0),
    lastServerEncoding((unsigned int)-1), bpsEstimate(20000000)
{
  setShared(::shared);

  supportsLocalCursor = true;
  supportsCursorPosition = true;
  supportsDesktopResize = true;
  supportsLEDState = true;

#ifdef HAVE_AUDIO
  // 只有在存在可播放音频的设备时才向服务器请求音频。
  // 打开该设备的操作会推迟到服务器确实有音频要发送时再进行。
  if (playAudio) {
    audioOutput = AudioOutput::create();
    supportsAudio = audioOutput != nullptr;
  }
#endif

  if (customCompressLevel)
    setCompressLevel(::compressLevel);

  setQualityLevel(::qualityLevel);

  OptionsDialog::addCallback(handleOptions, this);
}

CConn::~CConn()
{
  close();

  OptionsDialog::removeCallback(handleOptions);
  Fl::remove_timeout(handleUpdateTimeout, this);

  if (desktop)
    delete desktop;

  delete audioOutput;

  if (sock) {
    struct timeval now;

    sock->shutdownWrite();

    // 通过等待对端（最多 250 毫秒）来优雅关闭
    // FIXME: 应以异步方式完成
    gettimeofday(&now, nullptr);
    while (core::msSince(&now) < 250) {
      bool done;

      done = false;
      while (true) {
        try {
          sock->inStream().skip(sock->inStream().avail());
          if (!sock->inStream().hasData(1))
            break;
        } catch (std::exception&) {
          done = true;
          break;
        }
      }

      if (done)
        break;

      /* usleep 来自文件顶部统一 include 的 unistd.h（Windows 上是
       * contrib 的 Sleep 包装），无需再按平台分支。 */
      usleep(10000);
    }

    Fl::remove_fd(sock->getFd());

    delete sock;
  }
}

void CConn::connect(const char* vncServerName, network::Socket* socket)
{
  vlog.info(_("开始连接 %s ..."),
            vncServerName ? vncServerName : _("(socket)"));
  sock = socket;
  if(sock == nullptr) {
    try {
#ifndef WIN32
      if (strchr(vncServerName, '/') != nullptr) {
        sock = new network::UnixSocket(vncServerName);
        serverHost = sock->getPeerAddress();
        vlog.info(_("Connected to socket %s"), serverHost.c_str());
      } else
#endif
      {
        network::getHostAndPort(vncServerName, &serverHost, &serverPort);

        network::TcpSocket *tcpSock =
            new network::TcpSocket(serverHost.c_str(), serverPort);
        /* TCP 保活：检测半开连接（网络闪断、NAT 超时），
         * 30 秒空闲后开始探测，探测间隔 5 秒。 */
        tcpSock->enableKeepAlive(true);
        sock = tcpSock;
        vlog.info(_("Connected to host %s port %d"),
                  serverHost.c_str(), serverPort);
      }
    } catch (std::exception& e) {
      vlog.error("%s", e.what());
      abort_connection(_("Failed to connect to \"%s\":\n\n%s"),
                       vncServerName, e.what());
      return;
    }
  }

  Fl::add_fd(sock->getFd(), FL_READ | FL_EXCEPT, socketEvent, this);

  setServerName(serverHost.c_str());
  setStreams(&sock->inStream(), &sock->outStream());

  initialiseProtocol();
}

std::string CConn::connectionInfo()
{
  std::string infoText;

  char pfStr[100];

  infoText += core::format(_("Desktop name: %.80s"), server.name());
  infoText += "\n";

  infoText += core::format(_("Host: %.80s port: %d"),
                           serverHost.c_str(), serverPort);
  infoText += "\n";

  infoText += core::format(_("Size: %d x %d"),
                           server.width(), server.height());
  infoText += "\n";

  // 翻译者注意：这里会填入一段以尽量语言无关的方式描述
  // 协议像素格式的字符串
  server.pf().print(pfStr, 100);
  infoText += core::format(_("Pixel format: %s"), pfStr);
  infoText += "\n";

  infoText += core::format(_("Requested encoding: %s"),
                           rfb::encodingName(getPreferredEncoding()));
  infoText += "\n";

  infoText += core::format(_("Last used encoding: %s"),
                           rfb::encodingName(lastServerEncoding));
  infoText += "\n";

  infoText += core::format(_("Line speed estimate: %d kbit/s"),
                           (int)(bpsEstimate / 1000));
  infoText += "\n";

  infoText += core::format(_("Protocol version: %d.%d"),
                           server.majorVersion, server.minorVersion);
  infoText += "\n";

  infoText += core::format(_("Security method: %s"),
                           rfb::secTypeName(csecurity->getType()));
  infoText += "\n";

  return infoText;
}

unsigned CConn::getUpdateCount()
{
  return updateCount;
}

unsigned CConn::getPixelCount()
{
  return pixelCount;
}

unsigned CConn::getPosition()
{
  return sock->inStream().pos();
}

void CConn::socketEvent(FL_SOCKET fd, void *data)
{
  CConn *cc;

  assert(data);
  cc = (CConn*)data;

  // 暂时停止监视该套接字，改为异步处理到达的数据
  Fl::remove_fd(fd);
  cc->msgTimer.start(0);

  // 先合并数据，直到我们彻底处理完毕
  cc->getOutStream()->cork(true);
}

void CConn::processNextMsg(core::Timer*)
{
  static bool recursing = false;
  bool again;
  int when;

  // 我认为 processMsg() 不是递归安全的，所以加上这个检查
  assert(!recursing);

  recursing = true;

  again = false;
  try {
    // 我们可能是为了冲刷尚未写出的套接字数据而被调用
    sock->outStream().flush();

    again = processMsg();
  } catch (rdr::end_of_stream& e) {
    vlog.info("%s", e.what());
    if (!desktop) {
      vlog.error(_("The connection was dropped by the server before "
                   "the session could be established."));
      abort_connection(_("The connection was dropped by the server "
                       "before the session could be established."));
    } else {
      disconnect();
    }
  } catch (rfb::auth_cancelled& e) {
    vlog.info("%s", e.what());
    disconnect();
  } catch (rfb::auth_error& e) {
    savedUsername.clear();
    savedPassword.clear();
    vlog.error(_("Authentication failed: %s"), e.what());
    abort_connection(_("Failed to authenticate with the server. Reason "
                       "given by the server:\n\n%s"), e.what());
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    abort_connection_unexpected(e);
  }

  recursing = false;

  if (again) {
    msgTimer.repeat();
    return;
  }

  // 数据已处理完，回到等待状态

  getOutStream()->cork(false);

  when = FL_READ | FL_EXCEPT;
  if (sock->outStream().hasBufferedData())
    when |= FL_WRITE;

  Fl::add_fd(sock->getFd(), when, socketEvent, this);
}

////////////////////// CConnection 回调方法 //////////////////////

void CConn::getUserPasswd(std::string *user, std::string *password)
{
  const char *passwordFileName(passwordFile);
  int ret_val;

  assert(password);
  char *envUsername = getenv("VNC_USERNAME");
  char *envPassword = getenv("VNC_PASSWORD");

  if(user && envUsername && envPassword) {
    *user = envUsername;
    *password = envPassword;
    return;
  }

  if (!user && envPassword) {
    *password = envPassword;
    return;
  }

  if (user && !savedUsername.empty() && !savedPassword.empty()) {
    *user = savedUsername;
    *password = savedPassword;
    return;
  }

  if (!user && !savedPassword.empty()) {
    *password = savedPassword;
    return;
  }

  if (!user && passwordFileName[0]) {
    std::vector<uint8_t> obfPwd(8);
    FILE* fp;

    fp = fopen(passwordFileName, "rb");
    if (!fp)
      throw core::posix_error(
        core::format(_("Failed to open \"%s\""), passwordFileName),
        errno);

    obfPwd.resize(fread(obfPwd.data(), 1, obfPwd.size(), fp));
    fclose(fp);

    *password = rfb::deobfuscate(obfPwd.data(), obfPwd.size());

    return;
  }

  AuthDialog d(isSecure(), user != nullptr, password != nullptr);
  d.show();
  while (d.shown())
    Fl::wait();
  ret_val = d.result();

  if (ret_val == 1) {
    bool keepPasswd;

    if (reconnectOnError)
      keepPasswd = d.getKeepPassword();
    else
      keepPasswd = false;

    if (user) {
      *user = d.getUser();
      if (keepPasswd)
        savedUsername = d.getUser();
    }
    *password = d.getPassword();
    if (keepPasswd)
      savedPassword = d.getPassword();
  }

  if (ret_val != 1)
    throw rfb::auth_cancelled();
}

// 收到 serverInit 消息后会调用 initDone()。此时我们创建桌面窗口并
// 显示它。我们还会告诉服务器要使用的像素格式和编码，并请求第一次更新。
void CConn::initDone()
{
  vlog.info(_("连接已建立，远端桌面 %dx%d"),
            server.width(), server.height());
  desktop = new DesktopWindow(server.width(), server.height(), this);

  // 强制切换到我们想要的格式和编码
  updateEncoding();
  updatePixelFormat();
}

bool CConn::verifyCertificate(unsigned int status,
                              const uint8_t* certificate, size_t length)
{
#ifndef HAVE_GNUTLS
  (void)status;
  (void)certificate;
  (void)length;
  throw std::logic_error("TLS support not enabled");
#else
  const unsigned allowed_errors =
    GNUTLS_CERT_INVALID |
    GNUTLS_CERT_SIGNER_NOT_FOUND |
    GNUTLS_CERT_SIGNER_NOT_CA |
    GNUTLS_CERT_NOT_ACTIVATED |
    GNUTLS_CERT_EXPIRED |
    GNUTLS_CERT_INSECURE_ALGORITHM |
    GNUTLS_CERT_UNEXPECTED_OWNER;
  gnutls_datum_t status_str;
  unsigned int fatal_status;

  gnutls_datum_t cert_datum;
  gnutls_x509_crt_t crt;
  int err, known;

  const char *hostsDir;
  gnutls_datum_t info_datum;
  std::string info;
  size_t len;

  assert(status != 0);

  fatal_status = status & (~allowed_errors);

  if (fatal_status != 0) {
    err = gnutls_certificate_verification_status_print(fatal_status,
                                                       GNUTLS_CRT_X509,
                                                       &status_str,
                                                       0);
    if (err != GNUTLS_E_SUCCESS)
      throw rdr::tls_error(_("Failed to get certificate problem "
                             "description"), err);

    abort_connection(_("Invalid server certificate: %s"),
                     status_str.data);

    gnutls_free(status_str.data);

    return false;
  }

  err = gnutls_certificate_verification_status_print(status,
                                                     GNUTLS_CRT_X509,
                                                     &status_str,
                                                     0);
  if (err != GNUTLS_E_SUCCESS)
    throw rdr::tls_error(_("Failed to get certificate problem "
                           "description"), err);

  vlog.info(_("Server certificate problems: %s"), status_str.data);

  gnutls_free(status_str.data);

  /* 证书存在一些用户可覆盖的问题，于是进入 TOFU 环节 */

  hostsDir = core::getvncstatedir();
  if (hostsDir == nullptr) {
    throw std::runtime_error(_("Could not determine VNC state "
                               "directory path"));
  }

  std::string dbPath;
  dbPath = (std::string)hostsDir + "/x509_known_hosts";

  cert_datum.data = (uint8_t*)certificate;
  cert_datum.size = length;

  known = gnutls_verify_stored_pubkey(dbPath.c_str(), nullptr,
                                      getServerName(), nullptr,
                                      GNUTLS_CRT_X509, &cert_datum, 0);

  /* 之前是否已知？ */
  if (known == GNUTLS_E_SUCCESS) {
    vlog.info(_("Server has an existing security exception"));
    return true;
  }

  if ((known != GNUTLS_E_NO_CERTIFICATE_FOUND) &&
      (known != GNUTLS_E_CERTIFICATE_KEY_MISMATCH))
    throw rdr::tls_error(_("Failed to load list of servers with a "
                           "security exception"), known);

  gnutls_x509_crt_init(&crt);
  err = gnutls_x509_crt_import(crt, &cert_datum, GNUTLS_X509_FMT_DER);
  if (err != GNUTLS_E_SUCCESS)
    throw rdr::tls_error(_("Failed to decode server certificate"), err);

  err = gnutls_x509_crt_print(crt, GNUTLS_CRT_PRINT_ONELINE,
                              &info_datum);
  gnutls_x509_crt_deinit(crt);
  if (err != GNUTLS_E_SUCCESS)
    throw rdr::tls_error(_("Failed to format server certificate for "
                           "display"), err);

  len = strlen((char*)info_datum.data);
  for (size_t i = 0; i < len - 1; i++) {
    if (info_datum.data[i] == ',' && info_datum.data[i + 1] == ' ')
      info_datum.data[i] = '\n';
  }

  info = (const char*)info_datum.data;

  gnutls_free(info_datum.data);

  /* 新主机 */
  if (known == GNUTLS_E_NO_CERTIFICATE_FOUND) {
    std::string text;

    vlog.info(_("Server host is not previously known"));
    vlog.info("%s", info.c_str());

    if (status & (GNUTLS_CERT_INVALID |
                  GNUTLS_CERT_SIGNER_NOT_FOUND |
                  GNUTLS_CERT_SIGNER_NOT_CA)) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This certificate has been signed by an unknown authority:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Unknown certificate issuer"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~(GNUTLS_CERT_INVALID |
                  GNUTLS_CERT_SIGNER_NOT_FOUND |
                  GNUTLS_CERT_SIGNER_NOT_CA);
    }

    if (status & GNUTLS_CERT_NOT_ACTIVATED) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This certificate is not yet valid:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Certificate is not yet valid"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_NOT_ACTIVATED;
    }

    if (status & GNUTLS_CERT_EXPIRED) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This certificate has expired:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Expired certificate"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_EXPIRED;
    }

    if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This certificate uses an insecure algorithm:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Insecure certificate algorithm"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_INSECURE_ALGORITHM;
    }

    if (status & GNUTLS_CERT_UNEXPECTED_OWNER) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        core::format(
          _("The specified hostname \"%s\" does not match the "
            "certificate provided by the server:"),
          getServerName()).c_str(),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Certificate hostname mismatch"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_UNEXPECTED_OWNER;
    }

    if (status != 0) {
      vlog.error("Unhandled server certificate problems: 0x%x",
                 status);
      throw std::logic_error("Unhandled server certificate problems");
    }
  } else if (known == GNUTLS_E_CERTIFICATE_KEY_MISMATCH) {
    std::string text;

    vlog.info(_("Server host certificate has changed"));
    vlog.info("%s", info.c_str());

    if (status & (GNUTLS_CERT_INVALID |
                  GNUTLS_CERT_SIGNER_NOT_FOUND |
                  GNUTLS_CERT_SIGNER_NOT_CA)) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This host is previously known with a different certificate, "
          "and the new certificate has been signed by an unknown "
          "authority:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Unexpected server certificate"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~(GNUTLS_CERT_INVALID |
                  GNUTLS_CERT_SIGNER_NOT_FOUND |
                  GNUTLS_CERT_SIGNER_NOT_CA);
    }

    if (status & GNUTLS_CERT_NOT_ACTIVATED) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This host is previously known with a different certificate, "
          "and the new certificate is not yet valid:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Unexpected server certificate"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_NOT_ACTIVATED;
    }

    if (status & GNUTLS_CERT_EXPIRED) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This host is previously known with a different certificate, "
          "and the new certificate has expired:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Unexpected server certificate"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_EXPIRED;
    }

    if (status & GNUTLS_CERT_INSECURE_ALGORITHM) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        _("This host is previously known with a different certificate, "
          "and the new certificate uses an insecure algorithm:"),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Unexpected server certificate"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_INSECURE_ALGORITHM;
    }

    if (status & GNUTLS_CERT_UNEXPECTED_OWNER) {
      text = core::format(
        "%s\n\n%s\n\n%s\n\n%s",
        core::format(
          _("This host is previously known with a different "
            "certificate, and the specified hostname \"%s\" does not "
            "match the new certificate provided by the server:"),
          getServerName()).c_str(),
        info.c_str(),
        _("Someone could be trying to impersonate the site and you "
          "should not continue."),
        _("Do you want to make an exception for this server?"));

      fl_message_title(_("Unexpected server certificate"));
      if (fl_choice("%s", nullptr, fl_cancel, _("Add exception"),
                    fltk_escape(text).c_str()) != 2)
        return false;

      status &= ~GNUTLS_CERT_UNEXPECTED_OWNER;
    }

    if (status != 0) {
      vlog.error("Unhandled server certificate problems: 0x%x",
                 status);
      throw std::logic_error("Unhandled server certificate problems");
    }
  }

  if (gnutls_store_pubkey(dbPath.c_str(), nullptr,
                          getServerName(), nullptr,
                          GNUTLS_CRT_X509, &cert_datum, 0, 0))
    vlog.error(_("Failed to store server certificate to list of "
                 "servers with a security exception"));

  vlog.info(_("Security exception added for server host"));

  return true;
#endif
}

bool CConn::verifyHostKey(const uint8_t* key, size_t length,
                          const char* fingerprint)
{
  std::string text = core::format(
    _("The server has provided the following identifying information:\n"
      "\n"
      "Fingerprint: %s\n"
      "\n"
      "Do you want to continue connecting to this server?"),
    fingerprint);
  fl_message_title(_("Verify server key"));
  if (fl_choice("%s", nullptr, fl_cancel, _("Continue"),
                fltk_escape(text).c_str()) != 2)
    return false;

  // FIXME: 应将其保存下来用于 TOFU
  (void)key;
  (void)length;

  return true;
}

void CConn::setExtendedDesktopSize(unsigned reason, unsigned result,
                                   int w, int h,
                                   const rfb::ScreenSet& layout)
{
  CConnection::setExtendedDesktopSize(reason, result, w, h, layout);

  if (reason == rfb::reasonClient)
    desktop->setDesktopSizeDone(result);
}

// 桌面名称发生变化时会调用 setName()
void CConn::setName(const char* name)
{
  CConnection::setName(name);
  desktop->updateCaption();
}

// 一次更新开始时调用 framebufferUpdateStart()。
// 这里我们尝试发出新的帧缓冲更新请求，以便下一个更新可以
// 在我们解码当前更新的同时并行发出。
void CConn::framebufferUpdateStart()
{
  CConnection::framebufferUpdateStart();

  // 用于估算带宽
  gettimeofday(&updateStartTime, nullptr);
  updateStartPos = sock->inStream().pos();

  // 对于非常慢的更新，提前刷新屏幕
  Fl::add_timeout(1.0, handleUpdateTimeout, this);
}

// 一次更新结束时调用 framebufferUpdateEnd()。
// 对每个矩形，FdInStream 会记录连接速度的计时，
// 使我们能够恰当地选择格式和编码，然后请求下一次增量更新。
void CConn::framebufferUpdateEnd()
{
  unsigned long long elapsed, bps, weight;
  struct timeval now;

  CConnection::framebufferUpdateEnd();

  updateCount++;

  // 计算本次更新期间维持的带宽
  gettimeofday(&now, nullptr);
  elapsed = (now.tv_sec - updateStartTime.tv_sec) * 1000000;
  elapsed += now.tv_usec - updateStartTime.tv_usec;
  if (elapsed == 0)
    elapsed = 1;
  bps = (unsigned long long)(sock->inStream().pos() -
                             updateStartPos) * 8 *
                            1000000 / elapsed;
  // 本次更新耗时越长，让它对结果的影响越大，但最多占新值的 20%。
  weight = elapsed * 1000 / bpsEstimateWindow;
  if (weight > 200000)
    weight = 200000;
  bpsEstimate = ((bpsEstimate * (1000000 - weight)) +
                 (bps * weight)) / 1000000;

  Fl::remove_timeout(handleUpdateTimeout, this);
  desktop->updateWindow();

  // 根据更新后的带宽值计算新设置
  if (autoSelect) {
    updateEncoding();
    updateQualityLevel();
    updatePixelFormat();
  }
}

// 其余回调基本都能望文知义……

void CConn::bell()
{
  fl_beep();
}

bool CConn::dataRect(const core::Rect& r, int encoding)
{
  bool ret;

  if (encoding != rfb::encodingCopyRect)
    lastServerEncoding = encoding;

  ret = CConnection::dataRect(r, encoding);

  if (ret)
    pixelCount += r.area();

  return ret;
}

void CConn::setCursor(int width, int height, const core::Point& hotspot,
                      const uint8_t* data)
{
  CConnection::setCursor(width, height, hotspot, data);

  desktop->setCursor();
}

void CConn::setCursorPos(const core::Point& pos)
{
  desktop->setCursorPos(pos);
}

void CConn::setLEDState(unsigned int state)
{
  CConnection::setLEDState(state);

  desktop->setLEDState(state);
}

void CConn::handleClipboardRequest()
{
  desktop->handleClipboardRequest();
}

void CConn::handleClipboardAnnounce(bool available)
{
  desktop->handleClipboardAnnounce(available);
}

void CConn::handleClipboardData(const char* data)
{
  desktop->handleClipboardData(data);
}

void CConn::handleAudioBegin()
{
  if (audioOutput == nullptr)
    return;

  audioOutput->start();
}

void CConn::handleAudioEnd()
{
  if (audioOutput == nullptr)
    return;

  audioOutput->stop();
}

void CConn::handleAudioData(const uint8_t* data, size_t length)
{
  if (audioOutput == nullptr)
    return;

  audioOutput->play(data, length);
}


////////////////////// 内部方法 //////////////////////

void CConn::resizeFramebuffer()
{
  desktop->resizeFramebuffer(server.width(), server.height());
}

void CConn::updateEncoding()
{
  int encNum;

  if (autoSelect)
    encNum = rfb::encodingTight;
  else
    encNum = rfb::encodingNum(::preferredEncoding.getValueStr().c_str());

  if (encNum != -1)
    setPreferredEncoding(encNum);
}

void CConn::updateCompressLevel()
{
  if (customCompressLevel)
    setCompressLevel(::compressLevel);
  else
    setCompressLevel(-1);
}

void CConn::updateQualityLevel()
{
  int newQualityLevel;

  if (!autoSelect)
    newQualityLevel = ::qualityLevel;
  else {
    // 高于 16Mbps（即局域网）时，选择次高的 JPEG 质量，这在
    // 感知上应是无损的。如果带宽低于该值，则选择有损程度更高的
    // JPEG 质量。
    // 12~16Mbps 是滞回带：带宽在阈值附近抖动时不会每帧反复切换。
    if (getQualityLevel() == 8)
      newQualityLevel = (bpsEstimate < 12000000) ? 6 : 8;
    else
      newQualityLevel = (bpsEstimate > 16000000) ? 8 : 6;

    if (newQualityLevel != getQualityLevel()) {
      vlog.info(_("Throughput %d kbit/s - changing to quality %d"),
                (int)(bpsEstimate/1000), newQualityLevel);
    }
  }

  setQualityLevel(newQualityLevel);
}

void CConn::updatePixelFormat()
{
  bool useFullColour;
  rfb::PixelFormat pf, fullColourPF;

  useFullColour = fullColour;

  fullColourPF = getFramebuffer()->getPF();

  // 如果带宽降到 256 Kbps 以下，则切换到调色板模式。
  // 200~320Kbps 是滞回带：带宽在阈值附近抖动时不会反复切换。
  if (autoSelect) {
    const bool currentlyFull = (server.pf() == fullColourPF);
    useFullColour = currentlyFull ? (bpsEstimate > 200000)
                                  : (bpsEstimate > 320000);
    if (useFullColour != currentlyFull) {
      if (useFullColour)
        vlog.info(_("Throughput %d kbit/s - full color is now enabled"),
                  (int)(bpsEstimate/1000));
      else
        vlog.info(_("Throughput %d kbit/s - full color is now disabled"),
                  (int)(bpsEstimate/1000));
    }
  }

  if (useFullColour) {
    pf = fullColourPF;
  } else {
    if (lowColourLevel == 0)
      pf = verylowColourPF;
    else if (lowColourLevel == 1)
      pf = lowColourPF;
    else
      pf = mediumColourPF;
  }

  if (pf != server.pf()) {
    char str[256];
    pf.print(str, 256);
    vlog.info(_("Using pixel format %s"),str);
    setPF(pf);
  }
}

void CConn::handleOptions(void *data)
{
  CConn *self = (CConn*)data;

  self->updateEncoding();
  self->updateCompressLevel();
  self->updateQualityLevel();
  self->updatePixelFormat();
}

void CConn::handleUpdateTimeout(void *data)
{
  CConn *self = (CConn *)data;

  assert(self);

  self->desktop->updateWindow();

  Fl::repeat_timeout(1.0, handleUpdateTimeout, data);
}
