/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011-2024 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright (C) 2011 D. R. Commander.  All Rights Reserved.
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
#include <string>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifdef WIN32
#include <core/winerrno.h>
#include <direct.h>
#endif

#include <core/Exception.h>
#include <core/Logger_file.h>
#include <core/Logger_stdio.h>
#include <core/LogWriter.h>
#include <core/Timer.h>
#include <core/time.h>
#include <core/string.h>

#ifdef HAVE_GNUTLS
#include <rfb/CSecurityTLS.h>
#endif

#include <core/i18n.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_PNG_Image.H>
#include <FL/Fl_Sys_Menu_Bar.H>
#include <FL/fl_ask.H>

#include "fltk/Fl_Password_Input.h"
#include "fltk/theme.h"
#include "fltk/util.h"
#include "parameters.h"
#include "CConn.h"
#include "ServerDialog.h"
#include "touch.h"
#include "vncviewer.h"

#ifdef WIN32
#include "resource.h"
#include "win32.h"
#endif

static core::LogWriter vlog("main");

static std::string vncServerName;

static const char *argv0 = nullptr;

static bool inMainloop = false;
static bool exitMainloop = false;
static std::string exitError;
static bool fatalError = false;

static const char *about_text()
{
  static char buffer[1024];

  // 这段文本会在多处使用，且编码可能各不相同，
  // 因此需要确保每次都拿到一个新字符串。
  snprintf(buffer, sizeof(buffer),
           _("TuPigVNC v%s\n"
             "Built on: %s\n"
             "Copyright (C) 1999-%d TuPigVNC team and many others (see README.rst)\n"
             "See https://github.com/TuPig/TuPigVNC for information on TuPigVNC."),
           PACKAGE_VERSION, BUILD_TIMESTAMP, 2026);

  return buffer;
}


void abort_vncviewer(const char *error, ...)
{
  fatalError = true;

  // 优先采用我们遇到的第一个错误，因为它可能是最相关的。
  if (exitError.empty()) {
    va_list ap;

    va_start(ap, error);
    exitError = core::vformat(error, ap);
    va_end(ap);
  }

  if (inMainloop)
    exitMainloop = true;
  else {
    // 此时仍处于启动早期，假定直接 exit() 即可。
    if (alertOnFatalError && !exitError.empty())
      fl_alert("%s", exitError.c_str());
    exit(EXIT_FAILURE);
  }
}

void abort_connection(const char *error, ...)
{
  assert(inMainloop);

  // 优先采用我们遇到的第一个错误，因为它可能是最相关的。
  if (exitError.empty()) {
    va_list ap;

    va_start(ap, error);
    exitError = core::vformat(error, ap);
    va_end(ap);
  }

  exitMainloop = true;
}

void abort_connection_unexpected(const std::exception &e)
{
  abort_connection(_("An unexpected error occurred when communicating "
                     "with the server:\n\n%s"), e.what());
}

void disconnect()
{
  exitMainloop = true;
}

void about_vncviewer()
{
  fl_message_title(_("About TuPigVNC"));
  fl_message("%s", about_text());
}

static void mainloop(const char* vncserver, network::Socket* sock)
{
  /* 自动重连：断线后最多尝试 3 次，间隔 2/4/8 秒（指数退避 + 抖动），
   * 封顶 30 秒。连接稳定后计数归零，长时间会话里多次断线仍能自动重连。 */
  const int maxReconnectAttempts = 3;
  const int baseDelaySeconds = 2;
  const int maxDelaySeconds = 30;
  int reconnectAttempts = 0;

  while (true) {
    CConn *cc;

    exitMainloop = false;

    cc = new CConn();
    cc->connect(vncserver, sock);

    /* 连接成功（connect 返回时没有错误）则重置重连计数 */
    if (exitError.empty())
      reconnectAttempts = 0;

    while (!exitMainloop) {
      int next_timer;

      next_timer = core::Timer::checkTimeouts();
      if (next_timer < 0)
        next_timer = INT_MAX;

      if (Fl::wait((double)next_timer / 1000.0) < 0.0) {
        vlog.error(_("Internal FLTK error. Exiting."));
        exit(-1);
      }
    }

    delete cc;

    if (fatalError) {
      assert(!exitError.empty());
      if (alertOnFatalError)
        fl_alert("%s", exitError.c_str());
      break;
    }

    if (exitError.empty())
      break;

    if (reconnectOnError && (sock == nullptr)) {
      /* 先自动重连（指数退避 + ±20% 抖动，避免服务器重启时同步冲击） */
      if (reconnectAttempts < maxReconnectAttempts) {
        reconnectAttempts++;
        int delay = baseDelaySeconds << (reconnectAttempts - 1);
        if (delay > maxDelaySeconds)
          delay = maxDelaySeconds;
        const double jitter = 0.8 + (rand() % 401) / 1000.0;  // 0.8~1.2
        const double waitSeconds = delay * jitter;

        vlog.info(_("连接中断，%.1f 秒后自动重连（第 %d/%d 次）"),
                  waitSeconds, reconnectAttempts, maxReconnectAttempts);

        /* 等待期间保持界面响应（FLTK 1.3 没有 Fl::now，用 msSince 计时） */
        struct timeval start;
        gettimeofday(&start, nullptr);
        while (true) {
          const double elapsed = core::msSince(&start) / 1000.0;
          if (elapsed >= waitSeconds)
            break;
          if (Fl::wait(waitSeconds - elapsed) < 0.0)
            break;
        }
        continue;
      }

      /* 自动重连失败，询问用户 */
      int ret;
      ret = fl_choice(_("%s\n\n"
                        "自动重连失败，是否重试？"),
                      nullptr, _("Reconnect"), fl_close,
                      exitError.c_str());
      exitError.clear();
      if (ret == 1) {
        reconnectAttempts = 0;
        continue;
      }
      break;
    }

    if (alertOnFatalError)
      fl_alert("%s", exitError.c_str());

    break;
  }
}

#ifdef __APPLE__
static void about_callback(Fl_Widget* /*widget*/, void* /*data*/)
{
  about_vncviewer();
}

static void new_connection_cb(Fl_Widget* /*widget*/, void* /*data*/)
{
  const char *argv[2];
  pid_t pid;

  pid = fork();
  if (pid == -1) {
    vlog.error(_("Error starting new connection: %s"), strerror(errno));
    return;
  }

  if (pid != 0)
    return;

  argv[0] = argv0;
  argv[1] = nullptr;

  execvp(argv[0], (char * const *)argv);

  vlog.error(_("Error starting new connection: %s"), strerror(errno));
  _exit(1);
}
#endif

static void CleanupSignalHandler(int sig)
{
  // CleanupSignalHandler 允许执行 C++ 对象清理，因为它调用 exit()，
  // 而不是默认的 abort。
  vlog.info(_("Termination signal %d has been received. TuPigVNC will now exit."), sig);
  exit(1);
}

static void init_fltk()
{
  // 调整 FLTK 的外观
  init_theme();

  // 要正确集成到 Gnome Shell，需要为窗口设置一个合理的 WM_CLASS。
  Fl_Window::default_xclass("vncviewer");

  // 为所有窗口设置默认图标。
#ifdef WIN32
  HICON lg, sm;

  lg = (HICON)LoadImage(GetModuleHandle(nullptr),
                        MAKEINTRESOURCE(IDI_ICON),
                        IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                        GetSystemMetrics(SM_CYICON),
                        LR_DEFAULTCOLOR | LR_SHARED);
  sm = (HICON)LoadImage(GetModuleHandle(nullptr),
                        MAKEINTRESOURCE(IDI_ICON),
                        IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                        GetSystemMetrics(SM_CYSMICON),
                        LR_DEFAULTCOLOR | LR_SHARED);

  Fl_Window::default_icons(lg, sm);
#elif ! defined(__APPLE__)
  const int icon_sizes[] = {128, 64, 48, 32, 24, 22, 16};

  Fl_PNG_Image *icons[sizeof(icon_sizes)/sizeof(icon_sizes[0])];
  int count;

  count = 0;

  // FIXME: 遵循图标主题规范
  for (int icon_size : icon_sizes) {
      char icon_path[PATH_MAX];
      bool exists;

      sprintf(icon_path, "%s/icons/hicolor/%dx%d/apps/tigervnc.png",
              CMAKE_INSTALL_FULL_DATADIR, icon_size, icon_size);

      struct stat st;
      if (stat(icon_path, &st) != 0)
        exists = false;
      else
        exists = true;

      if (exists) {
          icons[count] = new Fl_PNG_Image(icon_path);
          if (icons[count]->w() == 0 ||
              icons[count]->h() == 0 ||
              icons[count]->d() != 4) {
              delete icons[count];
              continue;
          }

          count++;
      }
  }

  Fl_Window::default_icons((const Fl_RGB_Image**)icons, count);

  for (int i = 0;i < count;i++)
      delete icons[i];
#endif

  // 关闭弹窗跟随鼠标这一烦人行为。
  fl_message_hotspot(false);

  // 避免弹窗标题为空
  fl_message_title_default("TuPigVNC");

  // FLTK 暴露这些字符串以便我们翻译它们。
  fl_no     = _("No");
  fl_yes    = _("Yes");
  fl_ok     = _("OK");
  fl_cancel = _("Cancel");
  fl_close  = _("Close");

  Fl_Password_Input::caps_warning_tooltip = _("Caps lock is active");

  Fl_File_Chooser::add_favorites_label = _("Add to Favorites");
  Fl_File_Chooser::all_files_label = _("All Files (*)");
  Fl_File_Chooser::custom_filter_label = _("Custom Filter");
  Fl_File_Chooser::existing_file_label = _("Please choose an existing file!");
  Fl_File_Chooser::favorites_label = _("Favorites");
  Fl_File_Chooser::filename_label = _("Filename:");
#ifdef WIN32
  Fl_File_Chooser::filesystems_label = _("My Computer");
#else
  Fl_File_Chooser::filesystems_label = _("File Systems");
#endif // WIN32
  Fl_File_Chooser::manage_favorites_label = _("Manage Favorites");
  Fl_File_Chooser::new_directory_label = _("New Directory?");
  Fl_File_Chooser::new_directory_tooltip = _("Create a new directory.");
  Fl_File_Chooser::preview_label = _("Preview");
  Fl_File_Chooser::save_label = _("Save");
  Fl_File_Chooser::show_label = _("Show:");
  Fl_File_Chooser::hidden_label = _("Show hidden files");

#ifdef __APPLE__
  Fl_Mac_App_Menu::about = _("About TuPigVNC");
  Fl_Mac_App_Menu::print = ""; // 不需要打印项
  Fl_Mac_App_Menu::services = _("Services");
  Fl_Mac_App_Menu::hide = _("Hide TuPigVNC");
  Fl_Mac_App_Menu::hide_others = _("Hide others");
  Fl_Mac_App_Menu::show = _("Show all");
  Fl_Mac_App_Menu::quit = _("Quit TuPigVNC");

  fl_mac_set_about(about_callback, nullptr);

  Fl_Sys_Menu_Bar *menubar;
  menubar = new Fl_Sys_Menu_Bar(0, 0, 500, 25);
  // Fl_Sys_Menu_Bar 覆盖方法时并未将它们声明为虚函数，
  // 这意味着我们无法使用通用的 Fl_Menu_ 辅助函数。
  menubar->add(
    fltk_menu_escape(C_("SysMenu|", "&File")).c_str(),
    0, nullptr, nullptr, FL_SUBMENU);
  menubar->insert(
    1, fltk_menu_escape(C_("SysMenu|File|", "&New connection")).c_str(),
    FL_COMMAND | 'n', new_connection_cb);
#endif
}

static void usage(const char *programName)
{
#ifdef WIN32
  // 如果没有控制台，则需要创建一个用于输出
  if (GetConsoleWindow() == nullptr) {
    HANDLE handle;
    int fd;

    AllocConsole();

    handle = GetStdHandle(STD_ERROR_HANDLE);
    fd = _open_osfhandle((intptr_t)handle, O_TEXT);
    *stderr = *fdopen(fd, "w");
  }
#endif

#ifdef WIN32
  fprintf(stderr, _(
          "\n"
          "Usage: %s [parameters] [host][:displayNum]\n"
          "       %s [parameters] [host][::port]\n"
          "       %s [parameters] -listen [port]\n"
          "       %s [parameters] [.tigervnc file]\n"),
          programName, programName, programName, programName);
#else
  fprintf(stderr, _(
          "\n"
          "Usage: %s [parameters] [host][:displayNum]\n"
          "       %s [parameters] [host][::port]\n"
          "       %s [parameters] [unix socket]\n"
          "       %s [parameters] -listen [port]\n"
          "       %s [parameters] [.tigervnc file]\n"),
          programName, programName, programName, programName, programName);
#endif

#if !defined(WIN32) && !defined(__APPLE__)
  fprintf(stderr, _("\n"
          "Options:\n\n"
          "  -display Xdisplay  - Specifies the X display for the viewer window\n"
          "  -geometry geometry - Initial position of the main TuPigVNC window. See the\n"
          "                       man page for details.\n"));
#endif

  fprintf(stderr, _("\n"
          "Parameters can be turned on with -<param> or off with -<param>=0\n"
          "Parameters which take a value can be specified as "
          "-<param> <value>\n"
          "Other valid forms are <param>=<value> -<param>=<value> "
          "--<param>=<value>\n"
          "Parameter names are case-insensitive.  The parameters are:\n\n"));
  core::Configuration::listParams(79, 14);

#ifdef WIN32
  // 等待用户关闭控制台窗口即可
  Sleep(INFINITE);
#endif

  exit(1);
}

static void
potentiallyLoadConfigurationFile(const char *filename)
{
  const bool hasPathSeparator = (strchr(filename, '/') != nullptr ||
                                 (strchr(filename, '\\')) != nullptr);

  if (hasPathSeparator) {
#ifndef WIN32
    struct stat sb;

    // 这可能是一个 UNIX 套接字，需要检查一下
    if (stat(filename, &sb) == -1) {
      // 存在某种访问问题；交给 loadViewerParameters() 处理……
    } else {
      if ((sb.st_mode & S_IFMT) == S_IFSOCK)
        return;
    }
#endif

    try {
      // 服务器名可能为空，但仍需要清空它，
      // 以免尝试连接到该文件名
      vncServerName = loadViewerParameters(filename);
    } catch (std::exception& e) {
      vlog.error("%s", e.what());
      abort_vncviewer(_("Unable to load the specified configuration "
                        "file:\n\n%s"), e.what());
    }
  }
}

static void
create_base_dirs()
{
  const char *dir;

  dir = core::getvncconfigdir();
  if (dir == nullptr) {
    vlog.error(_("Could not determine VNC config directory path"));
    return;
  }

#ifndef WIN32
  const char *dotdir = strrchr(dir, '.');
  if (dotdir != nullptr && strcmp(dotdir, ".vnc") == 0)
    vlog.info(_("~/.vnc is deprecated, please consult 'man vncviewer' for paths to migrate to."));
#else
  const char *vncdir = strrchr(dir, '\\');
  if (vncdir != nullptr && strcmp(vncdir, "vnc") == 0)
    vlog.info(_("%%APPDATA%%\\vnc is deprecated, please switch to the %%APPDATA%%\\TuPigVNC location."));
#endif

  if (core::mkdir_p(dir, 0755) == -1) {
    if (errno != EEXIST)
      vlog.error(_("Could not create VNC config directory \"%s\": %s"),
                 dir, strerror(errno));
  }

  dir = core::getvncdatadir();
  if (dir == nullptr) {
    vlog.error(_("Could not determine VNC data directory path"));
    return;
  }

  if (core::mkdir_p(dir, 0755) == -1) {
    if (errno != EEXIST)
      vlog.error(_("Could not create VNC data directory \"%s\": %s"),
                 dir, strerror(errno));
  }

  dir = core::getvncstatedir();
  if (dir == nullptr) {
    vlog.error(_("Could not determine VNC state directory path"));
    return;
  }

  if (core::mkdir_p(dir, 0755) == -1) {
    if (errno != EEXIST)
      vlog.error(_("Could not create VNC state directory \"%s\": %s"),
                 dir, strerror(errno));
  }
}

#ifndef WIN32
static void
createTunnel(const char *gatewayHost, const char *remoteHost,
             int remotePort, int localPort)
{
  const char *cmd = getenv("VNC_VIA_CMD");
  char *cmd2, *percent;
  char lport[10], rport[10];
  sprintf(lport, "%d", localPort);
  sprintf(rport, "%d", remotePort);
  setenv("G", gatewayHost, 1);
  setenv("H", remoteHost, 1);
  setenv("R", rport, 1);
  setenv("L", lport, 1);
  if (!cmd)
    cmd = "/usr/bin/ssh -f -L \"$L\":\"$H\":\"$R\" -- \"$G\" sleep 20";
  /* 兼容 TuPigVNC 的做法。 */
  cmd2 = strdup(cmd);
  while ((percent = strchr(cmd2, '%')) != nullptr)
    *percent = '$';
  int res = system(cmd2);
  if (res != 0)
    vlog.error(_("Failed to create tunnel: '%s' returned %d"),
               cmd2, res);
  free(cmd2);
}

static void mktunnel()
{
  const char *gatewayHost;
  std::string remoteHost;
  int localPort = network::findFreeTcpPort();
  int remotePort;

  network::getHostAndPort(vncServerName.c_str(), &remoteHost, &remotePort);
  vncServerName = core::format("localhost::%d", localPort);
  gatewayHost = (const char*)via;
  createTunnel(gatewayHost, remoteHost.c_str(), remotePort, localPort);
}
#endif /* !WIN32 */

int main(int argc, char** argv)
{
  argv0 = argv[0];

  setlocale(LC_ALL, "");

#ifdef WIN32
  /*
   * 避免 FLTK 在磁盘根目录（如 C:\FLTK\fltk.org\fltk.prefs）创建配置文件。
   * 将 FLTK 的偏好设置根目录重定向到用户的漫游配置目录。
   * 参考上游 issue #822。
   *
   * 目录解析复用 core::getvncconfigdir()，不再自行读取 APPDATA，
   * 保证与其余配置路径（%APPDATA%\TuPigVNC）始终一致。
   */
  if (getenv("FLTK_PREFS_ROOT") == nullptr) {
    const char* configDir = core::getvncconfigdir();
    if (configDir != nullptr && configDir[0] != '\0')
      _putenv_s("FLTK_PREFS_ROOT", configDir);
  }
#endif

  fprintf(stderr,"\n%s\n", about_text());

  core::initStdIOLoggers();

  /* 日志统一写入「安装目录/logs」，文件名带时间戳，
   * 跨日或超过 20MB 自动切新文件，避免单个日志无限膨胀。 */
  {
    std::string logDir;

#ifdef WIN32
    const char* root = core::getvncinstallroot();
    if (root != nullptr)
      logDir = std::string(root) + "/logs";
#else
    const char* statedir = core::getvncstatedir();
    if (statedir != nullptr)
      logDir = std::string(statedir) + "/logs";
#endif

    if (!logDir.empty()) {
      core::mkdir_p(logDir.c_str(), 0755);
      if (core::initRollingFileLogger(logDir.c_str(), 20L * 1024 * 1024) != nullptr)
        core::LogWriter::setLogParams("*:file:30");
      else
        core::LogWriter::setLogParams("*:stderr:30");
    } else {
      core::LogWriter::setLogParams("*:stderr:30");
    }
  }

  vlog.info(_("TuPigVNC 客户端启动"));

#ifdef SIGHUP
  signal(SIGHUP, CleanupSignalHandler);
#endif
  signal(SIGINT, CleanupSignalHandler);
  signal(SIGTERM, CleanupSignalHandler);

  /* 加载默认参数设置 */
  std::string defaultServerName;
  try {
    defaultServerName = loadViewerParameters(nullptr);
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
  }

  for (int i = 1; i < argc;) {
    int ret;

    ret = core::Configuration::handleParamArg(argc, argv, i);
    if (ret > 0) {
      i += ret;
      continue;
    }

    if (strcmp(argv[i], "-h") == 0 ||
        strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
    }

    if (strcmp(argv[i], "-v") == 0 ||
        strcmp(argv[i], "--version") == 0) {
      /* 每次启动时已经打印过版本号 */
      return 0;
    }

    if (argv[i][0] == '-') {
      fprintf(stderr, "\n");
      fprintf(stderr, _("%s: Unrecognized option '%s'\n"),
              argv[0], argv[i]);
      fprintf(stderr, _("See '%s --help' for more information.\n"),
              argv[0]);
      exit(1);
    }

    if (vncServerName[0] != '\0') {
      fprintf(stderr, "\n");
      fprintf(stderr, _("%s: Extra argument '%s'\n"), argv[0], argv[i]);
      fprintf(stderr, _("See '%s --help' for more information.\n"),
              argv[0]);
      exit(0);
    }

    vncServerName = argv[i];
    i++;
  }

  // 处理命令行中指定的任何旧式设置
  migrateDeprecatedOptions();

#if !defined(WIN32) && !defined(__APPLE__)
  if (strcmp(display, "") != 0) {
    Fl::display(display);
  }
  fl_open_display();
#endif

  init_fltk();
  enable_touch();

  // 检查服务器名实际上是否是一个配置文件
  potentiallyLoadConfigurationFile(vncServerName.c_str());

  create_base_dirs();

  network::Socket* sock = nullptr;

#ifndef WIN32
  /* 同时指定 -via 和 -listen 毫无意义 */
  if (listenMode && strlen(via) > 0) {
    // 翻译者注意：这里的“Parameters”（参数）指命令行参数，或来自
    // 文件或 Windows 注册表的设置。
    vlog.error(_("Parameters -listen and -via are incompatible"));
    abort_vncviewer(_("Parameters -listen and -via are incompatible"));
    return 1; /* 不会执行到这里 */
  }
#endif

  if (listenMode) {
    std::list<network::SocketListener*> listeners;
    try {
      int port = 5500;
      if (!vncServerName.empty() && isdigit(vncServerName[0]))
        port = atoi(vncServerName.c_str());

      createTcpListeners(&listeners, nullptr, port);
      if (listeners.empty())
        throw std::runtime_error(_("Unable to listen for incoming connections"));

      vlog.info(_("Listening on port %d"), port);

      /* 等待连接 */
      while (sock == nullptr) {
        fd_set rfds;
        FD_ZERO(&rfds);
        for (network::SocketListener* listener : listeners)
          FD_SET(listener->getFd(), &rfds);

        int n = select(FD_SETSIZE, &rfds, nullptr, nullptr, nullptr);
        if (n < 0) {
          if (errno == EINTR) {
            vlog.debug("Interrupted select() system call");
            continue;
          } else {
            throw core::socket_error("select", errno);
          }
        }

        for (network::SocketListener* listener : listeners)
          if (FD_ISSET(listener->getFd(), &rfds)) {
            sock = listener->accept();
            if (sock)
              /* 已获得连接 */
              break;
          }
      }
    } catch (std::exception& e) {
      vlog.error("%s", e.what());
      abort_vncviewer(_("Failure waiting for incoming VNC connection:\n\n%s"), e.what());
      return 1; /* 不会执行到这里 */
    }

    while (!listeners.empty()) {
      delete listeners.back();
      listeners.pop_back();
    }
  } else {
    if (vncServerName.empty()) {
      vncServerName = ServerDialog::run(defaultServerName.c_str());
      if (vncServerName.empty())
        return 1;
    }

#ifndef WIN32
    if (strlen(via) > 0) {
      try {
        mktunnel();
      } catch (std::exception& e) {
        vlog.error("%s", e.what());
        abort_vncviewer(_("Failure setting up encrypted tunnel:\n\n%s"), e.what());
      }
    }
#endif
  }

  inMainloop = true;
  mainloop(vncServerName.c_str(), sock);
  inMainloop = false;

  return 0;
}
