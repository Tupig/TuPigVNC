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
 * 为 encperf 生成确定性的原始 RFB 轨迹。
 *
 * 这些轨迹为合成的微基准，不能替代真实桌面工作负载。
 * 文件从 encperf 预期的位置开始：RFB 握手与 ServerInit 之后。
 *
 * 用法：
 *   genrfb ui output.rfb [--width 640] [--height 360] [--frames 30]
 *   genrfb motion output.rfb [--width 1280] [--height 720] [--frames 30]
 *
 * 示例：
 *   tests/perf/encperf encoding=Tight width=640 height=360 format=rgb888 out.rfb
 *
 * 可使用 encoding=Hextile、ZRLE 或 JPEG 运行同一轨迹以比较编解码器。
 * encperf 报告 CPU 时间、编码字节数与每帧编码耗时，近似表示服务器
 * 事件循环被编码占用的时间。该数值不包含网络延迟或输入队列延迟。
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <string>
#include <vector>

namespace {

void writeU8(FILE* out, uint8_t value)
{
    std::fputc(value, out);
}

void writeU16BE(FILE* out, uint16_t value)
{
    std::fputc((value >> 8) & 0xff, out);
    std::fputc(value & 0xff, out);
}

void writeU32BE(FILE* out, uint32_t value)
{
    std::fputc((value >> 24) & 0xff, out);
    std::fputc((value >> 16) & 0xff, out);
    std::fputc((value >> 8) & 0xff, out);
    std::fputc(value & 0xff, out);
}

/* TuPigVNC 以 32 位小端解析 rgb888，含一个未使用字节 */
void appendPixel(std::vector<uint8_t>& out, int red, int green, int blue)
{
    out.push_back(static_cast<uint8_t>(blue));
    out.push_back(static_cast<uint8_t>(green));
    out.push_back(static_cast<uint8_t>(red));
    out.push_back(0);
}

void writeUpdate(FILE* out, int x, int y, int width, int height,
                 const std::vector<uint8_t>& data)
{
    /* FramebufferUpdate, 一个矩形 */
    writeU8(out, 0);
    writeU8(out, 0);
    writeU16BE(out, 1);

    /* 矩形头 + Raw 编码 */
    writeU16BE(out, static_cast<uint16_t>(x));
    writeU16BE(out, static_cast<uint16_t>(y));
    writeU16BE(out, static_cast<uint16_t>(width));
    writeU16BE(out, static_cast<uint16_t>(height));
    writeU32BE(out, 0);

    std::fwrite(data.data(), 1, data.size(), out);
}

std::vector<uint8_t> desktopFrame(int width, int height)
{
    std::vector<uint8_t> data;
    data.reserve(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            appendPixel(data,
                        25 + x * 30 / width,
                        40 + y * 30 / height,
                        72);
        }
    }
    return data;
}

void uiTrace(FILE* out, int width, int height, int frames)
{
    writeUpdate(out, 0, 0, width, height, desktopFrame(width, height));

    int boxWidth = width < 320 ? width : 320;
    int boxHeight = height < 80 ? height : 80;

    for (int frame = 0; frame < frames - 1; ++frame) {
        int x = (frame * 37) % (width - boxWidth + 1 > 0
                                    ? width - boxWidth + 1
                                    : 1);
        int y = (frame * 19) % (height - boxHeight + 1 > 0
                                    ? height - boxHeight + 1
                                    : 1);

        std::vector<uint8_t> data;
        data.reserve(static_cast<size_t>(boxWidth) * boxHeight * 4);
        for (int py = 0; py < boxHeight; ++py) {
            for (int px = 0; px < boxWidth; ++px) {
                /* 重复的字形边缘与变化的鼠标块 */
                bool ink = ((px % 17) < 2) && ((py % 23) < 15);
                int span = width > 0 ? width : 1;
                int rel = ((x + px - frame * 11) % span + span) % span;
                bool cursor = rel < 12 && py < 16;

                int red = ink ? 225 : 45;
                int green;
                int blue;
                if (cursor) {
                    green = 235;
                    blue = 245;
                } else if (ink) {
                    green = 220;
                    blue = 180;
                } else {
                    green = 90;
                    blue = 130;
                }
                appendPixel(data, red, green, blue);
            }
        }
        writeUpdate(out, x, y, boxWidth, boxHeight, data);
    }
}

void motionTrace(FILE* out, int width, int height, int frames)
{
    int denom = height - 1 > 0 ? height - 1 : 1;

    for (int frame = 0; frame < frames; ++frame) {
        std::vector<uint8_t> data;
        data.reserve(static_cast<size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int band = ((x + frame * 13) / 48) % 6;
                int red = (band * 41 + (y / 8)) & 255;
                int green = (y * 255 / denom + frame * 3) & 255;
                int blue = ((x / 8) * 7 + frame * 5) & 255;
                appendPixel(data, red, green, blue);
            }
        }
        writeUpdate(out, 0, 0, width, height, data);
    }
}

void usage(const char* prog)
{
    std::fprintf(stderr,
                 "用法：%s {ui|motion} <输出文件> "
                 "[--width N] [--height N] [--frames N]\n",
                 prog);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        usage(argc > 0 ? argv[0] : "genrfb");
        return 1;
    }

    const char* workload = argv[1];
    const char* output = argv[2];
    int width = 1280;
    int height = 720;
    int frames = 30;

    if (std::strcmp(workload, "ui") != 0 &&
        std::strcmp(workload, "motion") != 0) {
        std::fprintf(stderr, "无效的工作负载：%s\n", workload);
        usage(argv[0]);
        return 1;
    }

    for (int i = 3; i < argc; ++i) {
        const char* arg = argv[i];
        if (std::strcmp(arg, "--width") == 0 && i + 1 < argc) {
            width = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--height") == 0 && i + 1 < argc) {
            height = std::atoi(argv[++i]);
        } else if (std::strcmp(arg, "--frames") == 0 && i + 1 < argc) {
            frames = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "未知参数：%s\n", arg);
            usage(argv[0]);
            return 1;
        }
    }

    if (width < 1 || width > 65535 || height < 1 || height > 65535) {
        std::fprintf(stderr, "宽度与高度必须在 1..65535 之间\n");
        return 1;
    }
    if (frames < 1) {
        std::fprintf(stderr, "帧数必须为正整数\n");
        return 1;
    }

    FILE* out = std::fopen(output, "wb");
    if (out == nullptr) {
        std::fprintf(stderr, "无法打开输出文件：%s\n", output);
        return 1;
    }

    if (std::strcmp(workload, "ui") == 0)
        uiTrace(out, width, height, frames);
    else
        motionTrace(out, width, height, frames);

    std::fclose(out);
    return 0;
}
