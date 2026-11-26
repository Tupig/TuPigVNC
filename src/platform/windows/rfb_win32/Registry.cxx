/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
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

// -=- Registry.cxx

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <core/LogWriter.h>
#include <core/string.h>

#include <rfb_win32/Registry.h>
#include <rfb_win32/Security.h>

#include <rdr/MemOutStream.h>
#include <rdr/HexOutStream.h>
#include <rdr/HexInStream.h>

#include <stdlib.h>

// 控制访问控制继承需要用到这些标志，但 VC6 的头文件里没有定义。
// 这些定义来自 Microsoft Platform SDK。
#ifndef PROTECTED_DACL_SECURITY_INFORMATION
#define PROTECTED_DACL_SECURITY_INFORMATION     (0x80000000L)
#endif
#ifndef UNPROTECTED_DACL_SECURITY_INFORMATION
#define UNPROTECTED_DACL_SECURITY_INFORMATION     (0x20000000L)
#endif


using namespace core;
using namespace rfb;
using namespace rfb::win32;


static LogWriter vlog("Registry");


RegKey::RegKey() : key(nullptr), freeKey(false) {}

RegKey::RegKey(const HKEY k) : key(nullptr), freeKey(false) {
  LONG result = RegOpenKeyEx(k, nullptr, 0, KEY_ALL_ACCESS, &key);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegOpenKeyEx(HKEY)", result);
  vlog.debug("Duplicated %p to %p", k, key);
  freeKey = true;
}

RegKey::RegKey(const RegKey& k) : key(nullptr), freeKey(false) {
  LONG result = RegOpenKeyEx(k.key, nullptr, 0, KEY_ALL_ACCESS, &key);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegOpenKeyEx(RegKey&)", result);
  vlog.debug("Duplicated %p to %p", k.key, key);
  freeKey = true;
}

RegKey::~RegKey() {
  close();
}


void RegKey::setHKEY(HKEY k, bool fK) {
  vlog.debug("setHKEY(%p,%d)", k, (int)fK);
  close();
  freeKey = fK;
  key = k;
}


bool RegKey::createKey(const RegKey& root, const char* name) {
  close();
  LONG result = RegCreateKey(root.key, name, &key);
  if (result != ERROR_SUCCESS) {
    vlog.error("RegCreateKey(%p, %s): %lx", root.key, name, result);
    throw core::win32_error("RegCreateKeyEx", result);
  }
  vlog.debug("createKey(%p,%s) = %p", root.key, name, key);
  freeKey = true;
  return true;
}

void RegKey::openKey(const RegKey& root, const char* name, bool readOnly) {
  close();
  LONG result = RegOpenKeyEx(root.key, name, 0, readOnly ? KEY_READ : KEY_ALL_ACCESS, &key);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegOpenKeyEx (open)", result);
  vlog.debug("openKey(%p,%s,%s) = %p", root.key, name,
	         readOnly ? "ro" : "rw", key);
  freeKey = true;
}

void RegKey::setDACL(const PACL acl, bool inherit) {
  DWORD result;
  if ((result = SetSecurityInfo(key, SE_REGISTRY_KEY,
    DACL_SECURITY_INFORMATION |
    (inherit ? UNPROTECTED_DACL_SECURITY_INFORMATION : PROTECTED_DACL_SECURITY_INFORMATION),
    nullptr, nullptr, acl, nullptr)) != ERROR_SUCCESS)
    throw core::win32_error("RegKey::setDACL failed", result);
}

void RegKey::close() {
  if (freeKey) {
    vlog.debug("RegCloseKey(%p)", key);
    RegCloseKey(key);
    key = nullptr;
  }
}

void RegKey::deleteKey(const char* name) const {
  LONG result = RegDeleteKey(key, name);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegDeleteKey", result);
}

void RegKey::deleteValue(const char* name) const {
  LONG result = RegDeleteValue(key, name);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegDeleteValue", result);
}

void RegKey::awaitChange(bool watchSubTree, DWORD filter, HANDLE event) const {
  LONG result = RegNotifyChangeKeyValue(key, watchSubTree, filter, event, event != nullptr);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegNotifyChangeKeyValue", result);
}


RegKey::operator HKEY() const {return key;}


void RegKey::setExpandString(const char* valname, const char* value) const {
  LONG result = RegSetValueEx(key, valname, 0, REG_EXPAND_SZ, (const BYTE*)value, (strlen(value)+1)*sizeof(char));
  if (result != ERROR_SUCCESS) throw core::win32_error("setExpandString", result);
}

void RegKey::setString(const char* valname, const char* value) const {
  LONG result = RegSetValueEx(key, valname, 0, REG_SZ, (const BYTE*)value, (strlen(value)+1)*sizeof(char));
  if (result != ERROR_SUCCESS) throw core::win32_error("setString", result);
}

void RegKey::setBinary(const char* valname, const void* value, size_t length) const {
  LONG result = RegSetValueEx(key, valname, 0, REG_BINARY, (const BYTE*)value, length);
  if (result != ERROR_SUCCESS) throw core::win32_error("setBinary", result);
}

void RegKey::setInt(const char* valname, int value) const {
  LONG result = RegSetValueEx(key, valname, 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
  if (result != ERROR_SUCCESS) throw core::win32_error("setInt", result);
}

void RegKey::setBool(const char* valname, bool value) const {
  setInt(valname, value ? 1 : 0);
}

std::string RegKey::getString(const char* valname) const {
  return getRepresentation(valname);
}

std::string RegKey::getString(const char* valname, const char* def) const {
  try {
    return getString(valname);
  } catch(std::exception&) {
    return def;
  }
}

std::vector<uint8_t> RegKey::getBinary(const char* valname) const {
  std::string hex = getRepresentation(valname);
  return hexToBin(hex.data(), hex.size());
}
std::vector<uint8_t> RegKey::getBinary(const char* valname, const uint8_t* def, size_t deflen) const {
  try {
    return getBinary(valname);
  } catch(std::exception&) {
    std::vector<uint8_t> out(deflen);
    memcpy(out.data(), def, deflen);
    return out;
  }
}

int RegKey::getInt(const char* valname) const {
  return atoi(getRepresentation(valname).c_str());
}
int RegKey::getInt(const char* valname, int def) const {
  try {
    return getInt(valname);
  } catch(std::exception&) {
    return def;
  }
}

bool RegKey::getBool(const char* valname) const {
  return getInt(valname) > 0;
}
bool RegKey::getBool(const char* valname, bool def) const {
  return getInt(valname, def ? 1 : 0) > 0;
}

std::string RegKey::getRepresentation(const char* valname) const {
  DWORD type, length;
  LONG result = RegQueryValueEx(key, valname, nullptr, &type, nullptr, &length);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("Get registry value length", result);
  std::vector<uint8_t> data(length);
  result = RegQueryValueEx(key, valname, nullptr, &type, (BYTE*)data.data(), &length);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("Get registry value", result);

  switch (type) {
  case REG_BINARY:
    {
      return binToHex(data.data(), length);
    }
  case REG_SZ:
    if (length) {
      return std::string((char*)data.data(), length);
    } else {
      return "";
    }
  case REG_DWORD:
    {
      char tmp[16];
      sprintf(tmp, "%lu", *((DWORD*)data.data()));
      return tmp;
    }
  case REG_EXPAND_SZ:
    {
    if (length) {
      std::string str((char*)data.data(), length);
      DWORD required = ExpandEnvironmentStrings(str.c_str(), nullptr, 0);
      if (required==0)
        throw core::win32_error("ExpandEnvironmentStrings", GetLastError());
      std::vector<char> expanded(required);
      length = ExpandEnvironmentStrings(str.c_str(), expanded.data(), required);
      if (required<length)
        throw std::runtime_error("Unable to expand environment strings");
      return expanded.data();
    } else {
      return "";
    }
    }
  default:
    throw std::logic_error("Unsupported registry type");
  }
}

bool RegKey::isValue(const char* valname) const {
  try {
    getRepresentation(valname);
    return true;
  } catch(std::exception&) {
    return false;
  }
}

const char* RegKey::getValueName(int i) {
  DWORD maxValueNameLen;
  LONG result = RegQueryInfoKey(key, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &maxValueNameLen, nullptr, nullptr, nullptr);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegQueryInfoKey", result);
  if (valueName.size() < (size_t)(maxValueNameLen + 1))
    valueName.resize(maxValueNameLen + 1);
  DWORD length = (DWORD)valueName.size();
  result = RegEnumValue(key, i, valueName.data(), &length,
                        nullptr, nullptr, nullptr, nullptr);
  if (result == ERROR_NO_MORE_ITEMS) return nullptr;
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegEnumValue", result);
  return valueName.data();
}

const char* RegKey::getKeyName(int i) {
  DWORD maxValueNameLen;
  LONG result = RegQueryInfoKey(key, nullptr, nullptr, nullptr, nullptr, &maxValueNameLen, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegQueryInfoKey", result);
  if (valueName.size() < (size_t)(maxValueNameLen + 1))
    valueName.resize(maxValueNameLen + 1);
  DWORD length = (DWORD)valueName.size();
  result = RegEnumKeyEx(key, i, valueName.data(), &length,
                        nullptr, nullptr, nullptr, nullptr);
  if (result == ERROR_NO_MORE_ITEMS) return nullptr;
  if (result != ERROR_SUCCESS)
    throw core::win32_error("RegEnumKey", result);
  return valueName.data();
}
