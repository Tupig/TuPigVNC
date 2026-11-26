/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
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

#include <errno.h>
#include <time.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <libgen.h>

// FIXME: 规避 FLTK 引入 windows.h 的问题
#ifdef WIN32
#include <winsock2.h>
#endif

#include <FL/Fl.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Input_Choice.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Return_Button.H>
#include <FL/fl_draw.H>
#include <FL/fl_ask.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_File_Chooser.H>
#include <FL/Fl_Scroll.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Choice.H>
#include <FL/Fl_Int_Input.H>
#include <FL/Fl_Toggle_Button.H>

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/xdgdirs.h>
#include <core/UpdateChecker.h>

#include <network/TcpSocket.h>

#include <rfb/encodings.h>
#include <rfb/CConnection.h>
#if defined(HAVE_GNUTLS) || defined(HAVE_NETTLE)
#include <rfb/Security.h>
#include <rfb/SecurityClient.h>
#endif

#include "fltk/layout.h"
#include "fltk/util.h"
#include "fltk/theme.h"
#include "fltk/Fl_Suggestion_Input.h"
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

/* 当前存活的 ServerDialog 实例。
 * 后台更新线程经 Fl::awake 回到主线程时，若对话框已销毁则丢弃结果，
 * 避免访问悬空指针。 */
static std::atomic<ServerDialog*> g_updOwner{nullptr};

const char* SERVER_HISTORY="tigervnc.history";        // 旧格式（仅用于迁移）
const char* SERVER_HISTORY_JSON="history.json";       // 新格式：User/history.json
static const size_t SERVER_HISTORY_MAX = 250;         // 历史上限

// 前向声明（定义在文件下方，handleConnect 等处会用到）
static bool same_server(const std::string& a, const std::string& b);

ServerDialog::ServerDialog()
  : Fl_Window(540, 760, "TuPigVNC"),
    scroll(nullptr), historyBrowser(nullptr), activePanel(-1), panelTopY(0),
    activeOpt(0),
    optAutoSelect(nullptr), optJpeg(nullptr), optEncoding(nullptr),
    optColorLevel(nullptr), optCustomCompress(nullptr),
    optCompressLevel(nullptr), optQualityLevel(nullptr),
    optEncryption(nullptr), optAuth(nullptr),
    optViewOnly(nullptr), optMouseMode(nullptr), optCursorType(nullptr),
    optSystemKeys(nullptr), optAcceptClip(nullptr), optSendClip(nullptr),
    optDisplayMode(nullptr),
    optShared(nullptr), optReconnect(nullptr), optAudio(nullptr)
{
  for (int i = 0; i < 4; i++) {
    panels[i] = nullptr;
    entries_[i] = nullptr;
  }
  for (int i = 0; i < 6; i++) {
    optPanes[i] = nullptr;
    optTabs[i] = nullptr;
  }
  for (int i = 0; i < 4; i++)
    optMod[i] = nullptr;

  buildLayout();
  loadOptions();

  g_updOwner = this;
}


// ============================================================
// 极简单页 · Material 3
// 所有入口平铺在主界面 · 无侧栏无标签无菜单 · 点击原地展开
// ============================================================

// Material 3 配色（与 theme.cxx 保持一致）
static Fl_Color M_CARD      = fl_rgb_color(255, 255, 255);
static Fl_Color M_BG        = fl_rgb_color(251, 250, 255);
static Fl_Color M_TEXT      = fl_rgb_color(26, 21, 38);
static Fl_Color M_TEXT2     = fl_rgb_color(107, 101, 128);
static Fl_Color M_TEXT3     = fl_rgb_color(162, 155, 184);
static Fl_Color M_ACCENT    = fl_rgb_color(108, 92, 231);
static Fl_Color M_ACC_SOFT  = fl_rgb_color(239, 235, 255);
static Fl_Color M_STROKE2   = fl_rgb_color(224, 217, 242);

void ServerDialog::buildLayout()
{
  const int PAD = 20;           // 左右内边距
  const int GAP = 8;            // 元素间距
  const int TOPBAR_H = 48;      // 顶栏高度
  const int cw = w() - PAD * 2; // 内容宽度

  color(M_BG);

  // ===== 顶栏 =====
  Fl_Box *topbar = new Fl_Box(0, 0, w(), TOPBAR_H);
  topbar->box(FL_FLAT_BOX);
  topbar->color(M_CARD);

  Fl_Box *title = new Fl_Box(PAD, 0, 160, TOPBAR_H, "TuPigVNC");
  title->labelfont(FL_HELVETICA_BOLD);
  title->labelsize(14);
  title->labelcolor(M_TEXT);
  title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  // 主题切换（Material 选中态；切换逻辑后续接入）
  {
    const char *themeNames[3] = {"Material", "Linear", "Arc"};
    int tw = 64;
    int tx = w() - PAD - tw * 3 - 4 * 2;
    for (int i = 0; i < 3; i++) {
      Fl_Button *b = new Fl_Button(tx + i * (tw + 4), (TOPBAR_H - 26) / 2,
                                   tw, 26, themeNames[i]);
      b->box(THEME_UP_BOX);
      b->color(i == 0 ? M_ACC_SOFT : M_CARD);
      b->labelcolor(i == 0 ? M_ACCENT : M_TEXT3);
      b->labelsize(11);
      b->clear_visible_focus();
    }
  }

  // ===== 主滚动区 =====
  scroll = new Fl_Scroll(0, TOPBAR_H, w(), h() - TOPBAR_H);
  scroll->box(FL_FLAT_BOX);
  scroll->color(M_BG);
  scroll->begin();

  int y = 18;

  // ---- 连接区标题 ----
  Fl_Box *h1 = new Fl_Box(PAD, y, cw, 24, "连接 VNC 服务器");
  h1->labelfont(FL_HELVETICA_BOLD);
  h1->labelsize(16);
  h1->labelcolor(M_TEXT);
  h1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 26;

  Fl_Box *hint = new Fl_Box(PAD, y, cw, 18, "输入地址，回车即可连接");
  hint->labelsize(12);
  hint->labelcolor(M_TEXT2);
  hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 28;

  // ---- 地址输入 + 连接 ----
  int btnW = 80;
  serverName = new Fl_Suggestion_Input(PAD, y, cw - btnW - GAP, 34, "");
  serverName->call_on_remove(onServerHistoryRemove, this);
  serverName->call_to_normalize(serverHistoryNormalize);

  Fl_Button *connectBtn = new Fl_Button(PAD + cw - btnW, y, btnW, 34, "连接");
  connectBtn->box(THEME_UP_BOX);
  connectBtn->color(M_ACCENT);
  connectBtn->labelcolor(FL_WHITE);
  connectBtn->labelsize(13);
  connectBtn->callback(handleConnect, this);
  y += 34 + 14;

  // ---- 最近连接（chip 占位，加载历史后填充）----
  Fl_Box *recentLbl = new Fl_Box(PAD, y, 40, 24, "最近");
  recentLbl->labelsize(11);
  recentLbl->labelcolor(M_TEXT3);
  recentLbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 24 + 12;

  // ---- 次要操作 ----
  Fl_Button *loadBtn = new Fl_Button(PAD, y, 90, 28, "载入配置");
  loadBtn->box(THEME_UP_BOX);
  loadBtn->color(M_CARD);
  loadBtn->labelcolor(M_TEXT2);
  loadBtn->labelsize(11);
  loadBtn->callback(handleLoad, this);

  Fl_Button *saveBtn = new Fl_Button(PAD + 90 + GAP, y, 80, 28, "另存为");
  saveBtn->box(THEME_UP_BOX);
  saveBtn->color(M_CARD);
  saveBtn->labelcolor(M_TEXT2);
  saveBtn->labelsize(11);
  saveBtn->callback(handleSaveAs, this);
  y += 28 + 22;

  // ---- 2×2 功能入口 ----
  {
    const char *entryNames[4] = {"连接历史", "选项设置", "自动更新", "关于"};
    int colW = (cw - GAP) / 2;
    int entryStartY = y;
    for (int i = 0; i < 4; i++) {
      int col = i % 2;
      int row = i / 2;
      int ex = PAD + col * (colW + GAP);
      int ey = entryStartY + row * (52 + GAP);
      Fl_Button *e = new Fl_Button(ex, ey, colW, 52, entryNames[i]);
      e->box(THEME_UP_BOX);
      e->color(M_CARD);
      e->labelcolor(M_TEXT);
      e->labelsize(13);
      e->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
      e->user_data((void*)(intptr_t)i);
      e->callback(onEntryClicked, this);
      entries_[i] = e;
    }
    panelTopY = entryStartY + 2 * (52 + GAP) + 8;
  }

  // ---- 展开面板（初始全部隐藏）----
  for (int i = 0; i < 4; i++) {
    panels[i] = new Fl_Group(PAD, panelTopY, cw, 450);
    panels[i]->box(FL_FLAT_BOX);
    panels[i]->color(M_BG);
    switch (i) {
    case 0: buildHistoryPanel(panels[i]); break;
    case 1: buildOptionsPanel(panels[i]); break;
    case 2: buildUpdatePanel(panels[i]);  break;
    case 3: buildAboutPanel(panels[i]);    break;
    }
    panels[i]->end();
    panels[i]->hide();
  }

  scroll->end();

  /* 为保证缩放行为正常所必需 */
  resizable(nullptr);

  callback(this->handleCancel, this);
}


/* 面板通用标题栏：标题 + 右侧「收起」 */
static void buildPanelHeader(Fl_Group *g, const char *text,
                             Fl_Callback *closeCb, void *closeData)
{
  int cw = g->w();
  Fl_Box *t = new Fl_Box(0, 0, cw - 64, 30, text);
  t->labelfont(FL_HELVETICA_BOLD);
  t->labelsize(13);
  t->labelcolor(M_TEXT);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  Fl_Button *close = new Fl_Button(cw - 60, 3, 56, 24, "收起");
  close->box(FL_FLAT_BOX);
  close->color(M_BG);
  close->labelcolor(M_TEXT3);
  close->labelsize(11);
  close->clear_visible_focus();
  close->callback(closeCb, closeData);
}

void ServerDialog::buildHistoryPanel(Fl_Group *g)
{
  buildPanelHeader(g, "连接历史", onClosePanel, this);

  historyBrowser = new Fl_Browser(0, 38, g->w(), 4 * 22);
  historyBrowser->box(FL_FLAT_BOX);
  historyBrowser->color(M_CARD);
  historyBrowser->textsize(12);
  historyBrowser->textcolor(M_TEXT);
}

// ============================================================
// 选项面板 · 6 个分类页
// ============================================================

/* 分类页内的说明小标题 */
static Fl_Box *optCaption(int x, int y, int w, const char *text)
{
  Fl_Box *b = new Fl_Box(x, y, w, 18, text);
  b->labelsize(11);
  b->labelcolor(M_TEXT3);
  b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  return b;
}

/* 分类页内的复选按钮 */
static Fl_Check_Button *optCheck(int x, int y, int w, const char *text)
{
  Fl_Check_Button *b = new Fl_Check_Button(x, y, w, 24, text);
  b->box(FL_NO_BOX);
  b->down_box(FL_DOWN_BOX);
  b->labelsize(12);
  b->labelcolor(M_TEXT);
  b->clear_visible_focus();
  return b;
}

/* 分类页内的下拉框 */
static Fl_Choice *optChoice(int x, int y, int w, int h)
{
  Fl_Choice *c = new Fl_Choice(x, y, w, h);
  c->box(THEME_UP_BOX);
  c->color(M_CARD);
  c->textsize(12);
  c->textcolor(M_TEXT);
  c->clear_visible_focus();
  return c;
}

/* 分类页内的数字输入框 */
static Fl_Int_Input *optSpin(int x, int y, int w, int h)
{
  Fl_Int_Input *i = new Fl_Int_Input(x, y, w, h);
  i->box(THEME_UP_BOX);
  i->color(M_CARD);
  i->textsize(12);
  i->textcolor(M_TEXT);
  return i;
}

void ServerDialog::buildOptionsPanel(Fl_Group *g)
{
  buildPanelHeader(g, "选项设置", onClosePanel, this);

  const int cw = g->w();
  const int TAB_Y = 38;
  const int TAB_H = 26;

  // ---- 6 个分类子标签（互斥切换）----
  const char *tabNames[6] = {"压缩", "安全", "输入", "键盘", "显示", "杂项"};
  const int tw = cw / 6;
  for (int i = 0; i < 6; i++) {
    Fl_Toggle_Button *t = new Fl_Toggle_Button(i * tw, TAB_Y, tw - 2, TAB_H,
                                               tabNames[i]);
    t->type(FL_RADIO_BUTTON);   // 同一父组内自动互斥
    t->box(THEME_UP_BOX);
    t->labelsize(11);
    t->labelcolor(M_TEXT2);
    t->clear_visible_focus();
    t->user_data((void*)(intptr_t)i);
    t->callback(onOptionTab, this);
    optTabs[i] = t;
  }

  // ---- 6 个分类页（同一时刻只显示一个）----
  const int PANE_Y = TAB_Y + TAB_H + 12;
  for (int i = 0; i < 6; i++) {
    Fl_Group *p = new Fl_Group(0, PANE_Y, cw, 370);
    p->box(FL_NO_BOX);
    switch (i) {
    case 0: buildOptionCompress(p, 0); break;
    case 1: buildOptionSecurity(p, 0); break;
    case 2: buildOptionInput(p, 0);    break;
    case 3: buildOptionKeys(p, 0);     break;
    case 4: buildOptionDisplay(p, 0);  break;
    case 5: buildOptionMisc(p, 0);     break;
    }
    p->end();
    p->hide();
    optPanes[i] = p;
  }

  showOptionPane(0);
}

void ServerDialog::buildOptionCompress(Fl_Group *p, int y)
{
  const int cw = p->w();
  const int half = cw / 2 - 8;

  optAutoSelect = optCheck(0, y, half, "自动选择");
  optJpeg = optCheck(0, y + 28, half, "允许 JPEG 压缩");

  int yy = y + 62;
  optCaption(0, yy, half, "首选编码");
  optCaption(half + 16, yy, half, "颜色级别");
  yy += 20;

  optEncoding = optChoice(0, yy, half, 26);
  const char *encs[6] = {"Tight", "JPEG", "ZRLE", "Hextile", "H.264", "Raw"};
  for (int i = 0; i < 6; i++)
    optEncoding->add(encs[i]);

  optColorLevel = optChoice(half + 16, yy, half, 26);
  optColorLevel->add("全部");
  optColorLevel->add("中");
  optColorLevel->add("低");
  optColorLevel->add("非常低");

  yy += 42;
  optCustomCompress = optCheck(0, yy, 130, "自定义压缩级别");
  optCompressLevel = optSpin(134, yy + 2, 46, 22);
  optCaption(186, yy, cw - 186, "（0=最快，9=最好）");

  yy += 34;
  optCaption(0, yy, 80, "质量级别");
  optQualityLevel = optSpin(80, yy + 1, 46, 22);
  optCaption(132, yy, cw - 132, "（0=最差，9=最好）");
}

void ServerDialog::buildOptionSecurity(Fl_Group *p, int y)
{
  const int half = p->w() / 2 - 8;

  optCaption(0, y, half, "加密");
  optEncryption = optChoice(0, y + 20, half, 26);
  optEncryption->add("无");
  optEncryption->add("RSA-AES");

  optCaption(0, y + 60, half, "认证");
  optAuth = optChoice(0, y + 80, half, 26);
  optAuth->add("无");
  optAuth->add("标准 VNC（不安全且无加密）");
  optAuth->add("用户名及密码（不安全且无加密）");
}

void ServerDialog::buildOptionInput(Fl_Group *p, int y)
{
  const int cw = p->w();
  const int half = cw / 2 - 8;

  optViewOnly = optCheck(0, y, cw, "仅显示（忽略鼠标及键盘）");

  int yy = y + 32;
  optCaption(0, yy, half, "鼠标");
  optMouseMode = optChoice(0, yy + 18, half, 26);
  optMouseMode->add("模拟鼠标中键");
  optMouseMode->add("服务器未提供光标时，显示本地光标");

  optCaption(half + 16, yy, half, "光标类型");
  optCursorType = optChoice(half + 16, yy + 18, half, 26);
  optCursorType->add("点");
  optCursorType->add("系统");

  yy += 60;
  optSystemKeys = optCheck(0, yy, cw, "全屏时始终发送所有键盘输入");

  yy += 32;
  optCaption(0, yy, cw, "剪贴板");
  optAcceptClip = optCheck(0, yy + 18, cw, "接受来自服务器的剪切板内容");
  optSendClip = optCheck(0, yy + 44, cw, "发送剪切板内容到服务器");
}

void ServerDialog::buildOptionKeys(Fl_Group *p, int y)
{
  const int cw = p->w();

  optCaption(0, y, cw, "键盘快捷键的修饰键：");

  const char *modNames[4] = {"Ctrl", "Shift", "Alt", "Win"};
  for (int i = 0; i < 4; i++) {
    Fl_Toggle_Button *b = new Fl_Toggle_Button(i * 66, y + 22, 60, 28,
                                               modNames[i]);
    b->box(THEME_UP_BOX);
    b->labelsize(11);
    b->labelcolor(M_TEXT);
    b->clear_visible_focus();
    optMod[i] = b;
  }

  Fl_Box *text = new Fl_Box(0, y + 62, cw, 140,
    "要从会话释放键盘控制权，请按 Ctrl+Alt。\n"
    "要将所有键盘输入传递给会话，请按 Ctrl+Alt+G。\n"
    "要切换全屏模式，请按 Ctrl+Alt+Enter。\n"
    "要打开会话的上下文菜单，请按 Ctrl+Alt+M。\n"
    "要将包含 Ctrl+Alt 的组合键直接发送给会话，请按 Ctrl+Alt+Space，\n"
    "在不松开 Ctrl+Alt 的情况下松开空格键，然后按下所需的按键。");
  text->labelsize(11);
  text->labelcolor(M_TEXT2);
  text->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP);
}

void ServerDialog::buildOptionDisplay(Fl_Group *p, int y)
{
  const int cw = p->w();

  optCaption(0, y, cw, "显示模式");
  optDisplayMode = optChoice(0, y + 20, cw / 2, 26);
  optDisplayMode->add("窗口化");
  optDisplayMode->add("在当前显示器上全屏显示");
  optDisplayMode->add("为所有显示器开启全屏模式");
  optDisplayMode->add("为选定显示器开启全屏模式");
}

void ServerDialog::buildOptionMisc(Fl_Group *p, int y)
{
  const int cw = p->w();

  optShared = optCheck(0, y, cw, "共享（不要断开其他查看器）");
  optReconnect = optCheck(0, y + 28, cw, "连接错误时询问是否重新连接");
  optAudio = optCheck(0, y + 56, cw, "播放来自服务器的音频");
}

void ServerDialog::onOptionTab(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->showOptionPane((int)(intptr_t)widget->user_data());
}

void ServerDialog::showOptionPane(int idx)
{
  if (idx < 0 || idx >= 6)
    return;
  activeOpt = idx;
  for (int i = 0; i < 6; i++) {
    if (optPanes[i] == nullptr)
      continue;
    if (i == idx)
      optPanes[i]->show();
    else
      optPanes[i]->hide();
  }
  if (optTabs[idx] != nullptr)
    optTabs[idx]->value(1);
  panels[1]->redraw();
}

void ServerDialog::loadOptions()
{
  if (optAutoSelect == nullptr)
    return;

  optAutoSelect->value(autoSelect ? 1 : 0);
  optJpeg->value(rfb::CConnection::noJpeg ? 0 : 1);

  // 首选编码
  const char *encs[6] = {"Tight", "JPEG", "ZRLE", "Hextile", "H.264", "Raw"};
  int ei = 0;
  for (int i = 0; i < 6; i++) {
    if (preferredEncoding == encs[i]) {
      ei = i;
      break;
    }
  }
  optEncoding->value(ei);

  // 颜色级别：0=全部 1=中 2=低 3=非常低
  int ci = 0;
  if (!fullColour) {
    switch ((int)lowColourLevel) {
    case 0: ci = 3; break;
    case 1: ci = 2; break;
    case 2: ci = 1; break;
    default: ci = 0; break;
    }
  }
  optColorLevel->value(ci);

  optCustomCompress->value(customCompressLevel ? 1 : 0);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", (int)compressLevel);
  optCompressLevel->value(buf);
  snprintf(buf, sizeof(buf), "%d", (int)qualityLevel);
  optQualityLevel->value(buf);

  // 安全：从 secTypes 字符串反推
#if defined(HAVE_GNUTLS) || defined(HAVE_NETTLE)
  const std::string secTypes =
      rfb::SecurityClient::secTypes.getValueStr();
  optEncryption->value(secTypes.find("RA2") != std::string::npos ? 1 : 0);
  if (secTypes.find("Plain") != std::string::npos)
    optAuth->value(2);
  else if (secTypes.find("VncAuth") != std::string::npos)
    optAuth->value(1);
  else
    optAuth->value(0);
#else
  optEncryption->value(0);
  optAuth->value(1);
#endif

  // 输入
  optViewOnly->value(viewOnly ? 1 : 0);
  optMouseMode->value(emulateMiddleButton ? 0 : 1);
  optCursorType->value(cursorType == "System" ? 1 : 0);
  optSystemKeys->value(fullscreenSystemKeys ? 1 : 0);
  optAcceptClip->value(acceptClipboard ? 1 : 0);
  optSendClip->value(sendClipboard ? 1 : 0);

  // 键盘修饰键
  const std::string mods = shortcutModifiers.getValueStr();
  optMod[0]->value(mods.find("Ctrl") != std::string::npos ? 1 : 0);
  optMod[1]->value(mods.find("Shift") != std::string::npos ? 1 : 0);
  optMod[2]->value(mods.find("Alt") != std::string::npos ? 1 : 0);
  optMod[3]->value(mods.find("Super") != std::string::npos ? 1 : 0);

  // 显示模式
  if (!fullScreen)
    optDisplayMode->value(0);
  else if (fullScreenMode == "All")
    optDisplayMode->value(2);
  else if (fullScreenMode == "Selected")
    optDisplayMode->value(3);
  else
    optDisplayMode->value(1);

  // 杂项
  optShared->value(shared ? 1 : 0);
  optReconnect->value(reconnectOnError ? 1 : 0);
#ifdef HAVE_AUDIO
  optAudio->value(playAudio ? 1 : 0);
#else
  optAudio->value(0);
  optAudio->deactivate();
#endif
}

void ServerDialog::storeOptions()
{
  if (optAutoSelect == nullptr)
    return;

  autoSelect.setParam(optAutoSelect->value());
  rfb::CConnection::noJpeg.setParam(!optJpeg->value());

  const char *encs[6] = {"Tight", "JPEG", "ZRLE", "Hextile", "H.264", "Raw"};
  int ei = optEncoding->value();
  if (ei < 0 || ei > 5)
    ei = 0;
  preferredEncoding.setParam(encs[ei]);

  switch (optColorLevel->value()) {
  case 1: fullColour.setParam(false); lowColourLevel.setParam(2); break;
  case 2: fullColour.setParam(false); lowColourLevel.setParam(1); break;
  case 3: fullColour.setParam(false); lowColourLevel.setParam(0); break;
  default: fullColour.setParam(true); break;
  }

  customCompressLevel.setParam(optCustomCompress->value());
  compressLevel.setParam(atoi(optCompressLevel->value()));
  qualityLevel.setParam(atoi(optQualityLevel->value()));

  // 安全：按「加密 + 认证」组合出 secTypes 列表
#if defined(HAVE_GNUTLS) || defined(HAVE_NETTLE)
  {
    std::string s;
    if (optEncryption->value() == 1) {
      s = "RA2ne,RA2,RA256";
      if (optAuth->value() == 1)
        s += ",VncAuth";
      else if (optAuth->value() == 2)
        s += ",Plain";
    } else {
      if (optAuth->value() == 0)
        s = "None";
      else if (optAuth->value() == 1)
        s = "VncAuth";
      else
        s = "Plain";
    }
    rfb::SecurityClient::secTypes.setParam(s.c_str());
  }
#endif

  viewOnly.setParam(optViewOnly->value());
  emulateMiddleButton.setParam(optMouseMode->value() == 0);
  acceptClipboard.setParam(optAcceptClip->value());
  sendClipboard.setParam(optSendClip->value());
  fullscreenSystemKeys.setParam(optSystemKeys->value());
  cursorType.setParam(optCursorType->value() == 1 ? "System" : "Dot");

  // 键盘修饰键
  {
    std::list<std::string> mods;
    if (optMod[0]->value())
      mods.push_back("Ctrl");
    if (optMod[1]->value())
      mods.push_back("Shift");
    if (optMod[2]->value())
      mods.push_back("Alt");
    if (optMod[3]->value())
      mods.push_back("Super");
    shortcutModifiers.setParam(mods);
  }

  switch (optDisplayMode->value()) {
  case 1: fullScreen.setParam(true); fullScreenMode.setParam("Current"); break;
  case 2: fullScreen.setParam(true); fullScreenMode.setParam("All"); break;
  case 3: fullScreen.setParam(true); fullScreenMode.setParam("Selected"); break;
  default: fullScreen.setParam(false); break;
  }

  shared.setParam(optShared->value());
  reconnectOnError.setParam(optReconnect->value());
#ifdef HAVE_AUDIO
  playAudio.setParam(optAudio->value());
#endif
}

// ============================================================
// 自动更新面板
// ============================================================

namespace {
/* 后台任务的返回结果（由工作线程 new，主线程 delete） */
struct UpdateTask {
  ServerDialog *self;
  bool install;              // 这次任务是「安装」而非「检查」
  bool ok;
  core::ReleaseInfo info;
  std::string error;
};
}

/* 主线程侧：后台任务完成后刷新界面 */
void ServerDialog::updateTaskDone(void *ptr)
{
  UpdateTask *t = (UpdateTask*)ptr;
  if (g_updOwner == t->self)
    t->self->handleUpdateTask(t->ok, t->info, t->error, t->install);
  delete t;
}

/* 刷新更新面板的统计三行 */
void ServerDialog::setUpdateStats(const char *stage, const char *speed,
                                 const char *eta)
{
  updStage->label(stage);
  updSpeed->label(speed);
  updEta->label(eta);
}

void ServerDialog::handleUpdateTask(bool ok, const core::ReleaseInfo &info,
                                    const std::string &error, bool install)
{
  updBusy = false;
  if (updThread.joinable())
    updThread.join();

  if (install) {
    if (!ok) {
      const std::string msg = std::string("安装失败：") + error;
      setUpdateStats(msg.c_str());
      updButton->activate();
      updButton->redraw();
    }
    // 安装成功时安装器会接管进程，无需再更新界面
    return;
  }

  updButton->activate();

  if (!ok) {
    const std::string msg = std::string("检查失败：") + error;
    setUpdateStats(msg.c_str());
    updButton->label("重试");
    updButton->redraw();
    return;
  }

  if (core::UpdateChecker::isNewer(info.version, PACKAGE_VERSION)) {
    updLatestVersion = info.version;
    updDownloadUrl = info.downloadUrl;
    updChecksumUrl = info.checksumUrl;

    updVersion->label(("v" PACKAGE_VERSION "  →  v" + info.version).c_str());
    setUpdateStats("当前阶段：发现新版本");
    updChangelog->label(info.notes.empty()
                        ? "（该版本未提供更新说明）"
                        : info.notes.c_str());
    updButton->label("立即更新");
  } else {
    updLatestVersion.clear();
    updVersion->label(("v" PACKAGE_VERSION "  ·  已是最新版本").c_str());
    setUpdateStats("当前阶段：无可用更新");
    updChangelog->label("当前已是最新版本。");
    updButton->label("已是最新");
    updButton->deactivate();
  }

  updButton->redraw();
  panels[2]->redraw();
}

void ServerDialog::onUpdateButton(Fl_Widget*, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  if (dialog->updLatestVersion.empty())
    dialog->startUpdateCheck();
  else
    dialog->startUpdateInstall();
}

void ServerDialog::startUpdateCheck()
{
  if (updBusy.exchange(true))
    return;
  if (updThread.joinable())
    updThread.join();

  updStage->label("当前阶段：正在检查新版本...");
  updButton->deactivate();
  panels[2]->redraw();

  updThread = std::thread([this]() {
    UpdateTask *t = new UpdateTask;
    t->self = this;
    t->install = false;
    t->ok = core::UpdateChecker::fetchLatestRelease(&t->info, &t->error);
    Fl::awake(updateTaskDone, t);
  });
}

void ServerDialog::startUpdateInstall()
{
  if (updDownloadUrl.empty())
    return;
  if (updBusy.exchange(true))
    return;
  if (updThread.joinable())
    updThread.join();

  updStage->label("当前阶段：正在下载并安装...");
  updButton->deactivate();
  panels[2]->redraw();

  const std::string url = updDownloadUrl;
  const std::string checksum = updChecksumUrl;
  const std::string version = updLatestVersion;

  updThread = std::thread([this, url, checksum, version]() {
    UpdateTask *t = new UpdateTask;
    t->self = this;
    t->install = true;
    t->ok = core::UpdateChecker::downloadAndInstall(url, checksum, version,
                                                    &t->error);
    Fl::awake(updateTaskDone, t);
  });
}

void ServerDialog::buildUpdatePanel(Fl_Group *g)
{
  buildPanelHeader(g, "自动更新", onClosePanel, this);

  const int cw = g->w();

  // ---- 版本对比 ----
  updVersion = new Fl_Box(0, 38, cw, 26,
                          ("v" PACKAGE_VERSION "  ·  点击下方按钮检查更新").c_str());
  updVersion->labelfont(FL_HELVETICA_BOLD);
  updVersion->labelsize(14);
  updVersion->labelcolor(M_TEXT);
  updVersion->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

  // ---- 统计三行 ----
  int y = 74;
  const int statH = 22;
  updStage = new Fl_Box(0, y, cw, statH, "当前阶段：待检查");
  updStage->labelsize(11);
  updStage->labelcolor(M_TEXT2);
  updStage->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  updSpeed = new Fl_Box(0, y + statH, cw, statH, "下载速度：—");
  updSpeed->labelsize(11);
  updSpeed->labelcolor(M_TEXT2);
  updSpeed->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  updEta = new Fl_Box(0, y + statH * 2, cw, statH, "剩余时间：—");
  updEta->labelsize(11);
  updEta->labelcolor(M_TEXT2);
  updEta->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  // ---- 更新说明 ----
  y += statH * 3 + 10;
  updChangelog = new Fl_Box(0, y, cw, 150, "尚未检查更新。");
  updChangelog->box(FL_BORDER_BOX);
  updChangelog->color(M_CARD);
  updChangelog->labelsize(11);
  updChangelog->labelcolor(M_TEXT2);
  updChangelog->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE |
                      FL_ALIGN_WRAP);

  // ---- 立即更新 ----
  y += 162;
  updButton = new Fl_Button(cw / 2 - 70, y, 140, 32, "检查更新");
  updButton->box(THEME_UP_BOX);
  updButton->color(M_ACCENT);
  updButton->labelcolor(FL_WHITE);
  updButton->labelsize(12);
  updButton->callback(onUpdateButton, this);
}

void ServerDialog::buildAboutPanel(Fl_Group *g)
{
  buildPanelHeader(g, "关于", onClosePanel, this);

  int cw = g->w();

  // 版本
  Fl_Box *ver = new Fl_Box(0, 42, cw, 26,
                           "TuPigVNC  v" PACKAGE_VERSION " · x64");
  ver->labelfont(FL_HELVETICA_BOLD);
  ver->labelsize(14);
  ver->labelcolor(M_TEXT);
  ver->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

  // 简介
  Fl_Box *desc = new Fl_Box(0, 74, cw, 40,
                            "跨平台 VNC 远程桌面客户端\n"
                            "基于 TigerVNC，Material 3 极简单页界面");
  desc->labelsize(11);
  desc->labelcolor(M_TEXT2);
  desc->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  // 链接按钮
  const char *links[4] = {"项目主页", "问题反馈", "版本发布", "许可证"};
  int bw = 92, gap = 6;
  int total = 4 * bw + 3 * gap;
  int bx = (cw - total) / 2;
  for (int i = 0; i < 4; i++) {
    Fl_Button *b = new Fl_Button(bx + i * (bw + gap), 122, bw, 28, links[i]);
    b->box(THEME_UP_BOX);
    b->color(M_CARD);
    b->labelcolor(M_ACCENT);
    b->labelsize(11);
    b->clear_visible_focus();
  }
}

void ServerDialog::refreshHistory()
{
  if (historyBrowser == nullptr)
    return;
  historyBrowser->clear();
  for (const ServerHistoryEntry& e : serverHistory) {
    char line[512];
    snprintf(line, sizeof(line), "%s    |    %s    |    %d 次",
             e.host.c_str(), e.name.c_str(), e.useCount);
    historyBrowser->add(line);
  }
}

void ServerDialog::showPanel(int idx)
{
  // 离开选项面板时保存设置
  if (activePanel == 1 && idx != 1)
    storeOptions();

  for (int i = 0; i < 4; i++) {
    if (i == idx)
      panels[i]->show();
    else
      panels[i]->hide();
    panels[i]->position(20, panelTopY);  // 与 buildLayout 的 PAD 一致
  }
  activePanel = idx;

  if (idx >= 0)
    scroll->scroll_to(0, panelTopY);

  scroll->redraw();
  redraw();
}

void ServerDialog::repositionWidgets()
{
  for (int i = 0; i < 4; i++)
    panels[i]->position(20, panelTopY);
  scroll->redraw();
}

void ServerDialog::onEntryClicked(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  int idx = (int)(intptr_t)widget->user_data();
  dialog->showPanel(dialog->activePanel == idx ? -1 : idx);
}

void ServerDialog::onClosePanel(Fl_Widget* /*widget*/, void *data)
{
  ((ServerDialog*)data)->showPanel(-1);
}


ServerDialog::~ServerDialog()
{
  g_updOwner = nullptr;
  if (updThread.joinable())
    updThread.join();
}


std::string ServerDialog::run(const char* servername)
{
  ServerDialog dialog;

  dialog.serverName->value(servername);

  dialog.show();

  try {
    dialog.loadServerHistory();
    std::list<std::string> names;
    for (const ServerHistoryEntry& e : dialog.serverHistory)
      names.push_back(e.host);
    dialog.serverName->set_suggestions(names);
    dialog.refreshHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to load the server history: %s"), e.what());
  }

  while (dialog.shown()) Fl::wait();

  if (dialog.serverName->value() == nullptr) {
    return "";
  }

  return dialog.serverName->value();
}

void ServerDialog::handleLoad(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();

  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TuPigVNC configuration (*.tigervnc)"),
                                                      Fl_File_Chooser::SINGLE,
                                                      _("Select a TuPigVNC configuration file"));
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  // 阻塞等待，直到用户做出选择。
  while(file_chooser->shown())
    Fl::wait();
  
  // 用户是否点了取消？
  if (file_chooser->value() == nullptr) {
    delete(file_chooser);
    return;
  }
  
  const char* filename = file_chooser->value();
  dialog->updateUsedDir(filename);

  try {
    dialog->serverName->value(loadViewerParameters(filename).c_str());
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to load the specified configuration file:\n\n%s"),
             e.what());
  }

  delete(file_chooser);
}


void ServerDialog::handleSaveAs(Fl_Widget* /*widget*/, void* data)
{ 
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();
  const char* filename;
  if (dialog->usedDir.empty())
    dialog->usedDir = core::getuserhomedir();
  
  Fl_File_Chooser* file_chooser = new Fl_File_Chooser(dialog->usedDir.c_str(),
                                                      _("TuPigVNC configuration (*.tigervnc)"),
                                                      Fl_File_Chooser::CREATE,
                                                      _("Save the TuPigVNC configuration to file"));
  
  file_chooser->preview(0);
  file_chooser->previewButton->hide();
  file_chooser->show();
  
  while(1) {
    
    // 阻塞等待，直到用户做出选择。
    while(file_chooser->shown())
      Fl::wait();
    
    // 用户是否点了取消？
    if (file_chooser->value() == nullptr) {
      delete(file_chooser);
      return;
    }
    
    filename = file_chooser->value();
    dialog->updateUsedDir(filename);
    
    FILE* f = fopen(filename, "r");
    if (f) {

      // 文件已存在。
      fclose(f);
      int overwrite_choice = fl_choice(_("%s already exists. Do you want to overwrite?"), 
                                       nullptr, _("No"), _("Overwrite"), filename);
      if (overwrite_choice != 2) {

        // 如果用户不想覆盖：
        file_chooser->show();
        continue;
      }
    }

    break;
  }
  
  try {
    saveViewerParameters(filename, servername);
  } catch (std::exception& e) {
    vlog.error("%s", e.what());
    fl_alert(_("Unable to save the specified configuration "
               "file:\n\n%s"), e.what());
  }
  
  delete(file_chooser);
}


void ServerDialog::handleCancel(Fl_Widget* /*widget*/, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;

  dialog->serverName->value("");
  dialog->hide();
}


void ServerDialog::handleConnect(Fl_Widget* /*widget*/, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const char* servername = dialog->serverName->value();

  dialog->hide();

  try {
    saveViewerParameters(nullptr, servername);
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the default configuration: %s"),
               e.what());
  }

  // 更新历史：已存在则累加次数并置顶，否则插入新记录
  int64_t now = time(nullptr);
  auto it = std::find_if(dialog->serverHistory.begin(),
                         dialog->serverHistory.end(),
                         [&](const ServerHistoryEntry& e) {
                           return same_server(e.host, servername);
                         });
  ServerHistoryEntry entry;
  if (it != dialog->serverHistory.end()) {
    entry = *it;
    dialog->serverHistory.erase(it);
  } else {
    entry.host = servername;
    entry.name = servername;
    entry.useCount = 0;
  }
  entry.useCount++;
  entry.lastUsed = now;
  dialog->serverHistory.push_front(entry);
  dialog->pruneHistory();

  try {
    dialog->saveServerHistory();
  } catch (std::exception& e) {
    vlog.error(_("Unable to save the server history: %s"), e.what());
  }
}


static bool same_server(const std::string& a, const std::string& b)
{
  std::string hostA, hostB;
  int portA, portB;

#ifndef WIN32
  if ((a.find("/") != std::string::npos) ||
      (b.find("/") != std::string::npos))
    return a == b;
#endif

  try {
    network::getHostAndPort(a.c_str(), &hostA, &portA);
    network::getHostAndPort(b.c_str(), &hostB, &portB);
  } catch (std::exception& e) {
    return false;
  }

  if (hostA != hostB)
    return false;

  if (portA != portB)
    return false;

  return true;
}


/* ---- 极简 JSON 字符串转义/提取（固定 schema，无第三方库）---- */

static std::string jsonEscape(const std::string& s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"':  out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n";  break;
    case '\r': out += "\\r";  break;
    case '\t': out += "\\t";  break;
    default:   out += c;      break;
    }
  }
  return out;
}

static std::string jsonExtractString(const std::string& obj, const char* key)
{
  std::string needle = std::string("\"") + key + "\":\"";
  size_t p = obj.find(needle);
  if (p == std::string::npos)
    return "";
  p += needle.size();
  std::string out;
  while (p < obj.size() && obj[p] != '"') {
    if (obj[p] == '\\' && p + 1 < obj.size()) {
      ++p;
      char c = obj[p];
      if (c == 'n')      out += '\n';
      else if (c == 'r') out += '\r';
      else if (c == 't') out += '\t';
      else               out += c;
    } else {
      out += obj[p];
    }
    ++p;
  }
  return out;
}

static int64_t jsonExtractInt(const std::string& obj, const char* key)
{
  std::string needle = std::string("\"") + key + "\":";
  size_t p = obj.find(needle);
  if (p == std::string::npos)
    return 0;
  p += needle.size();
  return (int64_t)atoll(obj.c_str() + p);
}

void ServerDialog::parseHistoryJson(const std::string& content)
{
  size_t pos = 0;
  while ((pos = content.find('{', pos)) != std::string::npos) {
    size_t end = content.find('}', pos);
    if (end == std::string::npos)
      break;

    std::string obj = content.substr(pos, end - pos + 1);
    ServerHistoryEntry e;
    e.host = jsonExtractString(obj, "host");
    e.name = jsonExtractString(obj, "name");
    e.lastUsed = jsonExtractInt(obj, "lastUsed");
    e.useCount = (int)jsonExtractInt(obj, "useCount");
    if (e.name.empty())
      e.name = e.host;

    // 去重（同一服务器只保留一条）
    if (!e.host.empty() &&
        std::find_if(serverHistory.begin(), serverHistory.end(),
                     [&](const ServerHistoryEntry& x) {
                       return same_server(x.host, e.host);
                     }) == serverHistory.end())
      serverHistory.push_back(e);

    pos = end + 1;
  }
}

void ServerDialog::pruneHistory()
{
  // 超出上限时淘汰「使用次数最少」的记录；次数相同则淘汰「最久未用」
  while (serverHistory.size() > SERVER_HISTORY_MAX) {
    auto worst = std::min_element(
      serverHistory.begin(), serverHistory.end(),
      [](const ServerHistoryEntry& a, const ServerHistoryEntry& b) {
        if (a.useCount != b.useCount)
          return a.useCount < b.useCount;
        return a.lastUsed < b.lastUsed;
      });
    serverHistory.erase(worst);
  }
}

void ServerDialog::migrateOldHistory()
{
  std::list<std::string> old;

#ifdef _WIN32
  try {
    old = loadHistoryFromRegKey();
  } catch (std::exception& e) {
    vlog.error(_("Failed to read legacy history from registry: %s"), e.what());
  }
#else
  const char* stateDir = core::getvncstatedir();
  if (stateDir != nullptr) {
    char filepath[PATH_MAX];
    snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY);
    FILE* f = fopen(filepath, "r");
    if (f) {
      char line[256];
      while (fgets(line, sizeof(line), f)) {
        int len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
          line[--len] = '\0';
        if (len > 0)
          old.push_back(line);
      }
      fclose(f);
    }
  }
#endif

  // 旧数据没有次数/时间信息，按出现顺序赋递减时间戳
  int64_t ts = time(nullptr);
  for (const std::string& host : old) {
    if (std::find_if(serverHistory.begin(), serverHistory.end(),
                     [&](const ServerHistoryEntry& x) {
                       return same_server(x.host, host);
                     }) != serverHistory.end())
      continue;
    ServerHistoryEntry e;
    e.host = host;
    e.name = host;
    e.lastUsed = ts--;
    e.useCount = 1;
    serverHistory.push_back(e);
  }

  if (!serverHistory.empty()) {
    try {
      saveServerHistory();
    } catch (std::exception& e) {
      vlog.error(_("Failed to migrate history: %s"), e.what());
    }
  }
}

void ServerDialog::loadServerHistory()
{
  serverHistory.clear();

  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    return;

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY_JSON);

  FILE* f = fopen(filepath, "r");
  if (!f) {
    // 首次运行：尝试从旧版（注册表 / tigervnc.history）迁移
    migrateOldHistory();
    return;
  }

  std::string content;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
    content.append(buf, n);
  fclose(f);

  parseHistoryJson(content);
  pruneHistory();
}

void ServerDialog::saveServerHistory()
{
  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  char filepath[PATH_MAX];
  snprintf(filepath, sizeof(filepath), "%s/%s", stateDir, SERVER_HISTORY_JSON);

  FILE* f = fopen(filepath, "w");
  if (!f) {
    std::string msg = core::format(_("Failed to open \"%s\""), filepath);
    throw core::posix_error(msg.c_str(), errno);
  }

  fprintf(f, "[\n");
  bool first = true;
  for (const ServerHistoryEntry& e : serverHistory) {
    if (!first) fprintf(f, ",\n");
    first = false;
    fprintf(f, "  {\"host\":\"%s\",\"name\":\"%s\","
               "\"lastUsed\":%lld,\"useCount\":%d}",
            jsonEscape(e.host).c_str(), jsonEscape(e.name).c_str(),
            (long long)e.lastUsed, e.useCount);
  }
  fprintf(f, "\n]\n");
  fclose(f);
}

void ServerDialog::updateUsedDir(const char* filename)
{
  char * name = strdup(filename);
  usedDir = dirname(name);
  free(name);
}

void ServerDialog::onServerHistoryRemove(Fl_Widget*, std::string s, void* data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  dialog->serverHistory.remove_if([&](const ServerHistoryEntry& e) {
    return same_server(e.host, s);
  });
  dialog->saveServerHistory();
}

std::string ServerDialog::serverHistoryNormalize(const std::string s)
{
  // 转成小写以实现大小写不敏感
  std::string result = s;
  transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}
