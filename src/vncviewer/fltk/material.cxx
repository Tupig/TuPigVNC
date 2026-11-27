/*
 * TuPigVNC · Material 3 自绘控件库实现
 */

#include "material.h"

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/fl_draw.H>

#include <math.h>
#include <stdio.h>
#include <string.h>

// FLTK 1.3 的 GDI 绘制是硬边（圆角、圆、斜线全是锯齿）。
// 这里接 GDI+ 做抗锯齿：Graphics 直接包住 FLTK 当前的 fl_gc，
// 边缘像素与 DC 上已画好的背景做 alpha 混合，因此调用方必须先
// 画背景再画形状，且窗口建议用 Fl_Double_Window 避免闪烁。
#ifdef WIN32
#include <FL/x.H>
#include <windows.h>
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "msimg32.lib")

namespace {

struct GdiplusBoot {
  ULONG_PTR token = 0;
  GdiplusBoot()
  {
    Gdiplus::GdiplusStartupInput input;
    Gdiplus::GdiplusStartup(&token, &input, nullptr);
  }
  ~GdiplusBoot() { Gdiplus::GdiplusShutdown(token); }
};

void ensureGdiplus()
{
  // 局部静态：首次调用时才加载 gdiplus.dll，线程安全
  static GdiplusBoot boot;
  (void)boot;
}

Gdiplus::Color toGdiColor(Fl_Color c)
{
  uchar r = 0, g = 0, b = 0;
  Fl::get_color(c, r, g, b);
  return Gdiplus::Color(255, r, g, b);
}

void addRoundRect(Gdiplus::GraphicsPath &path, float x, float y, float w,
                  float h, float r)
{
  if (r * 2 > w)
    r = w / 2;
  if (r * 2 > h)
    r = h / 2;
  if (r <= 0.0f) {
    path.AddRectangle(Gdiplus::RectF(x, y, w, h));
    return;
  }
  path.AddArc(x, y, r * 2, r * 2, 180, 90);
  path.AddArc(x + w - r * 2, y, r * 2, r * 2, 270, 90);
  path.AddArc(x + w - r * 2, y + h - r * 2, r * 2, r * 2, 0, 90);
  path.AddArc(x, y + h - r * 2, r * 2, r * 2, 90, 90);
  path.CloseFigure();
}

/* GDI+ 直接画到 FLTK 的 DC 上在部分环境下不可见，这里先画到
 * 32 位 DIB（带 alpha），再交给 FLTK 的 Fl_RGB_Image 做 alpha 混合
 * 绘制 —— 这是 FLTK 自己画图片走的同一条路径，保证能显示。 */
template <typename PaintFn>
void paintAA(int x, int y, int w, int h, PaintFn paint)
{
  if (w <= 0 || h <= 0)
    return;
  ensureGdiplus();

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;  // 自上而下
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;

  void *bits = nullptr;
  HDC mem = CreateCompatibleDC(fl_gc);
  HBITMAP bmp =
      CreateDIBSection(mem, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bmp == nullptr) {
    DeleteDC(mem);
    return;
  }
  HGDIOBJ old = SelectObject(mem, bmp);

  {
    Gdiplus::Graphics g(mem);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    g.Clear(Gdiplus::Color(0, 0, 0, 0));
    paint(g);
    g.Flush();
  }

  SelectObject(mem, old);
  DeleteDC(mem);

  // DIB 是 BGRA（Windows 字节序），Fl_RGB_Image 要 RGBA
  unsigned char *px = (unsigned char *)bits;
  const int npix = w * h;
  for (int i = 0; i < npix; i++) {
    const unsigned char t = px[i * 4];
    px[i * 4] = px[i * 4 + 2];
    px[i * 4 + 2] = t;
  }

  Fl_RGB_Image img(px, w, h, 4, w * 4);
  img.draw(x, y);

  DeleteObject(bmp);
}

}  // namespace
#endif

namespace mat {

// ---- 调色板（初始为默认值，initColors 会按需覆盖）----
// 暗色 Material 3：底色近黑，强调色用提亮的紫，保证暗底上的对比度
Fl_Color Card = 0x1C1A2400;
Fl_Color Bg = 0x14121A00;
Fl_Color CardHover = 0x26233200;
Fl_Color Text = 0xEAE6F500;
Fl_Color Text2 = 0xA8A2BB00;
Fl_Color Text3 = 0x6E688000;
Fl_Color Accent = 0xB4A6FF00;
Fl_Color AccentSoft = 0x2E284600;
Fl_Color AccentHover = 0xC4B8FF00;
Fl_Color Stroke = 0x2A263600;
Fl_Color Stroke2 = 0x38324300;
Fl_Color Ok = 0x6DD58C00;
Fl_Color OkSoft = 0x1E3A2800;
Fl_Color Err = 0xFFB4AB00;
Fl_Color ErrSoft = 0x3B1A1E00;
Fl_Color OnAccent = 0x2A1E5C00;

void initColors() {
  // 暗色 Material 3 调色板
  Card = fl_rgb_color(28, 26, 36);        // #1C1A24 卡片/窗口底
  Bg = fl_rgb_color(20, 18, 26);          // #14121A 输入框/面板底
  CardHover = fl_rgb_color(38, 35, 50);   // #262332
  Text = fl_rgb_color(234, 230, 245);     // #EAE6F5
  Text2 = fl_rgb_color(168, 162, 187);    // #A8A2BB
  Text3 = fl_rgb_color(110, 104, 128);    // #6E6880
  Accent = fl_rgb_color(180, 166, 255);   // #B4A6FF
  AccentSoft = fl_rgb_color(46, 40, 70);  // #2E2846
  AccentHover = fl_rgb_color(196, 184, 255);  // #C4B8FF
  Stroke = fl_rgb_color(42, 38, 54);      // #2A2636
  Stroke2 = fl_rgb_color(56, 50, 67);     // #383243
  Ok = fl_rgb_color(109, 213, 140);      // #6DD58C
  OkSoft = fl_rgb_color(30, 58, 40);      // #1E3A28
  Err = fl_rgb_color(255, 180, 171);      // #FFB4AB
  ErrSoft = fl_rgb_color(59, 26, 30);     // #3B1A1E
  OnAccent = fl_rgb_color(42, 30, 92);    // #2A1E5C
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
#ifdef WIN32
  paintAA(x, y, w, h, [&](Gdiplus::Graphics &g) {
    Gdiplus::GraphicsPath path;
    addRoundRect(path, 0.0f, 0.0f, (float)w, (float)h, (float)r);
    Gdiplus::SolidBrush brush(toGdiColor(c));
    g.FillPath(&brush, &path);
  });
#else
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
#endif
}

void roundRectOutline(int x, int y, int w, int h, int r, Fl_Color c) {
  if (w <= 0 || h <= 0)
    return;
  if (r > h / 2)
    r = h / 2;
  if (r > w / 2)
    r = w / 2;
#ifdef WIN32
  // 描边要沿形状中心线走，否则右/下边会被裁掉半个像素；
  // 外扩 1px 给 pen 宽度留出空间
  paintAA(x - 1, y - 1, w + 2, h + 2, [&](Gdiplus::Graphics &g) {
    Gdiplus::GraphicsPath path;
    addRoundRect(path, 1.5f, 1.5f, (float)w - 2.0f, (float)h - 2.0f,
                 (float)r - 0.5f);
    Gdiplus::Pen pen(toGdiColor(c), 1.0f);
    g.DrawPath(&pen, &path);
  });
#else
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
#endif
}

void circleFill(int cx, int cy, int radius, Fl_Color c) {
  if (radius <= 0)
    return;
#ifdef WIN32
  paintAA(cx - radius, cy - radius, radius * 2, radius * 2,
          [&](Gdiplus::Graphics &g) {
            Gdiplus::SolidBrush brush(toGdiColor(c));
            g.FillEllipse(&brush, 0.0f, 0.0f, (float)(radius * 2),
                          (float)(radius * 2));
          });
#else
  fl_color(c);
  fl_pie(cx - radius, cy - radius, radius * 2, radius * 2, 0, 360);
#endif
}

void lineAA(int x1, int y1, int x2, int y2, Fl_Color c) {
#ifdef WIN32
  const int lx = (x1 < x2 ? x1 : x2) - 2;
  const int ly = (y1 < y2 ? y1 : y2) - 2;
  const int lw = (x1 > x2 ? x1 - x2 : x2 - x1) + 5;
  const int lh = (y1 > y2 ? y1 - y2 : y2 - y1) + 5;
  paintAA(lx, ly, lw, lh, [&](Gdiplus::Graphics &g) {
    Gdiplus::Pen pen(toGdiColor(c), 1.0f);
    g.DrawLine(&pen, (float)(x1 - lx), (float)(y1 - ly), (float)(x2 - lx),
               (float)(y2 - ly));
  });
#else
  fl_color(c);
  fl_line(x1, y1, x2, y2);
#endif
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
      fg = mat::OnAccent;
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
      fg = active_ ? mat::OnAccent : mat::Text2;
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
  const int h = size / 2;
  const Fl_Color ic = mat::Accent;
  switch (icon_) {
    case History: {
      // 圆 + 时钟指针 + 左侧箭头（抗锯齿）
      // FLTK 要求 a2 >= a1，这里扫 300°（300..600），左下留 60° 缺口
      mat::roundRectOutline(cx - h, cy - h, size, size, h, ic);
      mat::lineAA(cx, cy, cx, cy - h + 2, ic);
      mat::lineAA(cx, cy, cx + h - 3, cy + 2, ic);
      mat::lineAA(cx - h + 1, cy - h + 3, cx - h + 1, cy - h + 6, ic);
      break;
    }
    case Options: {
      // 齿轮：外圆 + 8 根齿 + 内圆
      mat::roundRectOutline(cx - h, cy - h, size, size, h, ic);
      const double pi = 3.14159265358979;
      for (int i = 0; i < 8; i++) {
        const double a = i * pi / 4.0;
        const int x1 = cx + (int)(cos(a) * (h - 1));
        const int y1 = cy + (int)(sin(a) * (h - 1));
        const int x2 = cx + (int)(cos(a) * h);
        const int y2 = cy + (int)(sin(a) * h);
        mat::lineAA(x1, y1, x2, y2, ic);
      }
      mat::roundRectOutline(cx - h / 3, cy - h / 3, (h / 3) * 2,
                            (h / 3) * 2, h / 3, ic);
      break;
    }
    case Update: {
      // 下载：竖线 + 箭头 + 底部托盘
      mat::lineAA(cx, cy - h, cx, cy + 1, ic);
      mat::lineAA(cx, cy + 1, cx - 4, cy - 3, ic);
      mat::lineAA(cx, cy + 1, cx + 4, cy - 3, ic);
      mat::lineAA(cx - h, cy + h - 1, cx + h, cy + h - 1, ic);
      break;
    }
    case About: {
      // 信息：圆 + i
      mat::roundRectOutline(cx - h, cy - h, size, size, h, ic);
      mat::lineAA(cx, cy - 3, cx, cy - 4, ic);
      mat::lineAA(cx, cy - 1, cx, cy + 4, ic);
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
    mat::circleFill(x() + w() - 15, y() + 15, 4, mat::Err);
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
  mat::circleFill(x() + 13, y() + h() / 2, 3, mat::Ok);

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
// MTag
// ============================================================

MTag::MTag(int x, int y, int w, int h, const char *label)
    : Fl_Widget(x, y, w, h, label) {
  box(FL_NO_BOX);
}

void MTag::draw() {
  mat::roundRectFill(x(), y(), w(), h(), 6, mat::AccentSoft);
  fl_font(FL_HELVETICA, 10);
  fl_color(mat::Accent);
  fl_draw(label(), x(), y(), w(), h(), FL_ALIGN_CENTER);
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


// ============================================================
// Material 3 风格消息框
//
// FLTK 自带的 fl_alert / fl_message 是 90 年代灰底立体样式，
// 而且按钮固定英文 "OK"，和整个界面格格不入。这里用自绘控件重做。
// ============================================================

static bool sAlertDone = false;
static int sAlertResult = 0;

static void alertBtnCb(Fl_Widget *w, void *data)
{
  sAlertResult = (int)(intptr_t)data;
  sAlertDone = true;
  if (w->window() != nullptr)
    w->window()->hide();
}

int matChoice(const char *title, const char *message,
              const char *b0, const char *b1, const char *b2,
              const char *b3)
{
  mat::initColors();

  const char *buttons[4] = {b0, b1, b2, b3};
  int count = 0;
  while (count < 4 && buttons[count] != nullptr)
    count++;
  if (count == 0) {
    buttons[0] = "确定";
    count = 1;
  }

  const int btnH = 34, btnGap = 10, btnPadH = 22;
  int widths[4] = {0, 0, 0, 0};
  int totalW = 0;
  for (int i = 0; i < count; i++) {
    int tw = (int)strlen(buttons[i]) * 13 + btnPadH * 2;
    if (tw < 72)
      tw = 72;
    if (tw > 150)
      tw = 150;
    widths[i] = tw;
    totalW += tw + (i > 0 ? btnGap : 0);
  }

  const int W = 440;
  const int H = 200;

  // 用双缓冲窗口：GDI+ 抗锯齿要读背景像素做混合，直接画屏幕 DC 会闪
  Fl_Window *win = new Fl_Window(W, H);
  win->color(mat::Card);
  win->set_modal();
  win->begin();

  // 标题
  Fl_Box *t = new Fl_Box(24, 22, W - 48, 26, title != nullptr ? title : "");
  t->labelfont(FL_HELVETICA_BOLD);
  t->labelsize(15);
  t->labelcolor(mat::Text);
  t->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);

  // 正文
  Fl_Box *m = new Fl_Box(24, 56, W - 48, 72,
                         message != nullptr ? message : "");
  m->labelsize(12);
  m->labelcolor(mat::Text2);
  m->align(FL_ALIGN_LEFT | FL_ALIGN_TOP | FL_ALIGN_WRAP | FL_ALIGN_INSIDE);

  // 按钮：从右往左排，第 0 个（主操作）在最右边
  int bx = W - 24;
  for (int i = count - 1; i >= 0; i--) {
    bx -= widths[i];
    MButton *b = new MButton(bx, H - 24 - btnH, widths[i], btnH, buttons[i],
                            i == 0 ? MButton::Filled : MButton::Ghost);
    b->radius(12);
    b->fontSize(13);
    b->callback(alertBtnCb, (void*)(intptr_t)i);
    bx -= btnGap;
  }

  win->end();

  sAlertDone = false;
  sAlertResult = 0;

  win->show();
  Fl::flush();
  fprintf(stderr, "[matChoice] shown=%d x=%d y=%d w=%d h=%d children=%d\n",
          (int)win->shown(), win->x(), win->y(), win->w(), win->h(),
          win->children());
  while (!sAlertDone) {
    Fl::wait(0.1);
    Fl::flush();
  }
  fprintf(stderr, "[matChoice] done result=%d\n", sAlertResult);
  Fl::delete_widget(win);

  return sAlertResult;
}

int matAlert(const char *title, const char *message, bool withCancel)
{
  if (withCancel)
    return matChoice(title, message, "确定", "取消");
  return matChoice(title, message, "确定");
}

void matMessage(const char *title, const char *message)
{
  matChoice(title, message, "确定");
}

bool matConfirm(const char *title, const char *message,
                const char *okText, const char *cancelText)
{
  return matChoice(title, message,
                   okText != nullptr ? okText : "确定",
                   cancelText != nullptr ? cancelText : "取消") == 0;
}
