using System.Security.Cryptography;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 证书 / 主机公钥指纹的展示格式。
/// 只负责把字节算成可读文本，本身不做信任判断——是否接受由用户决定。
/// </summary>
internal static class TrustFingerprint
{
    /// <summary>
    /// 计算 SHA-256 指纹，输出常见的大写冒号分隔十六进制，
    /// 例如 3A:F2:...，方便和服务器管理员核对。
    /// </summary>
    public static string ComputeSha256(ReadOnlySpan<byte> data)
    {
        if (data.IsEmpty)
            return "（空指纹）";

        Span<byte> hash = stackalloc byte[32];
        SHA256.HashData(data, hash);

        // "AA:" * 31 + "AA"
        return string.Create(hash.Length * 3 - 1, hash.ToArray(), static (span, bytes) =>
        {
            const string digits = "0123456789ABCDEF";
            int index = 0;
            for (int i = 0; i < bytes.Length; i++)
            {
                if (i > 0) span[index++] = ':';
                span[index++] = digits[bytes[i] >> 4];
                span[index++] = digits[bytes[i] & 0x0F];
            }
        });
    }
}
