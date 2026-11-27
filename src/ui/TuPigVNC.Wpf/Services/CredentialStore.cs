using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 「记住密码」的本地存储。对照 <c>src/vncviewer/Credentials.cxx</c>：
/// Windows 上用 DPAPI 按**当前用户**加密，密文再 base64 写进 JSON。
/// 不是明文，也不能跨用户/跨机器解密——换用户或拷到别的机器都解不开。
///
/// 设计约束：
/// <list type="bullet">
/// <item>文件权限：只写当前用户目录，不写安装目录（安装目录可能全局可读）。</item>
/// <item>原子替换：先写临时文件再改名，中途失败不会破坏已有凭据。</item>
/// <item>解密失败返回 null 而不是抛异常：凭据文件损坏或换了用户时，
///       应该退回「重新输入密码」，不能让程序起不来。</item>
/// </list>
/// </summary>
internal static class CredentialStore
{
    private const string FileName = "credentials.json";

    private sealed record Record(string Server, string Username, string Password);

    /// <summary>凭据文件路径。始终在用户本地应用数据目录下。</summary>
    public static string FilePath
    {
        get
        {
            string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
            return Path.Combine(local, "TuPigVNC", FileName);
        }
    }

    /// <summary>
    /// 保存凭据。密码为空时删除该服务器的记录（等价于「不记住」）。
    /// 返回 null 成功，否则返回中文错误说明。
    /// </summary>
    public static string? Save(string server, string username, string password)
    {
        if (string.IsNullOrEmpty(server)) return "服务器地址为空，无法保存凭据。";
        try
        {
            string path = FilePath;
            string? directory = Path.GetDirectoryName(path);
            if (directory is null) return "无法确定凭据目录。";
            Directory.CreateDirectory(directory);

            var records = LoadAll();
            records.RemoveAll(r => string.Equals(r.Server, server, StringComparison.OrdinalIgnoreCase));
            if (password.Length > 0)
            {
                string? encrypted = Protect(password);
                if (encrypted is null) return "加密密码失败；未保存。";
                records.Add(new Record(server, username, encrypted));
            }

            string json = JsonSerializer.Serialize(records,
                new JsonSerializerOptions { WriteIndented = true });
            // 原子替换：临时文件写完后改名，中途失败不破坏已有凭据。
            string temp = path + ".tmp";
            File.WriteAllText(temp, json, new UTF8Encoding(false));
            File.Move(temp, path, overwrite: true);
            return null;
        }
        catch (Exception ex)
        {
            return $"保存凭据失败：{ex.Message}";
        }
    }

    /// <summary>读取某服务器记住的用户名和密码。没有记录或解密失败返回 null。</summary>
    public static (string Username, string Password)? Load(string server)
    {
        if (string.IsNullOrEmpty(server)) return null;
        try
        {
            foreach (var record in LoadAll())
            {
                if (!string.Equals(record.Server, server, StringComparison.OrdinalIgnoreCase))
                    continue;
                string? password = Unprotect(record.Password);
                return password is null ? null : (record.Username, password);
            }
        }
        catch (Exception ex)
        {
            // 凭据损坏不能让程序起不来；退回重新输入即可。
            Debug.WriteLine($"[credentials] {ex.GetType().Name}");
        }
        return null;
    }

    /// <summary>删除某服务器的凭据。</summary>
    public static void Forget(string server)
    {
        if (string.IsNullOrEmpty(server)) return;
        try
        {
            var records = LoadAll();
            if (records.RemoveAll(r => string.Equals(r.Server, server, StringComparison.OrdinalIgnoreCase)) > 0)
            {
                string json = JsonSerializer.Serialize(records,
                    new JsonSerializerOptions { WriteIndented = true });
                File.WriteAllText(FilePath, json, new UTF8Encoding(false));
            }
        }
        catch (Exception ex) { Debug.WriteLine($"[credentials] {ex.GetType().Name}"); }
    }

    private static List<Record> LoadAll()
    {
        string path = FilePath;
        if (!File.Exists(path)) return new List<Record>();
        string json = File.ReadAllText(path, Encoding.UTF8);
        return JsonSerializer.Deserialize<List<Record>>(json) ?? new List<Record>();
    }

    /// <summary>DPAPI 加密后 base64。非 Windows 平台退化为 base64（仅编码）。</summary>
    private static string? Protect(string plain)
    {
        byte[] raw = Encoding.UTF8.GetBytes(plain);
        var input = new DataBlob(raw);
        try
        {
            if (!CryptProtectData(ref input, "TuPigVNC", IntPtr.Zero, IntPtr.Zero,
                    IntPtr.Zero, CryptProtectUiForbidden, out DataBlob output))
                return null;
            try { return Convert.ToBase64String(GetBytes(output)); }
            finally { LocalFree(output.Data); }
        }
        finally { Marshal.FreeHGlobal(input.Data); }
    }

    /// <summary>base64 解码后 DPAPI 解密。失败返回 null。</summary>
    private static string? Unprotect(string encoded)
    {
        byte[] cipher;
        try { cipher = Convert.FromBase64String(encoded); }
        catch (FormatException) { return null; }

        var input = new DataBlob(cipher);
        try
        {
            if (!CryptUnprotectData(ref input, IntPtr.Zero, IntPtr.Zero, IntPtr.Zero,
                    IntPtr.Zero, CryptProtectUiForbidden, out DataBlob output))
                return null;
            try { return Encoding.UTF8.GetString(GetBytes(output)); }
            finally { LocalFree(output.Data); }
        }
        finally { Marshal.FreeHGlobal(input.Data); }
    }

    private static byte[] GetBytes(DataBlob blob)
    {
        var bytes = new byte[blob.Size];
        if (blob.Size > 0) Marshal.Copy(blob.Data, bytes, 0, (int)blob.Size);
        return bytes;
    }

    private const uint CryptProtectUiForbidden = 0x1;

    [StructLayout(LayoutKind.Sequential)]
    private struct DataBlob
    {
        public uint Size;
        public IntPtr Data;
        public DataBlob(byte[] data)
        {
            Size = (uint)data.Length;
            Data = Marshal.AllocHGlobal(data.Length);
            Marshal.Copy(data, 0, Data, data.Length);
        }
    }

    [DllImport("crypt32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptProtectData(
        ref DataBlob dataIn, string? description, IntPtr entropy, IntPtr reserved,
        IntPtr promptStruct, uint flags, out DataBlob dataOut);

    [DllImport("crypt32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CryptUnprotectData(
        ref DataBlob dataIn, IntPtr description, IntPtr entropy, IntPtr reserved,
        IntPtr promptStruct, uint flags, out DataBlob dataOut);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern IntPtr LocalFree(IntPtr handle);
}
