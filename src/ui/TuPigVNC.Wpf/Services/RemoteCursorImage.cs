using System.Buffers.Binary;
using System.IO;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 把服务器发来的预乘 RGBA 光标位图转成 WPF 能用的 <c>.cur</c> 流。
///
/// WPF 的 <c>System.Windows.Input.Cursor</c> 只接受文件流或资源名，
/// 没有"从位图构造"的重载，所以这里按 Windows 的 CUR 格式手工拼一份：
/// 图标目录（ICONDIR）→ 目录项（ICONDIRENTRY，带上热点）→
/// 32 位 DIB（高度写成两倍，表示 XOR 图 + AND 掩码）→ 像素（自下而上，BGRA）→
/// 1 位 AND 掩码（全 0，透明度完全交给 alpha 通道）。
///
/// 纯托管实现，不 P/Invoke、不落盘，失败只会抛异常由调用方退回默认箭头。
/// </summary>
internal static class RemoteCursorImage
{
    private const int IconDirSize = 6;
    private const int IconDirEntrySize = 16;
    private const int BitmapInfoHeaderSize = 40;

    /// <summary>构造一个可交给 <c>new Cursor(stream)</c> 的内存流。</summary>
    public static Stream BuildCursorStream(int width, int height, int hotX, int hotY, byte[] rgba)
    {
        ArgumentOutOfRangeException.ThrowIfLessThan(width, 1);
        ArgumentOutOfRangeException.ThrowIfLessThan(height, 1);
        if (width > 256 || height > 256)
            throw new ArgumentOutOfRangeException(nameof(width), "光标尺寸超出 CUR 格式上限（256）");
        if (rgba.Length < (long)width * height * 4)
            throw new ArgumentException("光标位图数据不足", nameof(rgba));

        int xorStride = width * 4;
        // AND 掩码每行按 4 字节对齐。
        int andStride = ((width + 31) / 32) * 4;
        int imageSize = BitmapInfoHeaderSize + xorStride * height + andStride * height;

        // 不能 using：这个流要交给 Cursor 去读，方法返回时它必须还开着。
        var memory = new MemoryStream(IconDirSize + IconDirEntrySize + imageSize);

        // ---- ICONDIR ----
        WriteUInt16(memory, 0);            // reserved
        WriteUInt16(memory, 2);            // type = cursor
        WriteUInt16(memory, 1);            // count

        // ---- ICONDIRENTRY ----
        memory.WriteByte((byte)(width >= 256 ? 0 : width));
        memory.WriteByte((byte)(height >= 256 ? 0 : height));
        memory.WriteByte(0);               // 调色板颜色数，真彩为 0
        memory.WriteByte(0);               // reserved
        WriteUInt16(memory, (ushort)Math.Clamp(hotX, 0, width - 1));
        WriteUInt16(memory, (ushort)Math.Clamp(hotY, 0, height - 1));
        WriteUInt32(memory, (uint)imageSize);
        WriteUInt32(memory, IconDirSize + IconDirEntrySize);

        // ---- BITMAPINFOHEADER ----
        WriteUInt32(memory, BitmapInfoHeaderSize);
        WriteUInt32(memory, (uint)width);
        WriteUInt32(memory, (uint)(height * 2));   // XOR + AND 两张图
        WriteUInt16(memory, 1);                    // planes
        WriteUInt16(memory, 32);                   // 位深
        WriteUInt32(memory, 0);                    // 无压缩
        WriteUInt32(memory, (uint)(xorStride * height + andStride * height));
        WriteUInt32(memory, 0);                    // 水平 DPI 由系统决定
        WriteUInt32(memory, 0);
        WriteUInt32(memory, 0);
        WriteUInt32(memory, 0);

        // ---- XOR 位图：DIB 是自下而上，源数据是自上而下 ----
        for (int y = height - 1; y >= 0; y--)
            memory.Write(rgba, y * xorStride, xorStride);

        // ---- AND 掩码：全 0 表示"按 alpha 通道走" ----
        for (int y = 0; y < height; y++)
        {
            memory.WriteByte(0);
            // 每行补齐到 andStride
            for (int i = 1; i < andStride; i++) memory.WriteByte(0);
        }

        memory.Position = 0;
        return memory;
    }

    private static void WriteUInt16(Stream stream, ushort value)
    {
        Span<byte> buffer = stackalloc byte[2];
        BinaryPrimitives.WriteUInt16LittleEndian(buffer, value);
        stream.Write(buffer);
    }

    private static void WriteUInt32(Stream stream, uint value)
    {
        Span<byte> buffer = stackalloc byte[4];
        BinaryPrimitives.WriteUInt32LittleEndian(buffer, value);
        stream.Write(buffer);
    }
}
