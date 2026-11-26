/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
 * Copyright 2012 Samuel Mannehed <samuel@cendio.se> for Cendio AB
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 * USA.
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

// FIXME: Workaround for FLTK including windows.h
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

#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <network/TcpSocket.h>

#include "fltk/layout.h"
#include "fltk/util.h"
#include "fltk/theme.h"
#include "fltk/Fl_Suggestion_Input.h"
#include "ServerDialog.h"
#include "OptionsDialog.h"
#include "vncviewer.h"
#include "parameters.h"

static core::LogWriter vlog("ServerDialog");

const char* SERVER_HISTORY="tigervnc.history";        // 旧格式（仅用于迁移）
const char* SERVER_HISTORY_JSON="history.json";       // 新格式：User/history.json
static const size_t SERVER_HISTORY_MAX = 250;         // 历史上限

ServerDialog::ServerDialog()
  : Fl_Window(540, 760, "TuPigVNC"),
    scroll(nullptr), activePanel(-1), panelTopY(0)
{
  for (int i = 0; i < 4; i++) {
    panels[i] = nullptr;
    entries_[i] = nullptr;
  }

  buildLayout();
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
    panels[i] = new Fl_Group(PAD, panelTopY, cw, 240);
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

  /* Needed for resize to work sanely */
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

  Fl_Box *placeholder = new Fl_Box(0, 40, g->w(), 60,
                                   "（历史表格接入后显示：主机 / 名称 / 次数 / 最后连接 / 操作）");
  placeholder->labelsize(11);
  placeholder->labelcolor(M_TEXT3);
  placeholder->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
}

void ServerDialog::buildOptionsPanel(Fl_Group *g)
{
  buildPanelHeader(g, "选项设置", onClosePanel, this);

  Fl_Box *placeholder = new Fl_Box(0, 40, g->w(), 60,
                                   "（6 分类：压缩 / 安全 / 输入 / 键盘快捷键 / 显示 / 杂项）");
  placeholder->labelsize(11);
  placeholder->labelcolor(M_TEXT3);
  placeholder->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
}

void ServerDialog::buildUpdatePanel(Fl_Group *g)
{
  buildPanelHeader(g, "自动更新", onClosePanel, this);

  Fl_Box *placeholder = new Fl_Box(0, 40, g->w(), 60,
                                   "（版本对比 / 下载速度 / 进度条 / 规范化更新说明）");
  placeholder->labelsize(11);
  placeholder->labelcolor(M_TEXT3);
  placeholder->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
}

void ServerDialog::buildAboutPanel(Fl_Group *g)
{
  buildPanelHeader(g, "关于", onClosePanel, this);

  Fl_Box *placeholder = new Fl_Box(0, 40, g->w(), 60,
                                   "TuPigVNC\n跨平台 VNC 远程桌面客户端");
  placeholder->labelsize(12);
  placeholder->labelcolor(M_TEXT2);
  placeholder->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE | FL_ALIGN_WRAP);
}

void ServerDialog::showPanel(int idx)
{
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
  
  // Block until user picks something.
  while(file_chooser->shown())
    Fl::wait();
  
  // Did the user hit cancel?
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
    
    // Block until user picks something.
    while(file_chooser->shown())
      Fl::wait();
    
    // Did the user hit cancel?
    if (file_chooser->value() == nullptr) {
      delete(file_chooser);
      return;
    }
    
    filename = file_chooser->value();
    dialog->updateUsedDir(filename);
    
    FILE* f = fopen(filename, "r");
    if (f) {

      // The file already exists.
      fclose(f);
      int overwrite_choice = fl_choice(_("%s already exists. Do you want to overwrite?"), 
                                       nullptr, _("No"), _("Overwrite"), filename);
      if (overwrite_choice != 2) {

        // If the user doesn't want to overwrite:
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
  // Convert to lowercase for case-insensitity
  std::string result = s;
  transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}
