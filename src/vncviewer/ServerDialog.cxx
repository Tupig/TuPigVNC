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
#include <stdio.h>
#include <time.h>
#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <libgen.h>

// FIXME: 规避 FLTK 引入 windows.h 的问题
#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
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
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

/* 后台更新线程的存活判定。
 * 用单调递增的实例 token 而非裸指针：ServerDialog 是栈对象，
 * 地址会被复用，裸指针比较可能把旧结果套到新对话框上。 */
static std::atomic<uint64_t> g_tokenSeq{0};      // 单调递增
static std::atomic<uint64_t> g_currentToken{0};  // 当前存活实例，0 表示无

const char* SERVER_HISTORY="tigervnc.history";        // 旧格式（仅用于迁移）
const char* SERVER_HISTORY_JSON="history.json";       // 新格式：User/history.json
static const size_t SERVER_HISTORY_MAX = 250;         // 历史上限

// 前向声明（定义在文件下方，handleConnect 等处会用到）
static bool same_server(const std::string& a, const std::string& b);

ServerDialog::ServerDialog()
  : Fl_Window(540, 760, "TuPigVNC"),
    scroll(nullptr), historyBrowser(nullptr), activePanel(-1), panelTopY(0),
    topbarH(48),
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
  for (int i = 0; i < 3; i++)
    themeBtns[i] = nullptr;
  for (int i = 0; i < 3; i++)
    recentChips[i] = nullptr;

  buildLayout();
  loadOptions();

  updToken = ++g_tokenSeq;
  g_currentToken = updToken;

  // 启动 2 秒后静默检查更新（不阻塞界面，有新版给入口加红点）
  Fl::add_timeout(2.0, startupCheck, this);
}

void ServerDialog::startupCheck(void *data)
{
  ((ServerDialog*)data)->checkUpdateSilently();
}


// ============================================================
// 极简单页 · Material 3
// 所有入口平铺在主界面 · 无侧栏无标签无菜单 · 点击原地展开
// 颜色统一由 fltk/material.h 的 mat:: 调色板提供
// ============================================================

void ServerDialog::buildLayout()
{
  const int PAD = 20;           // 左右内边距
  const int GAP = 8;            // 元素间距
  const int TOPBAR_H = 48;      // 顶栏高度
  const int cw = w() - PAD * 2; // 内容宽度

  // 窗口高度不超过屏幕 92%，小屏不超出可视区
  const int winH = std::min(760, (int)(Fl::h() * 0.92));
  if (h() > winH)
    size(w(), winH);

  mat::initColors();
  color(mat::Card);

  // ===== 顶栏 =====
  Fl_Box *topbar = new Fl_Box(0, 0, w(), TOPBAR_H);
  topbar->box(FL_FLAT_BOX);
  topbar->color(mat::Card);

  // 品牌 logo（22×22 显示器图标）+ 名称
  new MLogo(PAD, 0, 22, TOPBAR_H);

  Fl_Box *title = new Fl_Box(PAD + 30, 0, 160, TOPBAR_H, "TuPigVNC");
  title->labelfont(FL_HELVETICA_BOLD);
  title->labelsize(14);
  title->labelcolor(mat::Text);
  title->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  // 主题切换（Material / Linear / Arc）
  {
    const char *themeNames[3] = {"Material", "Linear", "Arc"};
    const int tw = 64;
    const int tx = w() - PAD - tw * 3 - 4 * 2;
    for (int i = 0; i < 3; i++) {
      MButton *b = new MButton(tx + i * (tw + 4), (TOPBAR_H - 26) / 2, tw, 26,
                               themeNames[i], MButton::Theme);
      b->radius(13);
      b->fontSize(11);
      b->user_data((void*)(intptr_t)i);
      b->callback(onThemeClicked, this);
      b->setActive(i == 0);
      themeBtns[i] = b;
    }
  }

  // 顶栏底部分隔线
  Fl_Box *divider = new Fl_Box(0, TOPBAR_H - 1, w(), 1);
  divider->box(FL_FLAT_BOX);
  divider->color(mat::Stroke);

  // ===== 主滚动区 =====
  scroll = new Fl_Scroll(0, TOPBAR_H, w(), h() - TOPBAR_H);
  scroll->box(FL_FLAT_BOX);
  scroll->color(mat::Card);
  scroll->begin();

  int y = 18;

  // ---- 连接区标题 ----
  Fl_Box *h1 = new Fl_Box(PAD, y, cw, 24, "连接 VNC 服务器");
  h1->labelfont(FL_HELVETICA_BOLD);
  h1->labelsize(17);
  h1->labelcolor(mat::Text);
  h1->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 26;

  Fl_Box *hint = new Fl_Box(PAD, y, cw, 18, "输入地址，回车即可连接");
  hint->labelsize(12);
  hint->labelcolor(mat::Text2);
  hint->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  y += 26;

  // ---- 地址输入 + 连接 ----
  const int btnW = 84;
  serverName = new MInput(PAD, y, cw - btnW - GAP, 34);
  serverName->placeholder("主机地址:端口");
  // 提示语写的是「输入地址，回车即可连接」，这里补上回车回调
  serverName->when(FL_WHEN_ENTER_KEY | FL_WHEN_RELEASE);
  serverName->callback(handleConnect, this);

  MButton *connectBtn =
      new MButton(PAD + cw - btnW, y, btnW, 34, "连接", MButton::Filled);
  connectBtn->radius(12);
  connectBtn->callback(handleConnect, this);
  y += 34 + 12;

  // ---- 最近连接（内联 chip）----
  recentLabel = new Fl_Box(PAD, y + 2, 34, 20, "最近");
  recentLabel->labelsize(11);
  recentLabel->labelcolor(mat::Text3);
  recentLabel->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
  {
    int chipX = PAD + 34 + 6;
    for (int i = 0; i < 3; i++) {
      recentChips[i] = new MChip(chipX, y, 120, 24, "");
      recentChips[i]->user_data((void*)(intptr_t)i);
      recentChips[i]->callback(onRecentChipClicked, this);
      recentChips[i]->hide();
      chipX += 124;
    }
  }
  y += 24 + 12;

  // ---- 次要操作 ----
  MButton *loadBtn = new MButton(PAD, y, 90, 28, "载入配置", MButton::Ghost);
  loadBtn->radius(7);
  loadBtn->fontSize(11);
  loadBtn->callback(handleLoad, this);

  MButton *saveBtn =
      new MButton(PAD + 90 + GAP, y, 80, 28, "另存为", MButton::Ghost);
  saveBtn->radius(7);
  saveBtn->fontSize(11);
  saveBtn->callback(handleSaveAs, this);
  y += 28 + 22;

  // ---- 2×2 功能入口 ----
  {
    const char* entryTitles[4] = {"连接历史", "选项设置", "自动更新", "关于"};
    const char* entrySubs[4] = {"最近连接记录", "压缩 · 安全 · 输入 · 显示",
                                 "检查是否有新版本",
                                 "v" PACKAGE_VERSION " · x64"};
    const int colW = (cw - GAP) / 2;
    const int entryStartY = y;
    for (int i = 0; i < 4; i++) {
      const int col = i % 2;
      const int row = i / 2;
      const int ex = PAD + col * (colW + GAP);
      const int ey = entryStartY + row * (52 + GAP);
      MEntryCard *e = new MEntryCard(ex, ey, colW, 52, entryTitles[i],
                                     entrySubs[i], (MEntryCard::Icon)i);
      e->user_data((void*)(intptr_t)i);
      e->callback(onEntryClicked, this);
      entries_[i] = e;
    }
    panelTopY = entryStartY + 2 * (52 + GAP) + 8;
  }

  // ---- 展开面板（初始全部隐藏）----
  for (int i = 0; i < 4; i++) {
    panels[i] = new MPanel(PAD, panelTopY, cw, 460);
    switch (i) {
    case 0: buildHistoryPanel(panels[i]); break;
    case 1: buildOptionsPanel(panels[i]); break;
    case 2: buildUpdatePanel(panels[i]);  break;
    case 3: buildAboutPanel(panels[i]);    break;
    }
    panels[i]->end();
    panels[i]->hide();
    fitPanelHeight(i);
  }

  scroll->end();

  /* 为保证缩放行为正常所必需 */
  resizable(nullptr);

  // 窗口高度贴合内容：设计稿是「宽度固定 540，高度自适应，最大 92vh」，
  // 固定 760 会在底部留一大片空白。
  applyWindowHeight();

  callback(this->handleCancel, this);
}

void ServerDialog::applyWindowHeight()
{
  const int maxH = (int)(Fl::h() * 0.92);
  // 收起时到 2×2 入口卡片下方，展开时加上面板高度
  int contentBottom = panelTopY + 22;
  if (activePanel >= 0 && panels[activePanel] != nullptr)
    contentBottom = panelTopY + panels[activePanel]->h() + 22;

  int targetH = topbarH + contentBottom;
  if (targetH > maxH)
    targetH = maxH;
  if (targetH < topbarH + 160)
    targetH = topbarH + 160;

  size(w(), targetH);
  if (scroll != nullptr)
    scroll->resize(0, topbarH, w(), targetH - topbarH);
  redraw();
}

void ServerDialog::fitPanelHeight(int idx)
{
  if (idx < 0 || idx >= 4 || panels[idx] == nullptr)
    return;

  // 面板高度贴合内容：取所有子控件的最大底边 + 底部内边距。
  // 固定 460 会让「关于」「自动更新」这类短面板底部留一大片空白。
  int bottom = 0;
  for (int c = 0; c < panels[idx]->children(); c++) {
    Fl_Widget *w = panels[idx]->child(c);
    bottom = std::max(bottom, w->y() + w->h());
  }
  if (bottom > 0)
    panels[idx]->size(panels[idx]->w(), bottom + 16);
}


/* 面板通用标题栏：标题 + 右侧「收起」 */
static void buildPanelHeader(Fl_Group *g, const char *text,
                             Fl_Callback *closeCb, void *closeData)
{
  int cw = g->w();
  Fl_Box *t = new Fl_Box(16, 0, cw - 88, 34, text);
  t->labelfont(FL_HELVETICA_BOLD);
  t->labelsize(13);
  t->labelcolor(mat::Text);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  MButton *close = new MButton(cw - 62, 5, 46, 24, "收起", MButton::Ghost);
  close->radius(7);
  close->fontSize(11);
  close->callback(closeCb, closeData);
}

void ServerDialog::buildHistoryPanel(Fl_Group *g)
{
  buildPanelHeader(g, "连接历史", onClosePanel, this);

  const int cw = g->w();

  // 列宽：主机 / 名称 / 次数 / 最后连接（最后一列占剩余宽度）
  const int colHost = 200, colName = 100, colCount = 60;
  const int colAgo = cw - 32 - colHost - colName - colCount;

  // 表头：每条占一个 Fl_Box，宽度与数据列严格对齐
  const int hx = 16;
  const char *hdrText[4] = {"主机", "名称", "次数", "最后连接"};
  const int hdrW[4] = {colHost, colName, colCount, colAgo};
  int cx = hx;
  for (int c = 0; c < 4; c++) {
    Fl_Box *b = new Fl_Box(cx, 40, hdrW[c], 20, hdrText[c]);
    b->labelsize(11);
    b->labelcolor(mat::Text2);
    b->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    cx += hdrW[c];
  }

  // 历史列表（高度按记录数，最多 12 行；双击即连接）
  const int rows = std::max<int>(1, (int)serverHistory.size());
  const int listH = std::min(12, rows) * 22 + 8;
  historyBrowser = new Fl_Browser(16, 62, cw - 32, listH);
  historyBrowser->box(FL_FLAT_BOX);
  historyBrowser->color(mat::Card);
  historyBrowser->textsize(12);
  historyBrowser->textcolor(mat::Text);
  historyBrowser->callback(onHistoryDoubleClick, this);

  // 用 \t 做列分隔，列宽与表头一致（Fl_Browser 原生多列对齐）
  static const int kColWidths[5] = {200, 100, 60, 148, 0};
  historyBrowser->column_widths(kColWidths);
  historyBrowser->column_char('\t');
}

// ============================================================
// 选项面板 · 6 个分类页
// ============================================================

/* 可选的首选编码（H.264 仅在编译开启时加入） */
static const int kEncodingIds[] = {
  rfb::encodingTight,
  rfb::encodingJPEG,
  rfb::encodingZRLE,
  rfb::encodingHextile,
#ifdef HAVE_H264
  rfb::encodingH264,
#endif
  rfb::encodingRaw,
};
static const size_t kEncodingCount =
    sizeof(kEncodingIds) / sizeof(kEncodingIds[0]);

/* 分类页内的说明小标题 */
static Fl_Box *optCaption(int x, int y, int w, const char *text)
{
  Fl_Box *b = new Fl_Box(x, y, w, 18, text);
  b->labelsize(11);
  b->labelcolor(mat::Text3);
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
  b->labelcolor(mat::Text);
  b->clear_visible_focus();
  return b;
}

/* 分类页内的下拉框 */
static Fl_Choice *optChoice(int x, int y, int w, int h)
{
  Fl_Choice *c = new Fl_Choice(x, y, w, h);
  c->box(THEME_UP_BOX);
  c->color(mat::Card);
  c->textsize(12);
  c->textcolor(mat::Text);
  c->clear_visible_focus();
  return c;
}

/* 分类页内的数字输入框 */
static Fl_Int_Input *optSpin(int x, int y, int w, int h)
{
  Fl_Int_Input *i = new Fl_Int_Input(x, y, w, h);
  i->box(THEME_UP_BOX);
  i->color(mat::Card);
  i->textsize(12);
  i->textcolor(mat::Text);
  return i;
}

void ServerDialog::buildOptionsPanel(Fl_Group *g)
{
  buildPanelHeader(g, "选项设置", onClosePanel, this);

  const int PAD = 16;
  const int cw = g->w() - PAD * 2;
  const int TAB_Y = 38;
  const int TAB_H = 26;

  // ---- 6 个分类子标签（互斥切换）----
  const char *tabNames[6] = {"压缩", "安全", "输入", "键盘快捷键", "显示", "杂项"};
  const int tw = cw / 6;
  for (int i = 0; i < 6; i++) {
    Fl_Toggle_Button *t = new Fl_Toggle_Button(PAD + i * tw, TAB_Y, tw - 2,
                                               TAB_H, tabNames[i]);
    t->type(FL_RADIO_BUTTON);   // 同一父组内自动互斥
    t->box(THEME_UP_BOX);
    t->labelsize(11);
    t->labelcolor(mat::Text2);
    t->clear_visible_focus();
    t->user_data((void*)(intptr_t)i);
    t->callback(onOptionTab, this);
    optTabs[i] = t;
  }

  // ---- 6 个分类页（同一时刻只显示一个）----
  const int PANE_Y = TAB_Y + TAB_H + 12;
  for (int i = 0; i < 6; i++) {
    Fl_Group *p = new Fl_Group(PAD, PANE_Y, cw, 380);
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
  for (size_t i = 0; i < kEncodingCount; i++)
    optEncoding->add(rfb::encodingName(kEncodingIds[i]));

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
  // 两个开关相互独立，默认都不启用；用三项下拉避免把 false/false 误写成 true
  optMouseMode->add("默认（都不启用）");
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
    b->labelcolor(mat::Text);
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
  text->labelcolor(mat::Text2);
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
  int ei = 0;
  for (size_t i = 0; i < kEncodingCount; i++) {
    if (preferredEncoding == rfb::encodingName(kEncodingIds[i])) {
      ei = (int)i;
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
  // 默认是「全部类型」列表：按设计稿显示「无 + 标准 VNC」
  const bool allTypes = secTypes.find("VncAuth") != std::string::npos &&
                        secTypes.find("Plain") != std::string::npos &&
                        (secTypes.find("X509") != std::string::npos ||
                         secTypes.find("RA2") != std::string::npos);
  if (allTypes) {
    optEncryption->value(0);
    optAuth->value(1);
  } else {
    const bool rsa = secTypes.find("RA2") != std::string::npos ||
                     secTypes.find("TLS") != std::string::npos ||
                     secTypes.find("X509") != std::string::npos;
    optEncryption->value(rsa ? 1 : 0);
    if (secTypes.find("Plain") != std::string::npos ||
        secTypes.find("RA2_256") != std::string::npos)
      optAuth->value(2);
    else if (secTypes.find("VncAuth") != std::string::npos ||
             secTypes.find("RA2ne") == std::string::npos)
      optAuth->value(1);
    else
      optAuth->value(0);
  }
  loadedEncryption = optEncryption->value();
  loadedAuth = optAuth->value();
#else
  optEncryption->value(0);
  optAuth->value(1);
  loadedEncryption = 0;
  loadedAuth = 1;
#endif

  // 输入
  optViewOnly->value(viewOnly ? 1 : 0);
  // 两个开关各自独立：优先显示「模拟鼠标中键」，其次「显示本地光标」，都没有就是默认项
  optMouseMode->value(emulateMiddleButton ? 1 : (alwaysCursor ? 2 : 0));
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

  int ei = optEncoding->value();
  if (ei < 0 || (size_t)ei >= kEncodingCount)
    ei = 0;
  preferredEncoding.setParam(rfb::encodingName(kEncodingIds[ei]));

  switch (optColorLevel->value()) {
  case 1: fullColour.setParam(false); lowColourLevel.setParam(2); break;
  case 2: fullColour.setParam(false); lowColourLevel.setParam(1); break;
  case 3: fullColour.setParam(false); lowColourLevel.setParam(0); break;
  default: fullColour.setParam(true); break;
  }

  customCompressLevel.setParam(optCustomCompress->value());
  compressLevel.setParam(atoi(optCompressLevel->value()));
  qualityLevel.setParam(atoi(optQualityLevel->value()));

  // 安全：只有用户真的改过「加密/认证」下拉，才重写 secTypes；
  // 否则保留原有列表（避免开合面板把「全部类型」改写成单一类型导致连不上）。
#if defined(HAVE_GNUTLS) || defined(HAVE_NETTLE)
  if (optEncryption->value() != loadedEncryption ||
      optAuth->value() != loadedAuth) {
    std::string s;
    if (optEncryption->value() == 1) {
      // RSA-AES：按认证方式挑对应的安全类型（RA2ne=无认证，RA2=标准VNC，RA2_256=用户名密码）
      switch (optAuth->value()) {
      case 0:  s = "RA2ne,RA2ne_256"; break;
      case 1:  s = "RA2";             break;
      default: s = "RA2_256";         break;
      }
    } else {
      switch (optAuth->value()) {
      case 0:  s = "None";    break;
      case 1:  s = "VncAuth"; break;
      default: s = "Plain";   break;
      }
    }
    rfb::SecurityClient::secTypes.setParam(s.c_str());
    loadedEncryption = optEncryption->value();
    loadedAuth = optAuth->value();
  }
#endif

  viewOnly.setParam(optViewOnly->value());
  // 鼠标模式三项：0=都不启用，1=模拟鼠标中键，2=显示本地光标
  const int mouseMode = optMouseMode->value();
  emulateMiddleButton.setParam(mouseMode == 1);
  alwaysCursor.setParam(mouseMode == 2);
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
  uint64_t token;            // 创建时的实例 token
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
  if (t->token != 0 && t->token == g_currentToken)
    t->self->handleUpdateTask(t->ok, t->info, t->error, t->install);
  delete t;
}

/* 把 GitHub Release 的 Markdown 更新说明转成「[类型] 内容」列表。
 * 支持 Conventional Commits 前缀（feat/fix/perf/docs/refactor），
 * 其余行原样保留。 */
static std::string normalizeChangelog(const std::string &notes)
{
  static const struct {
    const char *prefix;
    const char *tag;
  } tags[] = {
    {"feat:",     "新功能"},
    {"fix:",      "修复"},
    {"perf:",     "优化"},
    {"docs:",     "文档"},
    {"refactor:", "重构"},
  };

  std::string out;
  size_t pos = 0;
  while (pos < notes.size()) {
    size_t end = notes.find('\n', pos);
    if (end == std::string::npos)
      end = notes.size();

    std::string line = notes.substr(pos, end - pos);
    pos = end + 1;

    // 去掉 Markdown 列表前缀（"- " / "* "）
    size_t start = line.find_first_not_of(" \t");
    if (start != std::string::npos && (line[start] == '-' || line[start] == '*'))
      start = line.find_first_not_of(" \t", start + 1);
    if (start == std::string::npos)
      continue;
    line = line.substr(start);
    if (line.empty())
      continue;

    // 按 Conventional Commits 前缀加中文标签
    const char *tag = nullptr;
    for (size_t i = 0; i < sizeof(tags) / sizeof(tags[0]); i++) {
      const size_t plen = strlen(tags[i].prefix);
      if (line.compare(0, plen, tags[i].prefix) == 0) {
        tag = tags[i].tag;
        line = line.substr(plen);
        break;
      }
    }

    // 去掉标签后的前导空格
    size_t s2 = line.find_first_not_of(' ');
    if (s2 != std::string::npos)
      line = line.substr(s2);

    if (tag != nullptr)
      out += std::string("[") + tag + "] " + line + "\n";
    else
      out += line + "\n";
  }
  return out;
}

/* 刷新更新面板的统计三行 */
void ServerDialog::setUpdateStats(const char *stage, const char *speed,
                                 const char *eta)
{
  // 必须用 copy_label：Fl_Widget::label() 只存指针不拷贝，
  // 传进来的都是栈上临时的 std::string，函数返回后就成了悬垂指针。
  updStage->copy_label(stage);
  updSpeed->copy_label(speed);
  updEta->copy_label(eta);
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

    updVersion->copy_label(("v" PACKAGE_VERSION "  →  v" + info.version).c_str());
    setUpdateStats("当前阶段：发现新版本");
    const std::string changelog = info.notes.empty()
                                  ? "（该版本未提供更新说明）"
                                  : normalizeChangelog(info.notes);
    updChangelog->copy_label(changelog.c_str());
    updButton->label("立即更新");
  } else {
    updLatestVersion.clear();
    updVersion->label("v" PACKAGE_VERSION "  ·  已是最新版本");
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
  runUpdateTask(false, updateTaskDone, "当前阶段：正在检查新版本...");
}

void ServerDialog::startUpdateInstall()
{
  if (updDownloadUrl.empty())
    return;
  runUpdateTask(true, updateTaskDone, "当前阶段：正在下载并安装...");
}

/* 启动后台更新任务：install=true 走「下载并静默安装」，
 * 否则只查询最新版本。done 为任务完成后在主线程执行的回调。
 * stage 非空时同时更新面板状态（静默检查传 nullptr）。 */
bool ServerDialog::runUpdateTask(bool install, void (*done)(void*),
                                 const char *stage)
{
  if (updBusy.exchange(true))
    return false;
  if (updThread.joinable())
    updThread.join();

  if (stage != nullptr) {
    updStage->label(stage);
    updButton->deactivate();
    panels[2]->redraw();
  }

  const std::string url = updDownloadUrl;
  const std::string checksum = updChecksumUrl;
  const std::string version = updLatestVersion;

  updThread = std::thread([this, install, done, url, checksum, version,
                           token = updToken]() {
    UpdateTask *t = new UpdateTask;
    t->self = this;
    t->token = token;
    t->install = install;
    if (install)
      t->ok = core::UpdateChecker::downloadAndInstall(url, checksum, version,
                                                      &t->error);
    else
      t->ok = core::UpdateChecker::fetchLatestRelease(&t->info, &t->error);
    // 实例已销毁（析构把 g_currentToken 清成 0）时不能再往主线程塞回调，
    // 否则 handleUpdateTask 会解引用已释放的 this。
    if (token != 0 && token == g_currentToken.load())
      Fl::awake(done, t);
    else
      delete t;
  });
  return true;
}

/* 主线程侧：启动静默检查完成，有新版则给「自动更新」入口加红点 */
void ServerDialog::updateSilentDone(void *ptr)
{
  UpdateTask *t = (UpdateTask*)ptr;
  if (t->token != 0 && t->token == g_currentToken) {
    t->self->updBusy = false;
    if (t->ok &&
        core::UpdateChecker::isNewer(t->info.version, PACKAGE_VERSION)) {
      t->self->updLatestVersion = t->info.version;
      t->self->updDownloadUrl = t->info.downloadUrl;
      t->self->updChecksumUrl = t->info.checksumUrl;
      if (t->self->entries_[2] != nullptr) {
        // 入口副标题显示新版本号，右上角亮红点
        const std::string sub = "有新版本 v" + t->info.version;
        t->self->entries_[2]->subtitle(sub.c_str());
        t->self->entries_[2]->badge(true);
      }
    }
    if (t->self->updThread.joinable())
      t->self->updThread.join();
  }
  delete t;
}

void ServerDialog::checkUpdateSilently()
{
  runUpdateTask(false, updateSilentDone, nullptr);
}

void ServerDialog::buildUpdatePanel(Fl_Group *g)
{
  buildPanelHeader(g, "自动更新", onClosePanel, this);

  const int PAD = 16;
  const int cw = g->w() - PAD * 2;

  // ---- 版本对比 ----
  updVersion = new Fl_Box(PAD, 38, cw, 26,
                          "v" PACKAGE_VERSION "  ·  点击下方按钮检查更新");
  updVersion->labelfont(FL_HELVETICA_BOLD);
  updVersion->labelsize(14);
  updVersion->labelcolor(mat::Text);
  updVersion->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

  // ---- 统计三行 ----
  int y = 74;
  const int statH = 22;
  updStage = new Fl_Box(PAD, y, cw, statH, "当前阶段：待检查");
  updStage->labelsize(11);
  updStage->labelcolor(mat::Text2);
  updStage->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  updSpeed = new Fl_Box(PAD, y + statH, cw, statH, "下载速度：—");
  updSpeed->labelsize(11);
  updSpeed->labelcolor(mat::Text2);
  updSpeed->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  updEta = new Fl_Box(PAD, y + statH * 2, cw, statH, "剩余时间：—");
  updEta->labelsize(11);
  updEta->labelcolor(mat::Text2);
  updEta->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  // ---- 更新说明 ----
  y += statH * 3 + 10;
  updChangelog = new Fl_Box(PAD, y, cw, 160, "尚未检查更新。");
  updChangelog->box(FL_BORDER_BOX);
  updChangelog->color(mat::Card);
  updChangelog->labelsize(11);
  updChangelog->labelcolor(mat::Text2);
  updChangelog->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_INSIDE |
                      FL_ALIGN_WRAP);

  // ---- 立即更新 ----
  y += 172;
  updButton =
      new MButton(PAD + cw / 2 - 70, y, 140, 32, "检查更新", MButton::Filled);
  updButton->radius(8);
  updButton->fontSize(12);
  updButton->callback(onUpdateButton, this);
}

void ServerDialog::buildAboutPanel(Fl_Group *g)
{
  buildPanelHeader(g, "关于", onClosePanel, this);

  const int PAD = 16;
  const int cw = g->w() - PAD * 2;

  // 版本
  Fl_Box *ver = new Fl_Box(PAD, 42, cw, 26,
                           "TuPigVNC  v" PACKAGE_VERSION " · x64");
  ver->labelfont(FL_HELVETICA_BOLD);
  ver->labelsize(14);
  ver->labelcolor(mat::Text);
  ver->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE);

  // 简介
  Fl_Box *desc = new Fl_Box(PAD, 74, cw, 40,
                            "跨平台 VNC 远程桌面客户端\n"
                            "基于 TigerVNC，Material 3 极简单页界面");
  desc->labelsize(11);
  desc->labelcolor(mat::Text2);
  desc->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);

  // 链接按钮
  const char *links[4] = {"项目主页", "问题反馈", "版本发布", "许可证"};
  int bw = 92, gap = 6;
  int total = 4 * bw + 3 * gap;
  int bx = PAD + (cw - total) / 2;
  for (int i = 0; i < 4; i++) {
    MButton *b = new MButton(bx + i * (bw + gap), 122, bw, 28, links[i],
                             MButton::Ghost);
    b->radius(7);
    b->fontSize(11);
  }
}

/* 把「最后使用时间」转成中文相对时间 */
static std::string humanAgo(int64_t lastUsed)
{
  if (lastUsed <= 0)
    return "从未";
  const int64_t diff = (int64_t)time(nullptr) - lastUsed;
  if (diff < 60)
    return "刚刚";
  if (diff < 3600)
    return std::to_string(diff / 60) + " 分钟前";
  if (diff < 86400)
    return std::to_string(diff / 3600) + " 小时前";
  if (diff < 86400 * 7)
    return std::to_string(diff / 86400) + " 天前";
  return std::to_string(diff / (86400 * 7)) + " 周前";
}

void ServerDialog::refreshHistory()
{
  if (historyBrowser == nullptr)
    return;
  historyBrowser->clear();
  for (const ServerHistoryEntry& e : serverHistory) {
    char line[512];
    snprintf(line, sizeof(line), "%s\t%s\t%d\t%s", e.host.c_str(),
             e.name.c_str(), e.useCount, humanAgo(e.lastUsed).c_str());
    historyBrowser->add(line);
  }

  // 列表高度随记录数变化（最多 12 行），否则只有 3 条时底部空一大块。
  // 注意：不能靠 fitPanelHeight 遍历子控件来算，Fl_Group 重排后坐标会失真，
  // 这里按 buildHistoryPanel 的固定布局直接算。
  const int rows = std::max<int>(1, (int)serverHistory.size());
  const int listH = std::min(12, rows) * 22 + 8;
  historyBrowser->resize(16, 62, historyBrowser->w(), listH);
  if (panels[0] != nullptr) {
    panels[0]->size(panels[0]->w(), 62 + listH + 16);
    if (activePanel == 0)
      applyWindowHeight();
  }

  refreshRecentChips();
}

void ServerDialog::refreshRecentChips()
{
  const int chipX0 = 20 + 34 + 6;  // PAD + 「最近」标签 + 间距
  const int chipGap = 8;
  int i = 0;

  // 按可用宽度平均分配，避免每个 chip 各自撑到 200px 后横向溢出
  const int count = std::min<int>(3, (int)serverHistory.size());
  const int availW = w() - chipX0 - 20;
  int chipW = 60;
  if (count > 0)
    chipW = (availW - (count - 1) * chipGap) / count;
  if (chipW > 200)
    chipW = 200;
  if (chipW < 60)
    chipW = 60;

  int chipX = chipX0;
  for (const ServerHistoryEntry& e : serverHistory) {
    if (i >= 3)
      break;
    char label[192];
    if (!e.name.empty() && e.name != e.host)
      snprintf(label, sizeof(label), "%s · %s", e.host.c_str(),
               e.name.c_str());
    else
      snprintf(label, sizeof(label), "%s", e.host.c_str());

    recentChips[i]->setLabel(label);
    recentChips[i]->resize(chipX, recentChips[i]->y(), chipW, 24);
    recentChips[i]->show();
    chipX += chipW + chipGap;
    i++;
  }
  for (; i < 3; i++)
    recentChips[i]->hide();

  // 「连接历史」入口副标题显示台数（设计稿：5 台 · 上限 250）
  if (entries_[0] != nullptr) {
    char sub[64];
    snprintf(sub, sizeof(sub), "%d 台 · 上限 250",
             (int)serverHistory.size());
    entries_[0]->subtitle(sub);
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

  // 收起/展开时窗口高度跟着变，避免底部大片空白
  applyWindowHeight();

  if (idx >= 0)
    scroll->scroll_to(0, panelTopY);

  scroll->redraw();
  redraw();
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

void ServerDialog::onThemeClicked(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const int idx = (int)(intptr_t)widget->user_data();
  // 目前仅实现 Material 主题，Linear / Arc 为占位。
  // 未实现的主题不能真的点亮选中态，否则提示框关掉后界面状态是假的。
  if (idx == 0) {
    for (int i = 0; i < 3; i++)
      dialog->themeBtns[i]->setActive(i == 0);
    return;
  }
  fl_message("「%s」主题正在开发中，当前为 Material 主题。",
             idx == 1 ? "Linear" : "Arc");
  // 提示完把选中态还原成 Material
  for (int i = 0; i < 3; i++)
    dialog->themeBtns[i]->setActive(i == 0);
}

void ServerDialog::onRecentChipClicked(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  const int idx = (int)(intptr_t)widget->user_data();
  if (idx < 0 || (size_t)idx >= dialog->serverHistory.size())
    return;
  auto it = dialog->serverHistory.begin();
  std::advance(it, idx);
  dialog->serverName->value(it->host.c_str());
  handleConnect(nullptr, dialog);
}

void ServerDialog::onHistoryDoubleClick(Fl_Widget *widget, void *data)
{
  ServerDialog *dialog = (ServerDialog*)data;
  Fl_Browser *b = (Fl_Browser*)widget;
  const int line = b->value();  // Fl_Browser 是 1 基
  if (line < 1 || (size_t)line > dialog->serverHistory.size())
    return;

  auto it = dialog->serverHistory.begin();
  std::advance(it, line - 1);

  if (Fl::event_button() == FL_RIGHT_MOUSE) {
    // 右键：删除该条历史
    dialog->serverHistory.erase(it);
    try {
      dialog->saveServerHistory();
    } catch (std::exception &e) {
      vlog.error(_("无法保存服务器历史：%s"), e.what());
    }
    dialog->refreshHistory();
    return;
  }

  if (Fl::event_clicks() > 0) {
    // 双击：填入地址并连接
    dialog->serverName->value(it->host.c_str());
    handleConnect(nullptr, dialog);
  }
}


ServerDialog::~ServerDialog()
{
  // 先把 token 清零，后台线程回调看到 token 不匹配就不会再碰 this
  g_currentToken = 0;
  Fl::remove_timeout(startupCheck, this);
  // 不能 join：WinHTTP 请求没设超时，网络慢时关窗会卡死几十秒。
  // worker 内部用 token 判定实例是否存活，detach 是安全的。
  if (updThread.joinable())
    updThread.detach();
}


std::string ServerDialog::run(const char* servername)
{
  ServerDialog dialog;

  dialog.serverName->value(servername);

  dialog.show();

  try {
    dialog.loadServerHistory();
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
    // 载入配置后立即刷新选项面板，避免之后开合面板把旧值覆盖回去
    dialog->loadOptions();
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
    // 先把选项面板里改动写回全局参数，否则另存的是旧值
    // （saveViewerParameters 只读全局参数，不会去看界面）
    dialog->storeOptions();
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

  // 空地址直接返回，否则会把 host="" 写进历史、run() 返回空串让程序退出
  if (servername == nullptr || servername[0] == '\0') {
    fl_message("请输入服务器地址，例如 192.168.1.100:5900");
    dialog->serverName->take_focus();
    return;
  }

  // 先把选项面板里改过的设置写回全局参数，否则直接连接会丢设置
  dialog->storeOptions();

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

namespace {

#ifdef WIN32
/* 跨进程命名互斥量：串行化多个实例对 history.json 的「读-改-写」，
 * 避免后写的实例覆盖先写的实例。Windows 互斥量可重入，同一线程
 * 重复获取不会死锁（loadServerHistory 内会调用 saveServerHistory）。 */
class HistoryLock {
public:
  HistoryLock()
  {
    mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\TuPigVNC-history");
    if (mutex_ == nullptr) {
      vlog.error(_("创建历史文件互斥量失败，错误码 %lu"), GetLastError());
      return;
    }
    DWORD ret = WaitForSingleObject(mutex_, INFINITE);
    if (ret != WAIT_OBJECT_0 && ret != WAIT_ABANDONED) {
      vlog.error(_("等待历史文件互斥量失败，错误码 %lu"), GetLastError());
      CloseHandle(mutex_);
      mutex_ = nullptr;
    }
  }

  ~HistoryLock()
  {
    if (mutex_ != nullptr) {
      ReleaseMutex(mutex_);
      CloseHandle(mutex_);
    }
  }

  bool locked() const { return mutex_ != nullptr; }

private:
  HANDLE mutex_ = nullptr;
};
#else
/* POSIX 下 rename() 本身即原子操作，无需额外加锁 */
class HistoryLock {
public:
  bool locked() const { return true; }
};
#endif

#ifdef WIN32
/* UTF-8 路径转宽字符，供 MoveFileExW 正确处理中文与长路径 */
std::wstring utf8ToWidePath(const std::string& path)
{
  int len = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
  if (len <= 0)
    return std::wstring();
  std::wstring wide(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], len);
  wide.resize(len - 1);  // 去掉末尾 NUL
  return wide;
}
#endif

/* 原子写入：先写同目录临时文件并落盘，再重命名覆盖目标文件。
 * 任一步失败都删除临时文件并保持原文件不变；成功返回 true。 */
bool writeFileAtomic(const std::string& path, const std::string& content,
                     std::string& error)
{
  const std::string tmppath = path + ".tmp";

  FILE* f = fopen(tmppath.c_str(), "wb");
  if (f == nullptr) {
    error = core::posix_error(
        core::format(_("无法创建临时文件 \"%s\""), tmppath.c_str()), errno).what();
    return false;
  }

  bool ok = true;
  if (fwrite(content.data(), 1, content.size(), f) != content.size()) {
    error = core::posix_error(
        core::format(_("写入临时文件 \"%s\" 失败"), tmppath.c_str()), errno).what();
    ok = false;
  }

  if (ok && fflush(f) != 0) {
    error = core::posix_error(
        core::format(_("刷新临时文件 \"%s\" 失败"), tmppath.c_str()), errno).what();
    ok = false;
  }

#ifdef WIN32
  if (ok) {
    DWORD err = 0;
    HANDLE h = (HANDLE)_get_osfhandle(_fileno(f));
    if (h == INVALID_HANDLE_VALUE)
      err = ERROR_INVALID_HANDLE;
    else if (!FlushFileBuffers(h))
      err = GetLastError();
    if (err != 0) {
      error = core::win32_error(
          core::format(_("落盘临时文件 \"%s\" 失败"), tmppath.c_str()), err).what();
      ok = false;
    }
  }
#else
  if (ok && fsync(fileno(f)) != 0) {
    error = core::posix_error(
        core::format(_("落盘临时文件 \"%s\" 失败"), tmppath.c_str()), errno).what();
    ok = false;
  }
#endif

  if (fclose(f) != 0 && ok) {
    error = core::posix_error(
        core::format(_("关闭临时文件 \"%s\" 失败"), tmppath.c_str()), errno).what();
    ok = false;
  }

  if (!ok) {
    remove(tmppath.c_str());
    return false;
  }

#ifdef WIN32
  std::wstring wtmp = utf8ToWidePath(tmppath);
  std::wstring wpath = utf8ToWidePath(path);
  if (wtmp.empty() || wpath.empty()) {
    error = _("历史文件路径包含无法转换的字符");
    remove(tmppath.c_str());
    return false;
  }
  if (!MoveFileExW(wtmp.c_str(), wpath.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DWORD err = GetLastError();
    error = core::win32_error(
        core::format(_("替换 \"%s\" 失败"), path.c_str()), err).what();
    remove(tmppath.c_str());
    return false;
  }
#else
  if (rename(tmppath.c_str(), path.c_str()) != 0) {
    error = core::posix_error(
        core::format(_("替换 \"%s\" 失败"), path.c_str()), errno).what();
    remove(tmppath.c_str());
    return false;
  }
#endif

  return true;
}

/* 历史文件完整路径；状态目录不可用时返回空串 */
std::string serverHistoryFilePath()
{
  const char* stateDir = core::getvncstatedir();
  if (stateDir == nullptr)
    return std::string();
  return std::string(stateDir) + "/" + SERVER_HISTORY_JSON;
}

}  // namespace

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

  const std::string filepath = serverHistoryFilePath();
  if (filepath.empty())
    return;

  HistoryLock lock;
  if (!lock.locked())
    vlog.error(_("无法获取历史文件锁，将在无锁状态下读取历史"));

  FILE* f = fopen(filepath.c_str(), "r");
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
  const std::string filepath = serverHistoryFilePath();
  if (filepath.empty())
    throw std::runtime_error(_("Could not determine VNC state directory path"));

  HistoryLock lock;
  if (!lock.locked())
    throw std::runtime_error(_("无法获取历史文件锁，放弃保存"));

  std::string content = "[\n";
  bool first = true;
  for (const ServerHistoryEntry& e : serverHistory) {
    if (!first)
      content += ",\n";
    first = false;
    content += core::format("  {\"host\":\"%s\",\"name\":\"%s\","
                            "\"lastUsed\":%lld,\"useCount\":%d}",
                            jsonEscape(e.host).c_str(),
                            jsonEscape(e.name).c_str(),
                            (long long)e.lastUsed, e.useCount);
  }
  content += "\n]\n";

  std::string error;
  if (!writeFileAtomic(filepath, content, error)) {
    vlog.error(_("保存服务器历史失败：%s"), error.c_str());
    throw std::runtime_error(error);
  }
}

void ServerDialog::updateUsedDir(const char* filename)
{
  char * name = strdup(filename);
  usedDir = dirname(name);
  free(name);
}
