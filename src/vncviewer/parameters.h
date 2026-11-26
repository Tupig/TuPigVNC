/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 * Copyright 2011 Pierre Ossman <ossman@cendio.se> for Cendio AB
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
#ifndef __PARAMETERS_H__
#define __PARAMETERS_H__

#include <list>
#include <string>

namespace core {
  class AliasParameter;
  class BoolParameter;
  class EnumListParameter;
  class EnumParameter;
  class IntParameter;
  class StringParameter;
}

class MonitorIndicesParameter;

extern core::IntParameter pointerEventInterval;
extern core::BoolParameter emulateMiddleButton;
extern core::BoolParameter dotWhenNoCursor; // 已废弃
extern core::BoolParameter alwaysCursor;
extern core::EnumParameter cursorType;

extern core::StringParameter passwordFile;

extern core::BoolParameter autoSelect;
extern core::BoolParameter fullColour;
extern core::AliasParameter fullColourAlias;
extern core::IntParameter lowColourLevel;
extern core::AliasParameter lowColourLevelAlias;
extern core::EnumParameter preferredEncoding;
extern core::BoolParameter customCompressLevel;
extern core::IntParameter compressLevel;
extern core::IntParameter qualityLevel;

extern core::BoolParameter maximize;
extern core::BoolParameter fullScreen;
extern core::EnumParameter fullScreenMode;
extern core::BoolParameter fullScreenAllMonitors; // 已废弃
extern MonitorIndicesParameter fullScreenSelectedMonitors;
extern core::StringParameter desktopSize;
extern core::StringParameter geometry;
extern core::BoolParameter remoteResize;

extern core::BoolParameter listenMode;

extern core::BoolParameter viewOnly;
extern core::BoolParameter shared;

#ifdef HAVE_AUDIO
extern core::BoolParameter playAudio;
#endif

extern core::BoolParameter acceptClipboard;
extern core::BoolParameter setPrimary;
extern core::BoolParameter sendClipboard;
#if !defined(WIN32) && !defined(__APPLE__)
extern core::BoolParameter sendPrimary;
extern core::StringParameter display;
#endif

extern core::EnumListParameter shortcutModifiers;

extern core::BoolParameter fullscreenSystemKeys;
extern core::BoolParameter alertOnFatalError;
extern core::BoolParameter reconnectOnError;

extern core::StringParameter via;

void saveViewerParameters(const char *filename, const char *servername=nullptr);
std::string loadViewerParameters(const char *filename);

#ifdef _WIN32
std::list<std::string> loadHistoryFromRegKey();
#endif

void migrateDeprecatedOptions();

#endif
