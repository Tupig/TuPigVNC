/* Copyright (C) 2026 TuPigVNC Team
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

#include <stdio.h>

#include <map>
#include <string>
#include <utility>

#ifdef WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

#include <core/LogWriter.h>
#include <core/Platform.h>
#include <core/i18n.h>
#include <core/xdgdirs.h>

#include "Credentials.h"

namespace
{

core::LogWriter vlog("Credentials");

/* 凭据文件名，放在 VNC 配置目录下。 */
const char* const kCredentialsFile = "credentials.json";

/* 单台服务器的凭据。 */
struct Entry
{
  std::string user;
  /* 加密后 base64 编码的密码，绝不存明文。 */
  std::string password;
};

using EntryMap = std::map<std::string, Entry>;

/* ------------------------- base64 编解码 ------------------------- */

const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int base64Value(char c)
{
  if (c >= 'A' && c <= 'Z')
    return c - 'A';
  if (c >= 'a' && c <= 'z')
    return c - 'a' + 26;
  if (c >= '0' && c <= '9')
    return c - '0' + 52;
  if (c == '+')
    return 62;
  if (c == '/')
    return 63;
  return -1;
}

std::string base64Encode(const std::string& raw)
{
  std::string out;
  out.reserve(((raw.size() + 2) / 3) * 4);

  size_t i = 0;
  while (i + 2 < raw.size())
  {
    unsigned int n = (static_cast<unsigned char>(raw[i]) << 16) |
                     (static_cast<unsigned char>(raw[i + 1]) << 8) |
                     static_cast<unsigned char>(raw[i + 2]);
    out += kBase64Chars[(n >> 18) & 0x3f];
    out += kBase64Chars[(n >> 12) & 0x3f];
    out += kBase64Chars[(n >> 6) & 0x3f];
    out += kBase64Chars[n & 0x3f];
    i += 3;
  }

  size_t remain = raw.size() - i;
  if (remain == 1)
  {
    unsigned int n = static_cast<unsigned char>(raw[i]) << 16;
    out += kBase64Chars[(n >> 18) & 0x3f];
    out += kBase64Chars[(n >> 12) & 0x3f];
    out += "==";
  }
  else if (remain == 2)
  {
    unsigned int n = (static_cast<unsigned char>(raw[i]) << 16) |
                     (static_cast<unsigned char>(raw[i + 1]) << 8);
    out += kBase64Chars[(n >> 18) & 0x3f];
    out += kBase64Chars[(n >> 12) & 0x3f];
    out += kBase64Chars[(n >> 6) & 0x3f];
    out += '=';
  }

  return out;
}

bool base64Decode(const std::string& text, std::string* raw)
{
  std::string out;
  out.reserve(text.size() / 4 * 3);

  unsigned int buffer = 0;
  int bits = 0;
  for (char c : text)
  {
    if (c == '=')
      break;
    int value = base64Value(c);
    if (value < 0)
      return false;
    buffer = (buffer << 6) | static_cast<unsigned int>(value);
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      out += static_cast<char>((buffer >> bits) & 0xff);
    }
  }

  *raw = std::move(out);
  return true;
}

/* ------------------------- 密码加解密 ------------------------- */

/*
 * Windows 上用 DPAPI 按当前用户加密，密文再 base64 后写进 JSON。
 * 非 Windows 平台没有等价的系统接口，退化为 base64（仅编码不加密）。
 */
bool encryptPassword(const std::string& plain, std::string* encoded)
{
  if (plain.empty())
  {
    encoded->clear();
    return true;
  }

#ifdef WIN32
  DATA_BLOB input;
  input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()));
  input.cbData = static_cast<DWORD>(plain.size());

  DATA_BLOB output;
  if (!CryptProtectData(&input, L"TuPigVNC", nullptr, nullptr, nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN, &output))
    return false;

  std::string cipher(reinterpret_cast<char*>(output.pbData), output.cbData);
  LocalFree(output.pbData);
  *encoded = base64Encode(cipher);
  return true;
#else
  *encoded = base64Encode(plain);
  return true;
#endif
}

bool decryptPassword(const std::string& encoded, std::string* plain)
{
  if (encoded.empty())
  {
    plain->clear();
    return true;
  }

  std::string cipher;
  if (!base64Decode(encoded, &cipher))
    return false;

#ifdef WIN32
  DATA_BLOB input;
  input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(cipher.data()));
  input.cbData = static_cast<DWORD>(cipher.size());

  DATA_BLOB output;
  if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &output))
    return false;

  plain->assign(reinterpret_cast<char*>(output.pbData), output.cbData);
  LocalFree(output.pbData);
  return true;
#else
  *plain = std::move(cipher);
  return true;
#endif
}

/* ------------------------- JSON 读写 ------------------------- */

/* 转义 JSON 字符串中的特殊字符。 */
std::string jsonEscape(const std::string& text)
{
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text)
  {
    switch (c)
    {
    case '"':
      out += "\\\"";
      break;
    case '\\':
      out += "\\\\";
      break;
    case '\n':
      out += "\\n";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\t':
      out += "\\t";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

/*
 * 极简 JSON 解析器，只认死固定 schema：
 *   { "host:port": { "user": "...", "password": "..." }, ... }
 * 无需引入第三方库。
 */
class JsonReader
{
public:
  explicit JsonReader(const std::string& text) : text_(text), pos_(0) {}

  bool parse(EntryMap* out)
  {
    out->clear();
    skipWhitespace();
    if (!parseObject(out))
      return false;
    skipWhitespace();
    return pos_ == text_.size();
  }

private:
  const std::string& text_;
  size_t pos_;

  void skipWhitespace()
  {
    while (pos_ < text_.size())
    {
      char c = text_[pos_];
      if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
        break;
      ++pos_;
    }
  }

  bool consume(char expected)
  {
    if (pos_ < text_.size() && text_[pos_] == expected)
    {
      ++pos_;
      return true;
    }
    return false;
  }

  bool parseString(std::string* out)
  {
    if (!consume('"'))
      return false;

    out->clear();
    while (pos_ < text_.size())
    {
      char c = text_[pos_++];
      if (c == '"')
        return true;
      if (c != '\\')
      {
        *out += c;
        continue;
      }

      if (pos_ >= text_.size())
        return false;
      char escape = text_[pos_++];
      switch (escape)
      {
      case '"':
        *out += '"';
        break;
      case '\\':
        *out += '\\';
        break;
      case '/':
        *out += '/';
        break;
      case 'b':
        *out += '\b';
        break;
      case 'f':
        *out += '\f';
        break;
      case 'n':
        *out += '\n';
        break;
      case 'r':
        *out += '\r';
        break;
      case 't':
        *out += '\t';
        break;
      default:
        /* 本项目只会写入上面这些转义，遇到其它字符按原样保留。 */
        *out += escape;
        break;
      }
    }

    return false;
  }

  bool parseEntry(Entry* entry)
  {
    if (!consume('{'))
      return false;

    skipWhitespace();
    if (consume('}'))
      return true;

    while (true)
    {
      std::string key;
      if (!parseString(&key))
        return false;
      skipWhitespace();
      if (!consume(':'))
        return false;
      skipWhitespace();

      std::string value;
      if (!parseString(&value))
        return false;
      if (key == "user")
        entry->user = value;
      else if (key == "password")
        entry->password = value;

      skipWhitespace();
      if (consume(','))
      {
        skipWhitespace();
        continue;
      }
      return consume('}');
    }
  }

  bool parseObject(EntryMap* out)
  {
    if (!consume('{'))
      return false;

    skipWhitespace();
    if (consume('}'))
      return true;

    while (true)
    {
      std::string host;
      if (!parseString(&host))
        return false;
      skipWhitespace();
      if (!consume(':'))
        return false;
      skipWhitespace();

      Entry entry;
      if (!parseEntry(&entry))
        return false;
      (*out)[host] = std::move(entry);

      skipWhitespace();
      if (consume(','))
      {
        skipWhitespace();
        continue;
      }
      return consume('}');
    }
  }
};

/* 把凭据表序列化成 JSON 文本。 */
std::string serialize(const EntryMap& entries)
{
  std::string out = "{\n";
  bool first = true;
  for (const auto& item : entries)
  {
    if (!first)
      out += ",\n";
    first = false;
    out += "  \"" + jsonEscape(item.first) + "\": {";
    out += "\"user\": \"" + jsonEscape(item.second.user) + "\", ";
    out += "\"password\": \"" + jsonEscape(item.second.password) + "\"}";
  }
  out += "\n}\n";
  return out;
}

/* ------------------------- 文件操作 ------------------------- */

/* 获取凭据文件完整路径；配置目录不可用时返回空串。 */
std::string credentialsPath()
{
  const char* dir = core::getvncconfigdir();
  if (dir == nullptr)
    return std::string();
  return std::string(dir) + "/" + kCredentialsFile;
}

/* 按二进制模式打开文件；失败返回 nullptr。 */
FILE* openFile(const std::string& path, const char* mode)
{
#ifdef WIN32
  FILE* fp = nullptr;
  if (_wfopen_s(&fp, core::widen(path).c_str(), core::widen(mode).c_str()) != 0)
    return nullptr;
  return fp;
#else
  return fopen(path.c_str(), mode);
#endif
}

/* 读取整个文件；文件不存在或读取失败时返回空串。 */
std::string readFile(const std::string& path)
{
  FILE* fp = openFile(path, "rb");
  if (fp == nullptr)
    return std::string();

  std::string content;
  char buffer[4096];
  size_t n;
  while ((n = fread(buffer, 1, sizeof(buffer), fp)) > 0)
    content.append(buffer, n);
  fclose(fp);
  return content;
}

void removeFile(const std::string& path)
{
#ifdef WIN32
  _wremove(core::widen(path).c_str());
#else
  remove(path.c_str());
#endif
}

/*
 * 原子写盘：先写同目录下的临时文件，再重命名覆盖目标文件，
 * 避免写到一半断电导致凭据文件损坏。
 */
bool writeFileAtomic(const std::string& path, const std::string& content)
{
  std::string tmpPath = path + ".tmp";

  FILE* fp = openFile(tmpPath, "wb");
  if (fp == nullptr)
    return false;

  bool ok = fwrite(content.data(), 1, content.size(), fp) == content.size();
  ok = (fclose(fp) == 0) && ok;
  if (!ok)
  {
    removeFile(tmpPath);
    return false;
  }

#ifdef WIN32
  if (!MoveFileExW(core::widen(tmpPath).c_str(), core::widen(path).c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
  {
    removeFile(tmpPath);
    return false;
  }
#else
  if (rename(tmpPath.c_str(), path.c_str()) != 0)
  {
    removeFile(tmpPath);
    return false;
  }
#endif

  return true;
}

/*
 * 读取并解析凭据文件。
 * 文件为空、不存在或格式错误时清空 entries 并返回 false。
 */
bool readEntries(const std::string& path, EntryMap* entries)
{
  entries->clear();

  std::string content = readFile(path);
  if (content.empty())
    return false;

  JsonReader reader(content);
  return reader.parse(entries);
}

void writeEntries(const std::string& path, const EntryMap& entries)
{
  if (!writeFileAtomic(path, serialize(entries)))
    vlog.error(_("写入凭据文件失败：%s"), path.c_str());
}

} // namespace

bool credentials::save(const std::string& host, const std::string& user,
                       const std::string& password)
{
  if (host.empty())
    return false;

  std::string path = credentialsPath();
  if (path.empty())
  {
    vlog.error(_("无法确定 VNC 配置目录，凭据未保存"));
    return false;
  }

  /* 文件损坏时 readEntries 返回 false，此时以空表覆盖重写。 */
  EntryMap entries;
  readEntries(path, &entries);

  Entry entry;
  entry.user = user;
  if (!encryptPassword(password, &entry.password))
  {
    vlog.error(_("密码加密失败，凭据未保存"));
    return false;
  }
  entries[host] = std::move(entry);

  writeEntries(path, entries);
  return true;
}

bool credentials::load(const std::string& host, std::string* user,
                       std::string* password)
{
  if (host.empty())
    return false;

  std::string path = credentialsPath();
  if (path.empty())
    return false;

  EntryMap entries;
  if (!readEntries(path, &entries))
    return false;

  auto it = entries.find(host);
  if (it == entries.end())
    return false;

  std::string plain;
  if (!decryptPassword(it->second.password, &plain))
    return false;

  if (user != nullptr)
    *user = it->second.user;
  if (password != nullptr)
    *password = std::move(plain);
  return true;
}

void credentials::forget(const std::string& host)
{
  if (host.empty())
    return;

  std::string path = credentialsPath();
  if (path.empty())
    return;

  EntryMap entries;
  if (!readEntries(path, &entries))
    return;

  if (entries.erase(host) == 0)
    return;

  writeEntries(path, entries);
}
