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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_GNUTLS
#include <rfb/CSecurityTLS.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#include "parameters.h"

#include <core/Configuration.h>
#include <core/Exception.h>
#include <core/LogWriter.h>
#include <core/i18n.h>
#include <core/string.h>
#include <core/xdgdirs.h>

#include <rfb/CConnection.h>
#include <rfb/SecurityClient.h>

#include <FL/fl_utf8.h>

#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>

#include "MonitorIndicesParameter.h"

static core::LogWriter vlog("Parameters");

core::IntParameter
  pointerEventInterval("PointerEventInterval",
                       _("Time in milliseconds to rate-limit "
                         "successive pointer events"),
                       17, 0, INT_MAX);
core::BoolParameter
  emulateMiddleButton("EmulateMiddleButton",
                      _("Emulate middle mouse button by pressing left "
                        "and right mouse buttons simultaneously"),
                      false);
core::BoolParameter
  dotWhenNoCursor("DotWhenNoCursor",
                  _("[DEPRECATED] Show a dot cursor when the server "
                    "sends an invisible cursor"),
                  false);
core::BoolParameter
  alwaysCursor("AlwaysCursor",
               _("Show local cursor when not provided by the server"),
               false);
core::EnumParameter
  cursorType("CursorType",
             core::format(
               "%s (%s)",
               _("Specify which local cursor type should be used when "
                 "AlwaysCursor is active"), ("Dot, System")).c_str(),
             {"Dot", "System"}, "Dot");

core::BoolParameter
  alertOnFatalError("AlertOnFatalError",
                    _("Show an error dialog on connection problems, "
                      "rather than exiting immediately"),
                    true);

core::BoolParameter
  reconnectOnError("ReconnectOnError",
                   _("Show an error dialog on connection problems, "
                     "rather than exiting immediately, and allow the "
                     "user to reconnect"),
                   true);

core::StringParameter
  passwordFile("PasswordFile",
               _("Password file for VNC authentication"),
               "");
core::AliasParameter
  passwd("passwd", &passwordFile);

core::BoolParameter
  autoSelect("AutoSelect",
             _("Auto select pixel format and encoding"),
             true);
core::BoolParameter
  fullColour("FullColor", _("Use all available colors"), true);
core::AliasParameter
  fullColourAlias("FullColour", &fullColour);
core::IntParameter
  lowColourLevel("LowColorLevel",
                 _("Color level to use on slow connections, "
                   "0 = Very Low, 1 = Low, 2 = Medium"),
                 2, 0, 2);
core::AliasParameter
  lowColourLevelAlias("LowColourLevel", &lowColourLevel);
core::EnumParameter
  preferredEncoding("PreferredEncoding",
                    core::format(
                      "%s (%s)",
                      _("Preferred encoding to use"),
                      "Tight, JPEG, ZRLE, Hextile, "
#ifdef HAVE_H264
                      "H.264, "
#endif
                      "Raw)").c_str(),
                    {"Tight", "JPEG", "ZRLE", "Hextile",
#ifdef HAVE_H264
                     "H.264",
#endif
                     "Raw"},
                    "Tight");
core::BoolParameter
  customCompressLevel("CustomCompressLevel",
                      _("Use custom compression level as specified by "
                        "CompressLevel"),
                      false);
core::IntParameter
  compressLevel("CompressLevel",
                _("Use specified compression level, 0 = Low, 9 = High"),
                2, 0, 9);
core::IntParameter
  qualityLevel("QualityLevel",
               _("JPEG quality level, 0 = Low, 9 = High"),
               8, 0, 9);

core::BoolParameter
  maximize("Maximize", _("Maximize viewer window"), false);
core::BoolParameter
  fullScreen("FullScreen",
             _("Enable full screen as specified by FullScreenMode"),
             false);
core::EnumParameter
  fullScreenMode("FullScreenMode",
                 core::format(
                   "%s (%s)",
                   _("Specify which monitors to use when in "
                     "full screen"), "Current, Selected, All").c_str(),
                 {"Current", "Selected", "All"}, "Current");

core::BoolParameter
  fullScreenAllMonitors("FullScreenAllMonitors",
                        _("[DEPRECATED] Enable full screen over all "
                          "monitors"),
                        false);
MonitorIndicesParameter
  fullScreenSelectedMonitors("FullScreenSelectedMonitors",
                             _("Use the given list of monitors in full "
                               "screen when FullScreenMode is set to "
                               "Selected"),
                             {1});
core::StringParameter
  desktopSize("DesktopSize",
              _("Reconfigure the desktop size on the server to the "
                "specified size when connecting"),
              "");
core::StringParameter
  geometry("geometry",
           _("Specify size and position of viewer window"),
           "");

core::BoolParameter
  listenMode("listen",
             _("Listen for incoming connections from VNC servers"),
             false);

core::BoolParameter
  remoteResize("RemoteResize",
               _("Dynamically resize the remote desktop size as the "
                 "size of the local client window changes"),
               true);

core::BoolParameter
  viewOnly("ViewOnly",
           _("Don't send any mouse or keyboard events to the server"),
           false);
core::BoolParameter
  shared("Shared",
         _("Don't disconnect other viewers upon connection"),
         false);

#ifdef HAVE_AUDIO
core::BoolParameter
  playAudio("Audio",
            _("Play audio sent by the server, if it offers any"),
            true);
#endif

core::BoolParameter
  acceptClipboard("AcceptClipboard",
                  _("Accept clipboard changes from the server"),
                  true);
core::BoolParameter
  sendClipboard("SendClipboard",
                _("Send clipboard changes to the server"),
                true);
#if !defined(WIN32) && !defined(__APPLE__)
core::BoolParameter
  setPrimary("SetPrimary",
             // 翻译者注意：这里指的是 X11 的两块不同的
             //              剪贴板
             _("Set the primary selection as well as the clipboard "
               "selection"),
             true);
core::BoolParameter
  sendPrimary("SendPrimary",
              // 翻译者注意：这里指的是 X11 的两块不同的
              //              剪贴板
              _("Send the primary selection to the server as well as "
                "the clipboard selection"),
              true);
core::StringParameter
  display("display", _("The X display to use"), "");
#endif

// 有效值列表需与 ShortcutHandler 保持同步
core::EnumListParameter
  shortcutModifiers("ShortcutModifiers",
                    // 翻译者注意：按键名必须使用
                    //              英文填写
                    _("The combination of modifier keys that triggers "
                      "special actions in the viewer instead of being "
                      "sent to the remote session (possible keys are a "
                      "combination of Ctrl, Shift, Alt, and Super)"),
                    {"Ctrl", "Shift", "Alt", "Super",
                     "Win", "Option", "Cmd"},
                    {"Ctrl", "Alt"});

core::BoolParameter
  fullscreenSystemKeys("FullscreenSystemKeys",
                       _("Pass special keys (like Alt+Tab) directly to "
                         "the server when in full-screen mode"),
                       true);

core::StringParameter
  via("via", _("SSH gateway to tunnel the connection via"), "");

static const char* IDENTIFIER_STRING = "TuPigVNC Configuration file Version 1.0";

#ifdef WIN32
/* 注册表基路径，集中定义避免多处硬编码 */
static const wchar_t* const REGISTRY_BASE_PATH =
    L"Software\\TuPigVNC\\vncviewer";
static const wchar_t* const REGISTRY_HISTORY_PATH =
    L"Software\\TuPigVNC\\vncviewer\\history";
#endif

/*
 * 我们只保存可以通过图形界面修改的那部分参数
 */
static core::Parameter* parameterArray[] = {
  /* 安全 */
#ifdef HAVE_GNUTLS
  &rfb::CSecurityTLS::X509CA,
  &rfb::CSecurityTLS::X509CRL,
#endif // HAVE_GNUTLS
  &rfb::SecurityClient::secTypes,
  /* 杂项 */
  &reconnectOnError,
  &shared,
#ifdef HAVE_AUDIO
  &playAudio,
#endif
  /* 压缩 */
  &autoSelect,
  &fullColour,
  &lowColourLevel,
  &preferredEncoding,
  &customCompressLevel,
  &compressLevel,
  &rfb::CConnection::noJpeg,
  &qualityLevel,
  /* 显示 */
  &fullScreen,
  &fullScreenMode,
  &fullScreenSelectedMonitors,
  /* 输入 */
  &viewOnly,
  &emulateMiddleButton,
  &alwaysCursor,
  &cursorType,
  &acceptClipboard,
  &sendClipboard,
#if !defined(WIN32) && !defined(__APPLE__)
  &sendPrimary,
  &setPrimary,
#endif
  &fullscreenSystemKeys,
  /* 键盘快捷键 */
  &shortcutModifiers,
};

static core::Parameter* readOnlyParameterArray[] = {
  &fullScreenAllMonitors,
  &dotWhenNoCursor
};

// 编码表
static const struct EscapeMap {
  const char first;
  const char second;
} replaceMap[] = { { '\n', 'n' },
                   { '\r', 'r' },
                   { '\\', '\\' } };

static bool encodeValue(const char* val, char* dest, size_t destSize) {

  size_t pos = 0;

  for (size_t i = 0; (val[i] != '\0') && (i < (destSize - 1)); i++) {
    bool normalCharacter;
    
    // 检查需要编码的字符序列
    normalCharacter = true;
    for (EscapeMap esc : replaceMap) {

      if (val[i] == esc.first) {
        dest[pos] = '\\';
        pos++;
        if (pos >= destSize)
          return false;

        dest[pos] = esc.second;
        normalCharacter = false;
        break;
      }

      if (normalCharacter) {
        dest[pos] = val[i];
      }
    }

    pos++;
    if (pos >= destSize)
      return false;
  }

  dest[pos] = '\0';
  return true;
}


static bool decodeValue(const char* val, char* dest, size_t destSize) {

  size_t pos = 0;
  
  for (size_t i = 0; (val[i] != '\0') && (i < (destSize - 1)); i++) {
    
    // 检查转义序列
    if (val[i] == '\\') {
      bool escapedCharacter;
      
      escapedCharacter = false;
      for (EscapeMap esc : replaceMap) {
        if (val[i+1] == esc.second) {
          dest[pos] = esc.first;
          escapedCharacter = true;
          i++;
          break;
        }
      }

      if (!escapedCharacter)
        return false;

    } else {
      dest[pos] = val[i];
    }

    pos++;
    if (pos >= destSize) {
      return false;
    }
  }
  
  dest[pos] = '\0';
  return true;
}


#ifdef _WIN32
static void setKeyString(const char *_name, const char *_value, HKEY* hKey) {
  
  const DWORD buffersize = 256;

  wchar_t name[buffersize];
  unsigned size = fl_utf8towc(_name, strlen(_name)+1, name, buffersize);
  if (size >= buffersize)
    throw std::length_error("The name of the parameter is too large");

  char encodingBuffer[buffersize];
  if (!encodeValue(_value, encodingBuffer, buffersize))
    throw std::length_error("The parameter is too large");

  wchar_t value[buffersize];
  size = fl_utf8towc(encodingBuffer, strlen(encodingBuffer)+1, value, buffersize);
  if (size >= buffersize)
    throw std::length_error("The parameter is too large");

  LONG res = RegSetValueExW(*hKey, name, 0, REG_SZ, (BYTE*)&value, (wcslen(value)+1)*2);
  if (res != ERROR_SUCCESS)
    throw core::win32_error("RegSetValueExW", res);
}


static void setKeyInt(const char *_name, const int _value, HKEY* hKey) {

  const DWORD buffersize = 256;
  wchar_t name[buffersize];
  DWORD value = _value;

  unsigned size = fl_utf8towc(_name, strlen(_name)+1, name, buffersize);
  if (size >= buffersize)
    throw std::length_error("The name of the parameter is too large");

  LONG res = RegSetValueExW(*hKey, name, 0, REG_DWORD, (BYTE*)&value, sizeof(DWORD));
  if (res != ERROR_SUCCESS)
    throw core::win32_error("RegSetValueExW", res);
}


static bool getKeyString(const char* _name, char* dest, size_t destSize, HKEY* hKey) {
  
  const DWORD buffersize = 256;
  wchar_t name[buffersize];
  WCHAR* value;
  DWORD valuesize;

  unsigned size = fl_utf8towc(_name, strlen(_name)+1, name, buffersize);
  if (size >= buffersize)
    throw std::length_error("The name of the parameter is too large");

  value = new WCHAR[destSize];
  valuesize = destSize;
  LONG res = RegQueryValueExW(*hKey, name, nullptr, nullptr, (LPBYTE)value, &valuesize);
  if (res != ERROR_SUCCESS){
    delete [] value;
    if (res != ERROR_FILE_NOT_FOUND)
      throw core::win32_error("RegQueryValueExW", res);
    // 该值不存在，将使用默认值。
    return false;
  }

  char* utf8val = new char[destSize];
  size = fl_utf8fromwc(utf8val, destSize, value, wcslen(value)+1);
  delete [] value;
  if (size >= destSize) {
    delete [] utf8val;
    throw std::length_error("The parameter is too large");
  }

  bool ret = decodeValue(utf8val, dest, destSize);
  delete [] utf8val;

  if (!ret)
    throw std::invalid_argument("Invalid format or too large value");

  return true;
}


static bool getKeyInt(const char* _name, int* dest, HKEY* hKey) {
  
  const DWORD buffersize = 256;
  DWORD dwordsize = sizeof(DWORD);
  DWORD value = 0;
  wchar_t name[buffersize];

  unsigned size = fl_utf8towc(_name, strlen(_name)+1, name, buffersize);
  if (size >= buffersize)
    throw std::length_error("The name of the parameter is too large");

  LONG res = RegQueryValueExW(*hKey, name, nullptr, nullptr, (LPBYTE)&value, &dwordsize);
  if (res != ERROR_SUCCESS){
    if (res != ERROR_FILE_NOT_FOUND)
      throw core::win32_error("RegQueryValueExW", res);
    // 该值不存在，将使用默认值。
    return false;
  }

  *dest = (int)value;
  return true;
}

static void removeValue(const char* _name, HKEY* hKey) {
  const DWORD buffersize = 256;
  wchar_t name[buffersize];

  unsigned size = fl_utf8towc(_name, strlen(_name)+1, name, buffersize);
  if (size >= buffersize)
    throw std::length_error("The name of the parameter is too large");

  LONG res = RegDeleteValueW(*hKey, name);
  if (res != ERROR_SUCCESS) {
    if (res != ERROR_FILE_NOT_FOUND)
      throw core::win32_error("RegDeleteValueW", res);
    // 该值不存在，无需删除。
    return;
  }
}

static void saveToReg(const char* servername) {
  
  HKEY hKey;
    
  LONG res = RegCreateKeyExW(HKEY_CURRENT_USER,
                             REGISTRY_BASE_PATH, 0, nullptr,
                             REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, nullptr,
                             &hKey, nullptr);
  if (res != ERROR_SUCCESS)
    throw core::win32_error(_("Failed to create registry key"), res);

  try {
    setKeyString("ServerName", servername, &hKey);
  } catch (std::exception& e) {
    RegCloseKey(hKey);
    throw std::runtime_error(core::format(
      _("Failed to save \"%s\": %s"), "ServerName", e.what()));
  }

  for (core::Parameter* param : parameterArray) {
    core::IntParameter* iparam;
    core::BoolParameter* bparam;

    if (param->isDefault()) {
      try {
        removeValue(param->getName(), &hKey);
      } catch (std::exception& e) {
        RegCloseKey(hKey);
        throw std::runtime_error(
          core::format(_("Failed to remove \"%s\": %s"),
                       param->getName(), e.what()));
      }
      continue;
    }

    iparam = dynamic_cast<core::IntParameter*>(param);
    bparam = dynamic_cast<core::BoolParameter*>(param);

    try {
      if (iparam != nullptr) {
        setKeyInt(iparam->getName(), (int)*(iparam), &hKey);
      } else if (bparam != nullptr) {
        setKeyInt(bparam->getName(), (int)*(bparam), &hKey);
      } else {
        setKeyString(param->getName(), param->getValueStr().c_str(), &hKey);
      }
    } catch (std::exception& e) {
      RegCloseKey(hKey);
      throw std::runtime_error(
        core::format(_("Failed to save \"%s\": %s"),
                     param->getName(), e.what()));
    }
  }

  // 移除只读参数，以与 Linux/macOS 将配置写入磁盘时的行为保持一致。
  // 如果此时该参数尚未迁移，它将会丢失。
  for (core::Parameter* param : readOnlyParameterArray) {
    try {
      removeValue(param->getName(), &hKey);
    } catch (std::exception& e) {
      RegCloseKey(hKey);
      throw std::runtime_error(
        core::format(_("Failed to remove \"%s\": %s"),
                     param->getName(), e.what()));
    }
  }

  res = RegCloseKey(hKey);
  if (res != ERROR_SUCCESS)
    throw core::win32_error(_("Failed to close registry key"), res);
}

std::list<std::string> loadHistoryFromRegKey()
{
  HKEY hKey;
  std::list<std::string> serverHistory;

  LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
                           REGISTRY_HISTORY_PATH, 0,
                           KEY_READ, &hKey);
  if (res != ERROR_SUCCESS) {
    if (res == ERROR_FILE_NOT_FOUND) {
      // 该键不存在，将使用默认值。
      return serverHistory;
    }

    throw core::win32_error(_("Failed to open registry key"), res);
  }

  unsigned index;
  const DWORD buffersize = 256;
  char indexString[3];

  for (index = 0;;index++) {
    snprintf(indexString, 3, "%d", index);
    char servernameBuffer[buffersize];

    try {
      if (!getKeyString(indexString, servernameBuffer,
                        buffersize, &hKey))
        break;
    } catch (std::exception& e) {
      // 忽略这一项，继续尝试下一项
      vlog.error(_("Failed to read server history entry %d: %s"),
                 (int)index, e.what());
      continue;
    }

    serverHistory.push_back(servernameBuffer);
  }

  res = RegCloseKey(hKey);
  if (res != ERROR_SUCCESS)
    throw core::win32_error(_("Failed to close registry key"), res);

  return serverHistory;
}

static void getParametersFromReg(core::Parameter* parameters[],
                                 size_t parameters_len, HKEY* hKey)
{
  const size_t buffersize = 256;
  int intValue = 0;
  char stringValue[buffersize];

  for (size_t i = 0; i < parameters_len; i++) {
    core::IntParameter* iparam;
    core::BoolParameter* bparam;

    iparam = dynamic_cast<core::IntParameter*>(parameters[i]);
    bparam = dynamic_cast<core::BoolParameter*>(parameters[i]);

    try {
      if (iparam != nullptr) {
        if (getKeyInt(iparam->getName(), &intValue, hKey))
          iparam->setParam(intValue);
      } else if (bparam != nullptr) {
        if (getKeyInt(bparam->getName(), &intValue, hKey))
          bparam->setParam(intValue);
      } else {
        if (getKeyString(parameters[i]->getName(), stringValue, buffersize, hKey))
          parameters[i]->setParam(stringValue);
      }
    } catch(std::exception& e) {
      // 忽略这一项，继续处理其余项
      vlog.error(_("Failed to read parameter \"%s\": %s"),
                 parameters[i]->getName(), e.what());
    }
  }
}

static std::string loadFromReg() {

  HKEY hKey;

  LONG res = RegOpenKeyExW(HKEY_CURRENT_USER,
                           REGISTRY_BASE_PATH, 0,
                           KEY_READ, &hKey);
  if (res != ERROR_SUCCESS) {
    if (res == ERROR_FILE_NOT_FOUND) {
      // 该键不存在，将使用默认值。
      return "";
    }

    throw core::win32_error(_("Failed to open registry key"), res);
  }

  const size_t buffersize = 256;
  char servername[buffersize];

  try {
    getKeyString("ServerName", servername, buffersize, &hKey);
  } catch(std::exception& e) {
    vlog.error(_("Failed to read parameter \"%s\": %s"),
               "ServerName", e.what());
    strcpy(servername, "");
  }

  getParametersFromReg(parameterArray,
                       sizeof(parameterArray) /
                         sizeof(core::Parameter*),
                       &hKey);
  getParametersFromReg(readOnlyParameterArray,
                       sizeof(readOnlyParameterArray) /
                         sizeof(core::Parameter*),
                       &hKey);

  res = RegCloseKey(hKey);
  if (res != ERROR_SUCCESS)
    throw core::win32_error(_("Failed to close registry key"), res);

  migrateDeprecatedOptions();

  return servername;
}
#endif // _WIN32


void saveViewerParameters(const char *filename, const char *servername) {

  const size_t buffersize = 256;
  char filepath[PATH_MAX];
  char encodingBuffer[buffersize];

  // 如果未指定文件名，则写入注册表或预定义文件。
  if(filename == nullptr) {

#ifdef _WIN32
    saveToReg(servername);
    return;
#endif
    
    const char* configDir = core::getvncconfigdir();
    if (configDir == nullptr)
      throw std::runtime_error(_("Could not determine VNC config directory path"));

    snprintf(filepath, sizeof(filepath), "%s/default.tigervnc", configDir);
  } else {
    snprintf(filepath, sizeof(filepath), "%s", filename);
  }

  /* 将参数写入文件 */
  FILE* f = fopen(filepath, "w+");
  if (!f)
    throw core::posix_error(
      core::format(_("Failed to open \"%s\""), filepath), errno);

  fprintf(f, "%s\n", IDENTIFIER_STRING);
  fprintf(f, "\n");

  if (!encodeValue(servername, encodingBuffer, buffersize)) {
    fclose(f);
    throw std::runtime_error(
      core::format(_("Failed to save \"%s\": %s"), "ServerName",
                   _("Could not encode parameter")));
  }
  fprintf(f, "ServerName=%s\n", encodingBuffer);

  for (core::Parameter* param : parameterArray) {
    if (param->isDefault())
      continue;
    if (!encodeValue(param->getValueStr().c_str(),
                     encodingBuffer, buffersize)) {
      fclose(f);
      throw std::runtime_error(
        core::format(_("Failed to save \"%s\": %s"), param->getName(),
                     _("Could not encode parameter")));
    }
    fprintf(f, "%s=%s\n", param->getName(), encodingBuffer);
  }
  fclose(f);
}

static bool findAndSetViewerParameterFromValue(
  core::Parameter* parameters[], size_t parameters_len,
  char* value, char* line)
{
  const size_t buffersize = 256;
  char decodingBuffer[buffersize];

  // 查找并设置正确的参数
  for (size_t i = 0; i < parameters_len; i++) {
    if (strcasecmp(line, parameters[i]->getName()) == 0) {
      if(!decodeValue(value, decodingBuffer, sizeof(decodingBuffer)))
        throw std::runtime_error(_("Invalid format or too large value"));
      parameters[i]->setParam(decodingBuffer);
      return false;
    }
  }

  return true;
}

std::string loadViewerParameters(const char *filename) {

  const size_t buffersize = 256;
  char filepath[PATH_MAX];
  char line[buffersize];
  char decodingBuffer[buffersize];
  char servername[sizeof(line)];

  memset(servername, '\0', sizeof(servername));

  // 如果未指定文件名，则从注册表或预定义文件加载。
  if(filename == nullptr) {

#ifdef _WIN32
    return loadFromReg();
#endif

    const char* configDir = core::getvncconfigdir();
    if (configDir == nullptr)
      throw std::runtime_error(_("Could not determine VNC config directory path"));

    snprintf(filepath, sizeof(filepath), "%s/default.tigervnc", configDir);
  } else {
    snprintf(filepath, sizeof(filepath), "%s", filename);
  }

  /* 从文件读取参数 */
  FILE* f = fopen(filepath, "r");
  if (!f) {
    if (!filename)
      return ""; // 使用默认值。
    throw core::posix_error(
      core::format(_("Failed to open \"%s\""), filepath), errno);
  }

  int lineNr = 0;
  while (!feof(f)) {

    // 读取下一行
    lineNr++;
    if (!fgets(line, sizeof(line), f)) {
      if (feof(f))
        break;

      fclose(f);
      throw core::posix_error(
        core::format(_("Failed to read line %d in file \"%s\""),
                     lineNr, filepath),
        errno);
    }

    if (strlen(line) == (sizeof(line) - 1)) {
      fclose(f);
      std::string msg = core::format(_("Failed to read line %d in "
                                       "file \"%s\""),
                                     lineNr, filepath);
      throw std::runtime_error(
        core::format("%s: %s", msg.c_str(), _("Line too long")));
    }

    // 确保文件第一行包含文件标识字符串
    if(lineNr == 1) {
      if(strncmp(line, IDENTIFIER_STRING, strlen(IDENTIFIER_STRING)) == 0)
        continue;

      fclose(f);
      throw std::runtime_error(core::format(
        _("Configuration file %s is in an invalid format"), filepath));
    }

    // 跳过空行和注释
    if ((line[0] == '\n') || (line[0] == '#') || (line[0] == '\r'))
      continue;
    // 忽略文件中多余的 NULL
    if (line[0] == '\0')
      continue;

    int len = strlen(line);
    if (line[len-1] == '\n') {
      line[len-1] = '\0';
      len--;
    }
    if (line[len-1] == '\r') {
      line[len-1] = '\0';
      len--;
    }

    // 查找参数值
    char *value = strchr(line, '=');
    if (value == nullptr) {
      std::string msg = core::format(_("Failed to read line %d in "
                                       "file \"%s\""),
                                     lineNr, filepath);
      vlog.error("%s: %s", msg.c_str(), _("Invalid format"));
      continue;
    }
    *value = '\0'; // 此时 line 中只剩参数名。
    value++;
    
    bool invalidParameterName = true; // 如果该行包含有效名称，
                                      // 下面会将其置为 false。

    try {
      if (strcasecmp(line, "ServerName") == 0) {

        if(!decodeValue(value, decodingBuffer, sizeof(decodingBuffer)))
          throw std::runtime_error(_("Invalid format or too large value"));
        snprintf(servername, sizeof(decodingBuffer), "%s", decodingBuffer);
        invalidParameterName = false;

      } else {
        invalidParameterName = findAndSetViewerParameterFromValue(
          parameterArray,
          sizeof(parameterArray) / sizeof(core::Parameter *),
          value, line);

        if (invalidParameterName) {
          invalidParameterName = findAndSetViewerParameterFromValue(
            readOnlyParameterArray,
            sizeof(readOnlyParameterArray) /
              sizeof(core::Parameter *),
            value, line);
        }
      }
    } catch(std::exception& e) {
      // 忽略这一项，继续处理其余项
      std::string msg = core::format(_("Failed to read line %d in "
                                       "file \"%s\""),
                                     lineNr, filepath);
      vlog.error("%s: %s", msg.c_str(), e.what());
      continue;
    }

    if (invalidParameterName) {
      std::string msg = core::format(_("Failed to read line %d in "
                                       "file \"%s\""),
                                     lineNr, filepath);
      vlog.error("%s: %s", msg.c_str(), _("Unknown parameter"));
    }
  }
  fclose(f);
  f = nullptr;

  migrateDeprecatedOptions();

  return servername;
}

void migrateDeprecatedOptions()
{
  if (fullScreenAllMonitors) {
    vlog.info(_("FullScreenAllMonitors is deprecated, set FullScreenMode to 'all' instead"));

    fullScreenMode.setParam("all");
  }
  if (dotWhenNoCursor) {
    vlog.info(_("DotWhenNoCursor is deprecated, set AlwaysCursor to 1 and CursorType to 'Dot' instead"));

    alwaysCursor.setParam(true);
    cursorType.setParam("Dot");
  }
}
