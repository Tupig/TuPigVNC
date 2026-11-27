/*
 * TuPigVNC · Material 3 自绘控件库
 *
 * FLTK 1.3 原生控件是灰色立体风格，与设计稿 material.html 的
 * 扁平圆角、紫色强调、图标卡片风差距很大。本文件提供一组自绘
 * 控件，把设计稿的视觉语言 1:1 落到 FLTK 上。
 *
 * 所有颜色、圆角、字号集中在这里定义，改主题只改一处。
 */

#ifndef __MATERIAL_H__
#define __MATERIAL_H__

#include <FL/Fl.H>
#include <FL/Fl_Group.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Widget.H>

#include <cstring>

// ============================================================
// 调色板（对齐 material.html 的 :root 变量）
// ============================================================
namespace mat {

extern Fl_Color Card;        // --card       #FFFFFF
extern Fl_Color Bg;          // --bg         #FBFAFF
extern Fl_Color CardHover;   // --card-hover #F7F4FF
extern Fl_Color Text;        // --text       #1A1526
extern Fl_Color Text2;       // --text-2     #6B6580
extern Fl_Color Text3;       // --text-3     #A29BB8
extern Fl_Color Accent;      // --accent     #6C5CE7
extern Fl_Color AccentSoft;  // --accent-soft#EFEBFF
extern Fl_Color AccentHover; // --accent-hover #5B4DD6
extern Fl_Color Stroke;      // --stroke     #EDE8FA
extern Fl_Color Stroke2;     // --stroke-2   #E0D9F2
extern Fl_Color Ok;          // --ok         #2E9E5B
extern Fl_Color OkSoft;      // --ok-soft    #DCF6E5
extern Fl_Color Err;         // --err        #E5484D
extern Fl_Color ErrSoft;     // --err-soft   #FFE8EA

/* 初始化调色板（须在 Fl::scheme 之后调用） */
void initColors();

/* 把 Fl_Color 按比例压暗/提亮（t>1 提亮，t<1 压暗） */
Fl_Color shade(Fl_Color c, double t);

// ============================================================
// 绘图基元
// ============================================================

/* 实心圆角矩形 */
void roundRectFill(int x, int y, int w, int h, int r, Fl_Color c);

/* 圆角矩形描边（1px） */
void roundRectOutline(int x, int y, int w, int h, int r, Fl_Color c);

/* 实心圆角矩形 + 1px 描边 */
void roundRect(int x, int y, int w, int h, int r, Fl_Color fill,
               Fl_Color stroke);

/* 在 w×h 区域内居中绘制单行文字，返回实际宽度 */
int textCentered(const char *s, int x, int y, int w, int h, Fl_Color c,
                 int size, Fl_Font face = FL_HELVETICA);

}  // namespace mat

// ============================================================
// 按钮：实心 / 描边 / 主题 / 分类标签
// ============================================================
class MButton : public Fl_Widget {
 public:
  enum Style {
    Filled,  // 实心紫底白字（主按钮「连接」）
    Ghost,   // 白底描边灰字（次要按钮）
    Theme,   // 顶栏主题切换（小圆角胶囊）
    Tab,     // 选项分类标签
  };

  MButton(int x, int y, int w, int h, const char *label, Style s = Filled);

  void draw() override;
  int handle(int event) override;

  void style(Style s) { style_ = s; redraw(); }
  void radius(int r) { radius_ = r; redraw(); }
  void setActive(bool a) { active_ = a; redraw(); }
  /* 是否处于「选中」状态（注意：不是 Fl_Widget::active() 的启用状态，
     故改名 selected 以免隐藏基类的 active()） */
  bool selected() const { return active_; }
  void fontSize(int s) { fontSize_ = s; redraw(); }
  void alignLeft() { alignLeft_ = true; redraw(); }

 private:
  Style style_;
  int radius_;
  int fontSize_;
  bool active_;
  bool alignLeft_;
  bool hover_;
  bool down_;
};

// ============================================================
// 功能入口卡片（图标盒 + 标题 + 副标题 + 可选右下红点）
// ============================================================
class MEntryCard : public Fl_Widget {
 public:
  enum Icon { History, Options, Update, About };

  MEntryCard(int x, int y, int w, int h, const char *title,
             const char *subtitle, Icon icon);

  void draw() override;
  int handle(int event) override;

  /* 更新副标题（如「有新版本 v0.0.3」） */
  void subtitle(const char *s);
  /* 右上角红点开关 */
  void badge(bool on) { badge_ = on; redraw(); }

 private:
  void drawIcon(int cx, int cy, int size);

  char title_[48];
  char subtitle_[64];
  Icon icon_;
  bool hover_;
  bool down_;
  bool badge_;
};

// ============================================================
// 最近连接 chip（小胶囊：绿点 + 文字）
// ============================================================
class MChip : public Fl_Widget {
 public:
  MChip(int x, int y, int w, int h, const char *label);

  void draw() override;
  int handle(int event) override;

  void setLabel(const char *s);

 private:
  char label_[96];
  bool hover_;
  bool down_;
};

// ============================================================
// 小标签（紫底紫字的胶囊，用于「上限 250 台」这类附注）
// ============================================================
class MTag : public Fl_Widget {
 public:
  MTag(int x, int y, int w, int h, const char *label);

  void draw() override;
};

// ============================================================
// 圆角输入框（12px 圆角 + 焦点紫色光晕 + 占位符）
// ============================================================
class MInput : public Fl_Input {
 public:
  MInput(int x, int y, int w, int h, const char *label = nullptr);

  void draw() override;
  int handle(int event) override;

  void placeholder(const char *p) {
    // 必须拷贝：Fl_Widget::label() 式的「只存指针」在调用方传临时对象时会悬空
    if (p == nullptr) {
      placeholder_[0] = '\0';
    } else {
      strncpy(placeholder_, p, sizeof(placeholder_) - 1);
      placeholder_[sizeof(placeholder_) - 1] = '\0';
    }
    redraw();
  }

 private:
  char placeholder_[128];
  bool focus_;
};

// ============================================================
// 圆角面板（12px 圆角 + 描边 + 背景）
// ============================================================
class MPanel : public Fl_Group {
 public:
  MPanel(int x, int y, int w, int h);

  void draw() override;
};

// ============================================================
// 品牌 logo（accent 色显示器线性图标）
// ============================================================
class MLogo : public Fl_Widget {
 public:
  MLogo(int x, int y, int w, int h);

  void draw() override;
};

// ============================================================
// Material 3 风格消息框（替代 fl_alert / fl_message / fl_choice，
// 杜绝英文 + 90 年代灰底弹窗）
// ============================================================

// 单按钮「确定」，返回 0
int matAlert(const char *title, const char *message,
             bool withCancel = false);

// 只有确定按钮的消息框（对应 fl_message）
void matMessage(const char *title, const char *message);

// 确定/取消（对应 fl_choice 的二选一）。
// 返回 true = 点了确定。
bool matConfirm(const char *title, const char *message,
                const char *okText = "确定",
                const char *cancelText = "取消");

// 最多三个按钮（对应 fl_choice 的完整形式）。
// 返回被点按钮的下标；nullptr 表示该位置没有按钮。
int matChoice(const char *title, const char *message,
              const char *b0, const char *b1 = nullptr,
              const char *b2 = nullptr, const char *b3 = nullptr);

#endif  // __MATERIAL_H__
