/* Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
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

#ifndef __SERVERDIALOG_H__
#define __SERVERDIALOG_H__

#include <FL/Fl_Window.H>
#include <string>
#include <list>
#include <cstdint>
#include <atomic>
#include <thread>

#include "fltk/material.h"

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
class Fl_Browser;
class Fl_Input_Choice;
class Fl_Check_Button;
class Fl_Int_Input;
class Fl_Choice;
class Fl_Toggle_Button;
class Fl_Box;
class Fl_Button;

namespace core { struct ReleaseInfo; }

class ServerDialog : public Fl_Window {
protected:
  ServerDialog();
  ~ServerDialog();

public:
  static std::string run(const char* servername,
                         const char* initialError = nullptr);

  // 显示时套用 Windows 11 系统圆角（对应设计稿的 20px 圆角卡片）
  void show() override;

protected:
  static void handleLoad(Fl_Widget *widget, void *data);
  static void handleSaveAs(Fl_Widget *widget, void *data);
  static void handleCancel(Fl_Widget *widget, void *data);
  static void handleConnect(Fl_Widget *widget, void *data);

private:
  void loadServerHistory();
  void saveServerHistory();
  void updateUsedDir(const char* filename);
  void pruneHistory();                        // 超过 250 台时淘汰
  void migrateOldHistory();                   // 从旧版（注册表/文件）迁移
  void parseHistoryJson(const std::string& content);

  // ---- 极简单页布局（Material 3）----
  void buildLayout();          // 构建整个单页界面
  void buildHistoryPanel(Fl_Group* g);  // 连接历史面板
  void buildOptionsPanel(Fl_Group* g);  // 选项设置面板
  void buildUpdatePanel(Fl_Group* g);   // 自动更新面板
  void buildAboutPanel(Fl_Group* g);    // 关于面板
  void showPanel(int idx);     // 展开/收起面板：-1 全部收起
  void refreshHistory();       // 根据 serverHistory 刷新历史列表
  void refreshRecentChips();   // 根据 serverHistory 刷新「最近连接」chip
  static void onEntryClicked(Fl_Widget*, void* data);
  static void onClosePanel(Fl_Widget*, void* data);
  static void onRecentChipClicked(Fl_Widget*, void* data);  // 点 chip 填入地址
  static void onHistoryDoubleClick(Fl_Widget*, void* data); // 双击历史即连接（右键删除）

  // ---- 选项面板：6 个分类页 ----
  void buildOptionCompress(Fl_Group* pane, int y);   // 压缩
  void buildOptionSecurity(Fl_Group* pane, int y);   // 安全
  void buildOptionInput(Fl_Group* pane, int y);      // 输入
  void buildOptionKeys(Fl_Group* pane, int y);       // 键盘快捷键
  void buildOptionDisplay(Fl_Group* pane, int y);    // 显示
  void buildOptionMisc(Fl_Group* pane, int y);       // 杂项
  void showOptionPane(int idx);        // 切换分类页
  void loadOptions();                  // 从全局参数读取初始值
  void storeOptions();                 // 写回全局参数
  static void onOptionTab(Fl_Widget*, void* data);

protected:
  MInput *serverName;
  Fl_Box *errorLabel;          // 输入框下方的行内错误提示（不弹窗）
  void showError(const char *msg);   // 显示行内错误
  void clearError();                 // 清除行内错误
  std::list<ServerHistoryEntry> serverHistory;
  std::string usedDir;

  Fl_Browser *historyBrowser;  // 连接历史列表
  Fl_Box *historyFoot;         // 历史面板底部提示（跟随列表高度）
  Fl_Group *panels[4];         // 0历史 1选项 2更新 3关于
  MEntryCard *entries_[4];     // 2×2 功能入口
  Fl_Widget *recentLabel;      // 「最近」标签
  MChip *recentChips[3];       // 最近连接 chip（最多 3 个）
  int activePanel;             // 当前展开的面板，-1 表示收起
  int panelTopY;               // 面板起始 y（entries 下方）
  int topbarH;                 // 顶栏高度（窗口高度自适应时要用）
  void applyWindowHeight();    // 按内容/面板高度调整窗口高度
  void fitPanelHeight(int idx);// 按子控件底部收窄指定面板高度

  // 选项面板：6 个分类页 + 子标签
  Fl_Group *optPanes[6];
  Fl_Toggle_Button *optTabs[6];

  // 压缩页
  Fl_Check_Button *optAutoSelect;
  Fl_Check_Button *optJpeg;
  Fl_Choice *optEncoding;
  Fl_Choice *optColorLevel;
  Fl_Check_Button *optCustomCompress;
  Fl_Int_Input *optCompressLevel;
  Fl_Int_Input *optQualityLevel;

  // 安全页
  Fl_Choice *optEncryption;
  Fl_Choice *optAuth;
  int loadedEncryption = 0;    // loadOptions 时载入的加密下拉值（判定是否改过）
  int loadedAuth = 1;          // loadOptions 时载入的认证下拉值

  // 输入页
  Fl_Check_Button *optViewOnly;
  Fl_Choice *optMouseMode;
  Fl_Choice *optCursorType;
  Fl_Check_Button *optSystemKeys;
  Fl_Check_Button *optAcceptClip;
  Fl_Check_Button *optSendClip;

  // 键盘快捷键页
  Fl_Toggle_Button *optMod[4];

  // 显示页
  Fl_Choice *optDisplayMode;

  // 杂项页
  Fl_Check_Button *optShared;
  Fl_Check_Button *optReconnect;
  Fl_Check_Button *optAudio;

  // 自动更新页
  static void onUpdateButton(Fl_Widget*, void* data);
  static void updateTaskDone(void* ptr); // 后台任务完成（主线程回调）
  static void updateSilentDone(void* ptr); // 启动静默检查完成
  void handleUpdateTask(bool ok, const core::ReleaseInfo& info,
                        const std::string& error, bool install);
  void setUpdateStats(const char* stage,
                      const char* speed = "下载速度：—",
                      const char* eta = "剩余时间：—");   // 刷新统计三行
  void startUpdateCheck();               // 后台查询最新版本
  void startUpdateInstall();             // 后台下载并静默安装
  bool runUpdateTask(bool install, void (*done)(void*),
                     const char* stage);  // 后台任务统一入口
  void checkUpdateSilently();            // 启动时静默检查（有新版加红点）
  static void startupCheck(void* data);  // 启动检查定时器回调

  Fl_Box *updVersion;                    // 版本对比行
  Fl_Box *updStage;                      // 当前阶段
  Fl_Box *updSpeed;                      // 下载速度
  Fl_Box *updEta;                        // 剩余时间
  Fl_Box *updChangelog;                  // 更新说明
  MButton *updButton;                    // 立即更新

  std::string updLatestVersion;
  std::string updDownloadUrl;
  std::string updChecksumUrl;
  uint64_t updToken = 0;                 // 实例 token（后台回调存活判定）
  std::atomic<bool> updBusy{false};      // 后台任务进行中
  std::thread updThread;                 // 后台线程句柄
};

#endif
