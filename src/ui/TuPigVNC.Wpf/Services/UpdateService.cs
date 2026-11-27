using System.Runtime.InteropServices;
using System.Text;

namespace TuPigVNC.Wpf.Services;

/// <summary>远程发行版信息。</summary>
internal sealed record ReleaseInfo(
    string Version, string Name, string PublishedAt, string HtmlUrl,
    string DownloadUrl, string ChecksumUrl, long Size, string Notes)
{
    /// <summary>安装包大小，用来显示"48.0 MB"这类文本；未知时为 0。</summary>
    public double SizeMegabytes => Size > 0 ? Size / 1024.0 / 1024.0 : 0;

    public bool HasUpdate(string currentVersion) =>
        UpdateService.CompareVersions(Version, currentVersion) > 0;
}

/// <summary>
/// 自动更新。所有网络和安装操作都是阻塞的（原生走 WinHTTP），
/// 调用方必须放到后台线程；本类只负责把 C ABI 包成托管类型。
///
/// 安全边界由原生侧保证：下载地址必须是 GitHub 官方域名，
/// 必须有 SHA-256 校验文件且校验通过才安装。缺校验文件时原生直接拒绝，
/// 托管侧不会绕过。
/// </summary>
internal static class UpdateService
{
    /// <summary>查询最新发行版。失败时抛 <see cref="UpdateException"/>。</summary>
    public static ReleaseInfo FetchLatest()
    {
        const int notesCapacity = 64 * 1024;
        IntPtr notes = Marshal.AllocHGlobal(notesCapacity);
        try
        {
            // 必须清零：原生侧只保证写满它要写的内容，不保证 NUL 终止。
            // 不清零时 PtrToStringUTF8 会读到未初始化内存，甚至越过缓冲区。
            Marshal.Copy(new byte[notesCapacity], 0, notes, notesCapacity);

            var info = new VncInterop.VncReleaseInfo
            {
                version = "", name = "", publishedAt = "", htmlUrl = "",
                downloadUrl = "", checksumUrl = "",
                notes = notes, notesCapacity = (UIntPtr)notesCapacity,
            };
            var error = new byte[1024];
            int rc = VncInterop.vnc_check_update(ref info, error, (UIntPtr)error.Length);
            if (rc != 0)
                throw new UpdateException(DecodeError(error));

            return new ReleaseInfo(
                info.version ?? "", info.name ?? "", info.publishedAt ?? "",
                info.htmlUrl ?? "", info.downloadUrl ?? "", info.checksumUrl ?? "",
                info.size, Marshal.PtrToStringUTF8(notes) ?? "");
        }
        finally
        {
            Marshal.FreeHGlobal(notes);
        }
    }

    /// <summary>下载、校验并静默安装。失败抛异常，错误信息可直接展示。</summary>
    public static void DownloadAndInstall(ReleaseInfo release)
    {
        ArgumentNullException.ThrowIfNull(release);
        // 原生侧也会拒绝空 checksumUrl；这里提前拦一次，给出更清楚的中文说明。
        if (string.IsNullOrWhiteSpace(release.ChecksumUrl))
            throw new UpdateException("该版本未提供 SHA-256 校验文件，为安全起见不自动安装；请前往发布页手动下载。");

        var error = new byte[1024];
        int rc = VncInterop.vnc_download_and_install(
            release.DownloadUrl, release.ChecksumUrl, release.Version,
            error, (UIntPtr)error.Length);
        if (rc != 0)
            throw new UpdateException(DecodeError(error));
    }

    /// <summary>语义化版本比较：a&lt;b 返回 -1，相等 0，a&gt;b 返回 1。</summary>
    public static int CompareVersions(string a, string b) =>
        VncInterop.vnc_compare_versions(a, b);

    private static string DecodeError(byte[] buffer)
    {
        int end = Array.IndexOf(buffer, (byte)0);
        if (end < 0) end = buffer.Length;
        string text = Encoding.UTF8.GetString(buffer, 0, end);
        return string.IsNullOrWhiteSpace(text) ? "更新操作失败，原因未知。" : text;
    }
}

internal sealed class UpdateException : Exception
{
    public UpdateException(string message) : base(message) { }
}
