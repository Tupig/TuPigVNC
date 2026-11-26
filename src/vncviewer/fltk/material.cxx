/*
 * TuPigVNC · Material 3 自绘控件库实现
 */

#include "material.h"

#include <FL/fl_draw.H>

#include <math.h>
#include <string.h>

namespace mat {

// ---- 调色板（初始为默认值，initColors 会按需覆盖）----
Fl_Color Card = 0xFFFFFF00;
Fl_Color Bg = 0xFBFAFF00;
Fl_Color CardHover = 0xF7F4FF00;
Fl_Color Text = 0x1A152600;
Fl_Color Text2 = 0x6B658000;
Fl_Color Text3 = 0xA29BB800;
Fl_Color Accent = 0x6C5CE700;
Fl_Color AccentSoft = 0xEFEBFF00;
Fl_Color AccentHover = 0x5B4DD600;
Fl_Color Stroke = 0xEDE8FA00;
Fl_Color Stroke2 = 0xE0D9F200;
Fl_Color Ok = 0x2E9E5B00;
Fl_Color OkSoft = 0xDCF6E500;
Fl_Color Err = 0xE5484D00;
Fl_Color ErrSoft = 0xFFE8EA00;

void initColors() {
  Card = fl_rgb_color(255, 255, 255);
  Bg = fl_rgb_color(251, 250, 255);
  CardHover = fl_rgb_color(247, 244, 255);
  Text = fl_rgb_color(26, 21, 38);
  Text2 = fl_rgb_color(107, 101, 128);
  Text3 = fl_rgb_color(162, 155, 184);
  Accent = fl_rgb_color(108, 92, 231);
  AccentSoft = fl_rgb_color(239, 235, 255);
  AccentHover = fl_rgb_color(91, 77, 214);
  Stroke = fl_rgb_color(237, 232, 250);
  Stroke2 = fl_rgb_color(224, 217, 242);
  Ok = fl_rgb_color(46, 158, 91);
  OkSoft = fl_rgb_color(220, 246, 229);
  Err = fl_rgb_color(229, 72, 77);
  ErrSoft = fl_rgb_color(255, 232, 234);
}

Fl_Color shade(Fl_Color c, double t) {
  // fl_color_average(c1, c2, w) = c1*w + c2*(1-w)，c1 才拿 w 权重。
  // 想「把 c 压暗到 t 倍」必须让 c 当 c1，写反了会得到 (1-t) 倍亮度。
  if (t <= 1.0) {
    if (t < 0.0)
      t = 0.0;
    return fl_color_average(c, FL_BLACK, (float)t);
  }
  // t>1 表示提亮：t=1.5 -> 50% 白，t=2 -> 纯白
  double w = 2.0 - t;
  if (w < 0.0)
    w = 0.0;
  return fl_color_average(c, FL_WHITE, (float)w);
}

// ============================================================
// 绘图基元
// ============================================================

void roundRectFill(int x, int y, int w, int h, int r, Fl_Color c) {
  if (w <= 0 || h <= 0)
    return;
  if (r > h / 2)
    r = h / 2;
  if (r > w / 2)
    r = w / 2;
  fl_color(c);
  if (r <= 0) {
    fl_rectf(x, y, w, h);
    return;
  }
  // 中间矩形
  fl_rectf(x, y + r, w, h - 2 * r);
  // 上下两个半圆角：逐行算内缩，像素级准确
  for (int i = 0; i < r; i++) {
    const double dy = (double)(r - i);
    const double dx = r - sqrt((double)(r * r) - dy * dy);
    const int inset = (int)(dx + 0.5);
    // 极窄控件（w 接近 2r）时两端会交叉，fl_xyline 端点反了会多画像素
    if (x + inset > x + w - inset - 1)
      continue;
    fl_xyline(x + inset, y + i, x + w - inset - 1);
    fl_xyline(x + inset, y + h - 1 - i, x + w - inset - 1);
  }
}

void roundRectOutline(int x, int y, int w, int h, int r, Fl_Color c) {
  if (w <= 0 || h <= 0)
    return;
  if (r > h / 2)
    r = h / 2;
  if (r > w / 2)
    r = w / 2;
  fl_color(c);
  if (r <= 0) {
    fl_rect(x, y, w, h);
    return;
  }
  if (x + r <= x + w - r - 1) {
    fl_xyline(x + r, y, x + w - r - 1);
    fl_xyline(x + r, y + h - 1, x + w - r - 1);
  }
  if (y + r <= y + h - r - 1) {
    fl_yxline(x, y + r, y + h - r - 1);
    fl_yxline(x + w - 1, y + r, y + h - r - 1);
  }
  // 右/下三个角的 bbox 各减 1：fl_arc 用 bbox 中心 + 半径算点，
  // 不减 1 的话最右/最下点会落在 x+w / y+h，比直边（x+w-1 / y+h-1）外凸 1px。
  fl_arc(x, y, r * 2, r * 2, 90, 180);
  fl_arc(x + w - r * 2 - 1, y, r * 2, r * 2, 0, 90);
  fl_arc(x, y + h - r * 2 - 1, r * 2, r * 2, 180, 270);
  fl_arc(x + w - r * 2 - 1, y + h - r * 2 - 1, r * 2, r * 2, 270, 360);
}

void roundRect(int x, int y, int w, int h, int r, Fl_Color fill,
               Fl_Color stroke) {
  roundRectFill(x, y, w, h, r, fill);
  roundRectOutline(x, y, w, h, r, stroke);
}

int textCentered(const char *s, int x, int y, int w, int h, Fl_Color c,
                 int size, Fl_Font face) {
  if (s == nullptr || s[0] == '\0')
    return 0;
  fl_color(c);
  fl_font(face, size);
  int tw = 0, th = 0;
  fl_measure(s, tw, th, 0);
  // 交给对齐重载算基线：fl_measure 的 th 是字体总高（ascent+descent），
  // 手算时减 th 会把文字整体抬高一个 ascent。
  fl_draw(s, x, y, w, h, FL_ALIGN_CENTER);
  return tw;
}

}  // namespace mat

// ============================================================
// MButton
// ============================================================

MButton::MButton(int x, int y, int w, int h, const char *label, Style s)
    : Fl_Widget(x, y, w, h, label),
      style_(s),
      radius_(12),
      fontSize_(13),
      active_(false),
      alignLeft_(false),
      hover_(false),
      down_(false) {}

void MButton::draw() {
  Fl_Color bg = mat::Card;
  Fl_Color fg = mat::Text;
  Fl_Color stroke = mat::Stroke2;

  switch (style_) {
    case Filled:
      bg = down_ ? mat::shade(mat::Accent, 0.75)
                 : (hover_ ? mat::AccentHover : mat::Accent);
      fg = FL_WHITE;
      stroke = bg;
      break;
    case Ghost:
      bg = hover_ ? mat::AccentSoft : mat::Card;
      fg = hover_ ? mat::Accent : mat::Text2;
      stroke = hover_ ? mat::Accent : mat::Stroke2;
      break;
    case Theme:
      bg = (active_ || hover_) ? mat::AccentSoft : mat::Card;
      fg = active_ ? mat::Accent : mat::Text3;
      stroke = mat::Card;
      break;
    case Tab:
      bg = active_ ? mat::Accent : (hover_ ? mat::CardHover : mat::Card);
      fg = active_ ? FL_WHITE : mat::Text2;
      stroke = active_ ? mat::Accent : mat::Stroke;
      break;
  }

  // Filled 的按下态已经在上面 shade(Accent, 0.75) 里处理过，
  // 这里再压暗一次会变成 0.5625 倍，颜色明显偏黑。

  // 禁用态：变灰、无悬停反馈。
  // 状态清理放在 handle() 里做，draw() 里改成员状态太脆——如果按下后
  // 没来得及重绘，禁用的按钮也会 fire callback。
  if (!Fl_Widget::active()) {
    bg = fl_color_average(FL_WHITE, bg, 0.5);
    fg = mat::Text3;
    stroke = mat::Stroke;
  }

  mat::roundRect(x(), y(), w(), h(), radius_, bg, stroke);

  fl_font(FL_HELVETICA, fontSize_);
  fl_color(fg);
  if (alignLeft_)
    fl_draw(label(), x() + 12, y(), w() - 12, h(), FL_ALIGN_LEFT);
  else
    fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_CENTER);
}

int MButton::handle(int event) {
  // 禁用态吞掉所有鼠标事件，避免靠 draw() 清状态这种脆弱写法
  if (!Fl_Widget::active())
    return 0;

  switch (event) {
    case FL_ENTER:
      hover_ = true;
      redraw();
      return 1;
    case FL_LEAVE:
      hover_ = false;
      // 按下期间窗口失焦/被系统打断时，FL_RELEASE 可能送不到，
      // 不在这里清 down_ 会一直卡在按下态。
      down_ = false;
      redraw();
      return 1;
    case FL_PUSH:
      if (Fl::event_button() == FL_LEFT_MOUSE) {
        down_ = true;
        redraw();
        return 1;
      }
      return 0;
    case FL_DRAG:
      if (down_) {
        const bool in = Fl::event_inside(this);
        if (in != hover_) {
          hover_ = in;
          redraw();
        }
        return 1;
      }
      return 0;
    case FL_RELEASE:
      if (down_) {
        down_ = false;
        redraw();
        if (Fl::event_inside(this)) {
          if (style_ == Theme)
            active_ = true;
          do_callback();
        }
        return 1;
      }
      return 0;
    default:
      return Fl_Widget::handle(event);
  }
}

// ============================================================
// MEntryCard
// ============================================================

MEntryCard::MEntryCard(int x, int y, int w, int h, const char *title,
                       const char *subtitle, Icon icon)
    : Fl_Widget(x, y, w, h),
      icon_(icon),
      hover_(false),
      down_(false),
      badge_(false) {
  strncpy(title_, title ? title : "", sizeof(title_) - 1);
  title_[sizeof(title_) - 1] = '\0';
  strncpy(subtitle_, subtitle ? subtitle : "", sizeof(subtitle_) - 1);
  subtitle_[sizeof(subtitle_) - 1] = '\0';
  box(FL_NO_BOX);
}

void MEntryCard::subtitle(const char *s) {
  strncpy(subtitle_, s ? s : "", sizeof(subtitle_) - 1);
  subtitle_[sizeof(subtitle_) - 1] = '\0';
  redraw();
}

void MEntryCard::drawIcon(int cx, int cy, int size) {
  fl_color(mat::Accent);
  const int h = size / 2;
  switch (icon_) {
    case History: {
      // 圆 + 时钟指针 + 左侧箭头
      // FLTK 要求 a2 >= a1，这里扫 300°（300..600），左下留 60° 缺口
      fl_arc(cx - h, cy - h, size, size, 300, 600);
      fl_line(cx, cy, cx, cy - h + 2);
      fl_line(cx, cy, cx + h - 3, cy + 2);
      fl_line(cx - h + 1, cy - h + 3, cx - h + 1, cy - h + 6);
      break;
    }
    case Options: {
      // 齿轮：外圆 + 8 根齿 + 内圆
      fl_circle(cx, cy, h - 1);
      const double pi = 3.14159265358979;
      for (int i = 0; i < 8; i++) {
        const double a = i * pi / 4.0;
        const int x1 = cx + (int)(cos(a) * (h - 1));
        const int y1 = cy + (int)(sin(a) * (h - 1));
        const int x2 = cx + (int)(cos(a) * h);
        const int y2 = cy + (int)(sin(a) * h);
        fl_line(x1, y1, x2, y2);
      }
      fl_circle(cx, cy, h / 3);
      break;
    }
    case Update: {
      // 下载：竖线 + 箭头 + 底部托盘
      fl_line(cx, cy - h, cx, cy + 1);
      fl_line(cx, cy + 1, cx - 4, cy - 3);
      fl_line(cx, cy + 1, cx + 4, cy - 3);
      fl_line(cx - h, cy + h - 1, cx + h, cy + h - 1);
      break;
    }
    case About: {
      // 信息：圆 + i
      fl_circle(cx, cy, h);
      fl_line(cx, cy - 3, cx, cy - 4);
      fl_line(cx, cy - 1, cx, cy + 4);
      break;
    }
  }
}

void MEntryCard::draw() {
  // 按下时用 accent-soft 打底，给出明确的点击反馈
  const bool press = down_ && hover_;
  mat::roundRect(x(), y(), w(), h(), 12,
                 press ? mat::AccentSoft : (hover_ ? mat::CardHover : mat::Card),
                 (hover_ || down_) ? mat::Stroke2 : mat::Stroke);

  // 图标盒 30×30，8px 圆角，accent-soft 底
  const int boxSz = 30;
  const int ix = x() + 10;
  const int iy = y() + (h() - boxSz) / 2;
  mat::roundRectFill(ix, iy, boxSz, boxSz, 8, mat::AccentSoft);
  drawIcon(ix + boxSz / 2, iy + boxSz / 2, 14);

  // 文本
  const int tx = ix + boxSz + 10;
  const int tw = w() - (tx - x()) - 10;
  fl_font(FL_HELVETICA_BOLD, 12);
  fl_color(mat::Text);
  fl_draw(title_, tx, y() + 13, tw, 16, FL_ALIGN_LEFT);
  fl_font(FL_HELVETICA, 10);
  fl_color(mat::Text3);
  fl_draw(subtitle_, tx, y() + 28, tw, 14, FL_ALIGN_LEFT);

  // 右上角红点
  if (badge_) {
    fl_color(mat::Err);
    fl_pie(x() + w() - 19, y() + 12, 7, 7, 0, 360);
  }
}

int MEntryCard::handle(int event) {
  // 禁用态直接吞掉所有鼠标事件
  if (!Fl_Widget::active())
    return 0;

  switch (event) {
    case FL_ENTER:
      hover_ = true;
      redraw();
      return 1;
    case FL_LEAVE:
      hover_ = false;
      down_ = false;
      redraw();
      return 1;
    case FL_PUSH:
      // FLTK 1.3 的约定：只有 FL_PUSH 返回非 0，后续 FL_RELEASE 才保证送到本控件。
      // 不认领的话点击能不能触发全靠平台实现，会出现「点了没反应」。
      if (Fl::event_button() == FL_LEFT_MOUSE) {
        down_ = true;
        redraw();
        return 1;
      }
      return 0;
    case FL_DRAG:
      // 按住后拖出卡片要立刻取消「按下」高亮，否则鼠标都到外面了还亮着
      if (down_) {
        const bool in = Fl::event_inside(this);
        if (in != hover_) {
          hover_ = in;
          redraw();
        }
        return 1;
      }
      return 0;
    case FL_RELEASE:
      if (down_) {
        down_ = false;
        redraw();
        if (Fl::event_inside(this))
          do_callback();
        return 1;
      }
      return 0;
    default:
      return Fl_Widget::handle(event);
  }
}

// ============================================================
// MChip
// ============================================================

MChip::MChip(int x, int y, int w, int h, const char *label)
    : Fl_Widget(x, y, w, h), hover_(false), down_(false) {
  strncpy(label_, label ? label : "", sizeof(label_) - 1);
  label_[sizeof(label_) - 1] = '\0';
  box(FL_NO_BOX);
}

void MChip::setLabel(const char *s) {
  strncpy(label_, s ? s : "", sizeof(label_) - 1);
  label_[sizeof(label_) - 1] = '\0';
  redraw();
}

void MChip::draw() {
  const int r = h() / 2;
  const bool on = hover_ || down_;
  mat::roundRect(x(), y(), w(), h(), r, down_ ? mat::AccentSoft : (on ? mat::CardHover : mat::Bg),
                 on ? mat::Accent : mat::Stroke);

  // 绿点
  fl_color(mat::Ok);
  fl_pie(x() + 10, y() + (h() - 6) / 2, 6, 6, 0, 360);

  // 文字
  fl_font(FL_HELVETICA, 11);
  fl_color(hover_ ? mat::Accent : mat::Text2);
  fl_draw(label_, x() + 20, y(), w() - 26, h(), FL_ALIGN_LEFT);
}

int MChip::handle(int event) {
  // 同 MEntryCard：必须认领 FL_PUSH，否则 FL_RELEASE 不保证送达
  if (!Fl_Widget::active())
    return 0;

  switch (event) {
    case FL_ENTER:
      hover_ = true;
      redraw();
      return 1;
    case FL_LEAVE:
      hover_ = false;
      down_ = false;
      redraw();
      return 1;
    case FL_PUSH:
      if (Fl::event_button() == FL_LEFT_MOUSE) {
        down_ = true;
        redraw();
        return 1;
      }
      return 0;
    case FL_DRAG:
      // 同 MEntryCard：拖出后立即取消按下高亮
      if (down_) {
        const bool in = Fl::event_inside(this);
        if (in != hover_) {
          hover_ = in;
          redraw();
        }
        return 1;
      }
      return 0;
    case FL_RELEASE:
      if (down_) {
        down_ = false;
        redraw();
        if (Fl::event_inside(this))
          do_callback();
        return 1;
      }
      return 0;
    default:
      return Fl_Widget::handle(event);
  }
}

// ============================================================
// MInput
// ============================================================

MInput::MInput(int x, int y, int w, int h, const char *label)
    : Fl_Input(x, y, w, h, label), focus_(false) {
  placeholder_[0] = '\0';
  box(FL_NO_BOX);
  textsize(13);
  textcolor(mat::Text);
  color(mat::Bg);
  selection_color(mat::AccentSoft);
}

void MInput::draw() {
  // 左右内边距：FLTK 1.3 的 Fl_Input 没有 padding 接口，直接调
  // drawtext() 自己指定绘制区，保证占位符和真实文字左边界一致。
  const int pad = 11;

  // 焦点光晕：画在控件外 3px，靠 handle() 里 damage 扩大区域保证不被裁
  if (focus_) {
    mat::roundRectFill(x() - 3, y() - 3, w() + 6, h() + 6, 14,
                       mat::AccentSoft);
  }
  mat::roundRect(x(), y(), w(), h(), 12, mat::Bg,
                 focus_ ? mat::Accent : mat::Stroke2);

  // 文字与光标（Fl_Input_::drawtext 是 protected，子类可直接调）
  drawtext(x() + pad, y() + 1, w() - 2 * pad, h() - 2);

  // 占位符
  if (placeholder_[0] != '\0' && size() == 0 && !focus_) {
    fl_font(FL_HELVETICA, 13);
    fl_color(mat::Text3);
    fl_draw(placeholder_, x() + pad, y(), w() - 2 * pad, h(), FL_ALIGN_LEFT);
  }
}

int MInput::handle(int event) {
  switch (event) {
    case FL_FOCUS:
    case FL_UNFOCUS:
      focus_ = (event == FL_FOCUS);
      // 光晕画在控件外 3px，普通 redraw() 只 damage 自身 bbox、
      // 边界外那圈会被裁掉，必须显式扩大 damage 区域。
      damage(FL_DAMAGE_ALL, x() - 3, y() - 3, w() + 6, h() + 6);
      break;
  }
  return Fl_Input::handle(event);
}

// ============================================================
// MPanel
// ============================================================

MPanel::MPanel(int x, int y, int w, int h) : Fl_Group(x, y, w, h) {
  box(FL_NO_BOX);
}

void MPanel::draw() {
  mat::roundRect(x(), y(), w(), h(), 12, mat::Bg, mat::Stroke);
  draw_children();
}

// ============================================================
// MLogo
// ============================================================

MLogo::MLogo(int x, int y, int w, int h) : Fl_Widget(x, y, w, h) {
  box(FL_NO_BOX);
}

void MLogo::draw() {
  // 22×22 显示器线性图标（对应设计稿 svg）
  const int s = 22;
  const int cx = x() + w() / 2;
  const int cy = y() + h() / 2;
  const int l = cx - s / 2;
  const int t = cy - s / 2;

  fl_color(mat::Accent);
  mat::roundRectOutline(l, t, s, s - 6, 3, mat::Accent);  // 屏幕
  fl_line(l + 6, t + s - 1, l + s - 6, t + s - 1);        // 底座
  fl_line(cx, t + s - 6, cx, t + s - 1);                  // 支架
}

