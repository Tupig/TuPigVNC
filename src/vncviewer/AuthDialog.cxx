/* Copyright 2024 Adam Halim for Cendio AB
 * Copyright 2011-2026 Pierre Ossman for Cendio AB
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

#include <assert.h>
#include <cstdint>

#include <FL/Fl.H>
#include <FL/Fl_Box.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Pixmap.H>
#include <FL/fl_ask.H>

#include <core/Configuration.h>
#include <core/i18n.h>

#include "fltk/Fl_Password_Input.h"
#include "fltk/layout.h"
#include "fltk/material.h"
#include "fltk/theme.h"

#include "AuthDialog.h"
#include "parameters.h"

/* xpm 数组早于 const 关键字出现，因此它们的定义不符合规范 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wwrite-strings"
#include "../../assets/secure.xpm"
#include "../../assets/insecure.xpm"
#pragma GCC diagnostic pop

static Fl_Pixmap secure_icon(secure);
static Fl_Pixmap insecure_icon(insecure);

AuthDialog::AuthDialog(bool secure_, bool needsUser, bool needsPassword)
  : Fl_Window(410, 0, _("VNC authentication"))
{
  const int PAD = 20;      // 窗口内边距
  const int GAP = 12;      // 字段间距
  const int FIELD_H = 34;  // 输入框高度（设计稿 34px）
  const int BTN_W = 88;    // 按钮宽度
  const int BTN_H = 34;    // 按钮高度
  const int ICON = 44;     // 圆形问号图标直径
  const int LABEL_H = 16;  // 字段小标题高度

  result_ = 0;
  mat::initColors();
  color(mat::Card);

  // ---- 顶部安全状态 pill（浅色圆角横幅）----
  Fl_Box *banner = new Fl_Box(PAD, PAD, w() - PAD * 2, 32);
  banner->box(FL_ROUNDED_BOX);
  banner->align(FL_ALIGN_CENTER | FL_ALIGN_INSIDE |
                FL_ALIGN_IMAGE_NEXT_TO_TEXT);
  banner->labelsize(12);
  banner->labelcolor(secure_ ? mat::Ok : mat::Err);
  banner->color(secure_ ? mat::OkSoft : mat::ErrSoft);
  banner->label(secure_ ? _("This connection is secure")
                        : _("This connection is not secure"));
  banner->image(secure_ ? secure_icon : insecure_icon);

  int y = PAD + 32 + 16;

  // ---- 品牌紫圆底问号图标 ----
  Fl_Box *icon = new Fl_Box(PAD, y, ICON, ICON, "?");
  icon->box(FL_OVAL_BOX);
  icon->color(mat::Accent);
  icon->labelcolor(FL_WHITE);
  icon->labelfont(FL_HELVETICA_BOLD);
  icon->labelsize(22);
  icon->align(FL_ALIGN_CENTER);

  const int fx = PAD + ICON + 16;  // 字段左边界
  const int fw = w() - PAD - fx;   // 字段宽度
  int fy = y;

  if (needsUser) {
    Fl_Box *lbl = new Fl_Box(fx, fy, fw, LABEL_H, _("Username:"));
    lbl->labelsize(11);
    lbl->labelcolor(mat::Text2);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fy += LABEL_H + 2;

    username = new MInput(fx, fy, fw, FIELD_H);
    fy += FIELD_H + GAP;
  } else {
    username = nullptr;
  }

  if (needsPassword) {
    Fl_Box *lbl = new Fl_Box(fx, fy, fw, LABEL_H, _("Password:"));
    lbl->labelsize(11);
    lbl->labelcolor(mat::Text2);
    lbl->align(FL_ALIGN_LEFT | FL_ALIGN_INSIDE);
    fy += LABEL_H + 2;

    // 密码框保留 Fl_Password_Input（自带明文切换眼睛），只改圆角外观
    passwd = new Fl_Password_Input(fx, fy, fw, FIELD_H, nullptr);
    passwd->box(THEME_UP_BOX);
    passwd->color(mat::Card);
    fy += FIELD_H + GAP;

    if (reconnectOnError) {
      keepPasswdCheckbox = new Fl_Check_Button(
          LBLRIGHT(fx, fy, CHECK_MIN_WIDTH, CHECK_HEIGHT,
                   _("Keep password for reconnect")));
      keepPasswdCheckbox->box(FL_NO_BOX);
      keepPasswdCheckbox->down_box(FL_DOWN_BOX);
      keepPasswdCheckbox->labelsize(12);
      keepPasswdCheckbox->labelcolor(mat::Text);
      keepPasswdCheckbox->clear_visible_focus();
      fy += CHECK_HEIGHT + GAP;
    } else {
      keepPasswdCheckbox = nullptr;
    }
  } else {
    passwd = nullptr;
    keepPasswdCheckbox = nullptr;
  }

  // ---- 底部按钮：取消（Ghost）+ 确定（Filled），右对齐 ----
  const int contentBottom = (y + ICON > fy) ? (y + ICON) : fy;
  const int by = contentBottom + 16;
  const int okX = w() - PAD - BTN_W;
  const int cancelX = okX - 8 - BTN_W;

  MButton *okBtn = new MButton(okX, by, BTN_W, BTN_H, fl_ok,
                               MButton::Filled);
  okBtn->radius(12);
  okBtn->fontSize(13);
  okBtn->callback(button_cb, (void*)(std::intptr_t)1);

  MButton *cancelBtn = new MButton(cancelX, by, BTN_W, BTN_H, fl_cancel,
                                   MButton::Ghost);
  cancelBtn->radius(12);
  cancelBtn->fontSize(13);
  cancelBtn->callback(button_cb, (void*)(std::intptr_t)0);

  end();

  size(w(), by + BTN_H + PAD);

  set_modal();
}

AuthDialog::~AuthDialog()
{
}

int AuthDialog::result()
{
  return result_;
}

std::string AuthDialog::getUser()
{
  return username->value();
}

std::string AuthDialog::getPassword()
{
  if (passwd)
    return passwd->value();
  return "";
}

bool AuthDialog::getKeepPassword()
{
  if (keepPasswdCheckbox)
    return keepPasswdCheckbox->value();
  return false;
}

int AuthDialog::handle(int event)
{
  // MButton 不处理键盘，这里统一接管回车（确定）与 Esc（取消）
  if (event == FL_KEYBOARD) {
    switch (Fl::event_key()) {
    case FL_Escape:
      result_ = 0;
      hide();
      return 1;
    case FL_Enter:
    case FL_KP_Enter:
      result_ = 1;
      hide();
      return 1;
    }
  }

  return Fl_Window::handle(event);
}

void AuthDialog::button_cb(Fl_Widget *w, void *val)
{
  AuthDialog* self;

  self = dynamic_cast<AuthDialog*>(w->window());
  assert(self != nullptr);

  self->result_ = (int)(std::intptr_t)val;
  self->hide();
}
