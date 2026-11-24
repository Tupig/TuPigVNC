/* Copyright (C) 2026 TuPigVNC Team
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

#include <core/UpdateChecker.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <vector>

namespace core {

namespace detail {
/* 平台相关的 HTTP GET 实现，返回响应正文 */
bool httpGet(const std::string& url, std::string* response,
             std::string* error);
}

const char* UpdateChecker::REPO_OWNER = "TuPig";
const char* UpdateChecker::REPO_NAME = "TuPigVNC";
const char* UpdateChecker::API_URL =
    "https://api.github.com/repos/TuPig/TuPigVNC/releases/latest";

namespace {

/* ------------------------------------------------------------------ */
/* 轻量 JSON 解析器（仅支持对象、数组、字符串、数字、布尔、null）      */
/* ------------------------------------------------------------------ */

struct JsonValue;

using JsonObject = std::map<std::string, JsonValue>;
using JsonArray = std::vector<JsonValue>;

struct JsonValue {
    enum Type { Null, Bool, Number, String, Object, Array } type = Null;
    bool boolean = false;
    double number = 0;
    std::string string;
    JsonObject object;
    JsonArray array;

    const JsonValue* find(const std::string& key) const {
        if (type != Object)
            return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    std::string asString() const {
        return type == String ? string : std::string();
    }

    long long asInteger() const {
        return type == Number ? static_cast<long long>(number) : 0;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : text_(text), pos_(0) {}

    bool parse(JsonValue* out) {
        skipWhitespace();
        if (!parseValue(out))
            return false;
        skipWhitespace();
        return pos_ >= text_.size();
    }

private:
    const std::string& text_;
    size_t pos_;

    void skipWhitespace() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool parseValue(JsonValue* out) {
        skipWhitespace();
        if (pos_ >= text_.size())
            return false;

        char c = text_[pos_];
        if (c == '{')
            return parseObject(out);
        if (c == '[')
            return parseArray(out);
        if (c == '"') {
            out->type = JsonValue::String;
            return parseString(&out->string);
        }
        if (c == 't' || c == 'f')
            return parseBool(out);
        if (c == 'n')
            return parseNull(out);
        return parseNumber(out);
    }

    bool parseObject(JsonValue* out) {
        out->type = JsonValue::Object;
        if (!consume('{'))
            return false;
        skipWhitespace();
        if (consume('}'))
            return true;
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(&key))
                return false;
            skipWhitespace();
            if (!consume(':'))
                return false;
            JsonValue value;
            if (!parseValue(&value))
                return false;
            out->object[key] = std::move(value);
            skipWhitespace();
            if (consume(','))
                continue;
            if (consume('}'))
                return true;
            return false;
        }
    }

    bool parseArray(JsonValue* out) {
        out->type = JsonValue::Array;
        if (!consume('['))
            return false;
        skipWhitespace();
        if (consume(']'))
            return true;
        while (true) {
            JsonValue value;
            if (!parseValue(&value))
                return false;
            out->array.push_back(std::move(value));
            skipWhitespace();
            if (consume(','))
                continue;
            if (consume(']'))
                return true;
            return false;
        }
    }

    bool parseString(std::string* out) {
        if (!consume('"'))
            return false;
        out->clear();
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out->push_back(c);
                continue;
            }
            if (pos_ >= text_.size())
                return false;
            char esc = text_[pos_++];
            switch (esc) {
            case '"': out->push_back('"'); break;
            case '\\': out->push_back('\\'); break;
            case '/': out->push_back('/'); break;
            case 'b': out->push_back('\b'); break;
            case 'f': out->push_back('\f'); break;
            case 'n': out->push_back('\n'); break;
            case 'r': out->push_back('\r'); break;
            case 't': out->push_back('\t'); break;
            case 'u': {
                if (pos_ + 4 > text_.size())
                    return false;
                unsigned code = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = text_[pos_++];
                    code <<= 4;
                    if (h >= '0' && h <= '9')
                        code |= h - '0';
                    else if (h >= 'a' && h <= 'f')
                        code |= h - 'a' + 10;
                    else if (h >= 'A' && h <= 'F')
                        code |= h - 'A' + 10;
                    else
                        return false;
                }
                /* UTF-8 编码 */
                if (code < 0x80) {
                    out->push_back(static_cast<char>(code));
                } else if (code < 0x800) {
                    out->push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                } else {
                    out->push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                    out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                }
                break;
            }
            default:
                return false;
            }
        }
        return false;
    }

    bool parseNumber(JsonValue* out) {
        size_t start = pos_;
        while (pos_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[pos_])) ||
                text_[pos_] == '-' || text_[pos_] == '+' ||
                text_[pos_] == '.' || text_[pos_] == 'e' ||
                text_[pos_] == 'E'))
            ++pos_;
        if (start == pos_)
            return false;
        out->type = JsonValue::Number;
        out->number = std::strtod(text_.substr(start, pos_ - start).c_str(),
                                  nullptr);
        return true;
    }

    bool parseBool(JsonValue* out) {
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out->type = JsonValue::Bool;
            out->boolean = true;
            return true;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out->type = JsonValue::Bool;
            out->boolean = false;
            return true;
        }
        return false;
    }

    bool parseNull(JsonValue* out) {
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out->type = JsonValue::Null;
            return true;
        }
        return false;
    }
};

/* 去掉前导 'v' 或 'V' */
std::string stripPrefix(const std::string& version) {
    if (!version.empty() && (version[0] == 'v' || version[0] == 'V'))
        return version.substr(1);
    return version;
}

/* 解析版本号为数字段，忽略预发布后缀 */
std::vector<long long> parseVersionParts(const std::string& version) {
    std::vector<long long> parts;
    std::string core = stripPrefix(version);

    /* 截断预发布后缀 */
    size_t dash = core.find('-');
    if (dash != std::string::npos)
        core = core.substr(0, dash);

    std::stringstream ss(core);
    std::string item;
    while (std::getline(ss, item, '.')) {
        if (item.empty())
            break;
        char* end = nullptr;
        long long value = std::strtoll(item.c_str(), &end, 10);
        if (end == item.c_str())
            break;
        parts.push_back(value);
    }
    return parts;
}

} // anonymous namespace

int UpdateChecker::compareVersions(const std::string& a,
                                  const std::string& b) {
    std::vector<long long> pa = parseVersionParts(a);
    std::vector<long long> pb = parseVersionParts(b);

    size_t count = pa.size() > pb.size() ? pa.size() : pb.size();
    for (size_t i = 0; i < count; ++i) {
        long long va = i < pa.size() ? pa[i] : 0;
        long long vb = i < pb.size() ? pb[i] : 0;
        if (va < vb)
            return -1;
        if (va > vb)
            return 1;
    }
    return 0;
}

bool UpdateChecker::isNewer(const std::string& remote,
                            const std::string& current) {
    return compareVersions(remote, current) > 0;
}

std::string UpdateChecker::platformKeyword() {
#if defined(_WIN32)
#  if defined(_M_X64) || defined(__x86_64__)
    return "win64";
#  else
    return "win32";
#  endif
#elif defined(__APPLE__)
    return "macos";
#else
#  if defined(__x86_64__)
    return "linux-x86_64";
#  elif defined(__aarch64__)
    return "linux-aarch64";
#  else
    return "linux";
#  endif
#endif
}

bool UpdateChecker::fetchLatestRelease(ReleaseInfo* info, std::string* error) {
    std::string json;
    if (!detail::httpGet(API_URL, &json, error))
        return false;

    JsonValue root;
    JsonParser parser(json);
    if (!parser.parse(&root)) {
        if (error)
            *error = "无法解析发行版信息";
        return false;
    }

    if (const JsonValue* tag = root.find("tag_name"))
        info->version = tag->asString();
    if (const JsonValue* name = root.find("name"))
        info->name = name->asString();
    if (const JsonValue* body = root.find("body"))
        info->notes = body->asString();
    if (const JsonValue* html = root.find("html_url"))
        info->htmlUrl = html->asString();
    if (const JsonValue* date = root.find("published_at"))
        info->publishedAt = date->asString();

    /* 选择当前平台对应的安装包 */
    std::string keyword = platformKeyword();
    if (const JsonValue* assets = root.find("assets")) {
        if (assets->type == JsonValue::Array) {
            for (const JsonValue& asset : assets->array) {
                const JsonValue* name = asset.find("name");
                const JsonValue* url = asset.find("browser_download_url");
                if (!name || !url)
                    continue;
                if (name->asString().find(keyword) != std::string::npos) {
                    info->downloadUrl = url->asString();
                    if (const JsonValue* size = asset.find("size"))
                        info->size = size->asInteger();
                    break;
                }
            }
        }
    }

    if (info->version.empty()) {
        if (error)
            *error = "发行版信息缺少版本号";
        return false;
    }
    return true;
}

} // namespace core
