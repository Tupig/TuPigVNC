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

/*
 * 从 keysymdef.h 生成 keyucsmap.h。
 *
 * 用法：
 *   genkeyuscmap [keysymdef.h 路径]
 *
 * 未指定路径时默认读取 ../common/rfb/keysymdef.h。
 * 生成结果输出到标准输出。
 */

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <map>
#include <string>

struct Entry {
    std::string name;
    unsigned int ucs;
    std::string ucsname;
};

static const char* skipSpace(const char* p)
{
    while (*p && std::isspace(static_cast<unsigned char>(*p)))
        ++p;
    return p;
}

/* 把 Unicode 码点编码为 UTF-8 字符串 */
static std::string utf8Encode(unsigned int ucs)
{
    std::string out;
    if (ucs < 0x80) {
        out += static_cast<char>(ucs);
    } else if (ucs < 0x800) {
        out += static_cast<char>(0xC0 | (ucs >> 6));
        out += static_cast<char>(0x80 | (ucs & 0x3F));
    } else if (ucs < 0x10000) {
        out += static_cast<char>(0xE0 | (ucs >> 12));
        out += static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (ucs & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (ucs >> 18));
        out += static_cast<char>(0x80 | ((ucs >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((ucs >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (ucs & 0x3F));
    }
    return out;
}

int main(int argc, char** argv)
{
    const char* filename = argc > 1 ? argv[1]
                                    : "../common/rfb/keysymdef.h";

    FILE* file = std::fopen(filename, "r");
    if (file == nullptr) {
        std::fprintf(stderr, "无法打开 %s\n", filename);
        return 1;
    }

    std::map<unsigned int, Entry> keys;

    char line[4096];
    while (std::fgets(line, sizeof(line), file) != nullptr) {
        const char* p = std::strstr(line, "#define XK_");
        if (p == nullptr)
            continue;
        p += std::strlen("#define XK_");

        const char* nameEnd = p;
        while (*nameEnd && !std::isspace(static_cast<unsigned char>(*nameEnd)))
            ++nameEnd;
        std::string name(p, nameEnd);

        const char* num = skipSpace(nameEnd);
        if (num[0] != '0' || (num[1] != 'x' && num[1] != 'X'))
            continue;
        unsigned int keysym = static_cast<unsigned int>(std::strtoul(num, nullptr, 16));

        const char* comment = std::strstr(num, "/*");
        if (comment == nullptr)
            continue;

        /* 注释可能形如 "U+0041 ..." 或 "(U+250C ..."，
         * 因此直接在注释中查找 "U+"。 */
        const char* u = std::strstr(comment, "U+");
        if (u == nullptr)
            continue;

        const char* ucsStart = u + 2;
        unsigned int ucs = static_cast<unsigned int>(std::strtoul(ucsStart, nullptr, 16));

        const char* ucsNameStart = ucsStart;
        while (*ucsNameStart &&
               !std::isspace(static_cast<unsigned char>(*ucsNameStart)))
            ++ucsNameStart;
        ucsNameStart = skipSpace(ucsNameStart);

        const char* ucsNameEnd = std::strstr(ucsNameStart, "*/");
        if (ucsNameEnd == nullptr)
            continue;
        std::string ucsname(ucsNameStart, ucsNameEnd);
        /* 形如 "(U+250C NAME)" 或 "U+0020 NAME>" 的注释会残留 ')' 或 '>' */
        while (!ucsname.empty() &&
               (std::isspace(static_cast<unsigned char>(ucsname.back())) ||
                ucsname.back() == ')' || ucsname.back() == '>'))
            ucsname.pop_back();

        /* 过滤：与 Unicode 相同的 ASCII / Latin-1 键符无需映射 */
        if (keysym == ucs &&
            ((keysym >= 0x20 && keysym <= 0x7f) ||
             (keysym >= 0xa0 && keysym <= 0xff)))
            continue;
        if ((keysym & 0xff000000) == 0x01000000) {
            if (keysym != (ucs | 0x01000000)) {
                std::fprintf(stderr, "键符 0x%x 与 Unicode 0x%x 不匹配\n",
                             keysym, ucs);
                std::fclose(file);
                return 1;
            }
            continue;
        }

        if (keys.find(keysym) != keys.end()) {
            std::fprintf(stderr, "重复键符 0x%x\n", keysym);
            std::fclose(file);
            return 1;
        }

        keys[keysym] = Entry{name, ucs, ucsname};
    }

    std::fclose(file);

    size_t maxlen = 0;
    for (const auto& kv : keys)
        maxlen = std::max(maxlen, kv.second.name.size());

    std::printf("/*\n");
    std::printf(" * This file is auto-generated from keysymdef.h\n");
    std::printf(" */\n");
    std::printf("\n");
    std::printf("struct codepair {\n");
    std::printf("  unsigned short keysym;\n");
    std::printf("  unsigned short ucs;\n");
    std::printf("};\n");
    std::printf("\n");
    std::printf("static const struct codepair keysymtab[] = {\n");

    for (const auto& kv : keys) {
        const Entry& entry = kv.second;
        std::string ch = (entry.ucs < 0x20 || entry.ucs == 0x7f)
                             ? std::string(" ")
                             : utf8Encode(entry.ucs);
        std::printf("  { 0x%04x, 0x%04x }, /* %*s %s %s */\n",
                    kv.first, entry.ucs, static_cast<int>(maxlen),
                    entry.name.c_str(), ch.c_str(), entry.ucsname.c_str());
    }

    std::printf("};\n");
    return 0;
}
