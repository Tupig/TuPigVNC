/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
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

#ifndef __SERVERDIALOG_H__
#define __SERVERDIALOG_H__

#include <FL/Fl_Window.H>
#include <string>
#include <list>
#include <cstdint>

#include "fltk/Fl_Suggestion_Input.h"

/* 一条连接历史记录（存 User/history.json） */
struct ServerHistoryEntry {
  std::string host;     // 服务器地址，如 "192.168.1.100:5900"
  std::string name;     // 显示名，默认同 host
  int64_t lastUsed;     // 最后使用时间（Unix 秒）
  int useCount;         // 使用次数
};

class Fl_Widget;
class Fl_Group;
class Fl_Scroll;
class Fl_Input_Choice;

class ServerDialog : public Fl_Window {
protected:
  ServerDialog();
  ~ServerDialog();

public:
  static std::string run(const char* servername);

protected:
  static void handleOptions(Fl_Widget *widget, void *data);
  static void handleLoad(Fl_Widget *widget, void *data);
  static void handleSaveAs(Fl_Widget *widget, void *data);
  static void handleAbout(Fl_Widget *widget, void *data);
  static void handleCancel(Fl_Widget *widget, void *data);
  static void handleConnect(Fl_Widget *widget, void *data);

private:
  void loadServerHistory();
  void saveServerHistory();
  void updateUsedDir(const char* filename);
  void pruneHistory();                        // 超过 250 台时淘汰
  void migrateOldHistory();                   // 从旧版（注册表/文件）迁移
  void parseHistoryJson(const std::string& content);

  static void onServerHistoryRemove(Fl_Widget*, std::string s, void* data);
  static std::string serverHistoryNormalize(const std::string s);

  // ---- 极简单页布局（Material 3）----
  void buildLayout();          // 构建整个单页界面
  void buildHistoryPanel(Fl_Group* g);  // 连接历史面板
  void buildOptionsPanel(Fl_Group* g);  // 选项设置面板
  void buildUpdatePanel(Fl_Group* g);   // 自动更新面板
  void buildAboutPanel(Fl_Group* g);    // 关于面板
  void showPanel(int idx);     // 展开/收起面板：-1 全部收起
  void repositionWidgets();    // 面板显隐后重排
  static void onEntryClicked(Fl_Widget*, void* data);
  static void onClosePanel(Fl_Widget*, void* data);

protected:
  Fl_Suggestion_Input *serverName;
  std::list<ServerHistoryEntry> serverHistory;
  std::string usedDir;

  Fl_Scroll *scroll;           // 主滚动容器
  Fl_Group *panels[4];         // 0历史 1选项 2更新 3关于
  Fl_Widget *entries_[4];      // 2×2 功能入口
  int activePanel;             // 当前展开的面板，-1 表示收起
  int panelTopY;               // 面板起始 y（entries 下方）
};

#endif
