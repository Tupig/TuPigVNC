using System.Diagnostics;
using System.IO;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;

namespace TuPigVNC.Wpf.Services;

/// <summary>一条连接历史记录（对应旧版 ServerHistoryEntry）。</summary>
internal sealed class HistoryEntry
{
    [JsonPropertyName("host")]
    public string Host { get; set; } = "";

    [JsonPropertyName("name")]
    public string Name { get; set; } = "";

    [JsonPropertyName("lastUsed")]
    public long LastUsed { get; set; }

    [JsonPropertyName("useCount")]
    public int UseCount { get; set; }

    /// <summary>给 WPF 绑定的相对时间文本（不写进 JSON）。</summary>
    [JsonIgnore]
    public string AgoText => HistoryStore.HumanAgo(LastUsed);
}

/// <summary>
/// 连接历史存储。逻辑照搬 ServerDialog.cxx：
/// - 文件位置：exe 目录\User\history.json（不可写时回退 %LOCALAPPDATA%\TuPigVNC\history.json）
/// - 去重：same_server（host + 端口，缺省端口 5900，端口 &lt; 100 视为 base+端口）
/// - 上限 250 条，超出时淘汰「次数最少、最久未用」的记录
/// - 写入：临时文件 + 落盘 + 原子替换
/// - 首次运行：从旧版注册表 HKCU\Software\TuPigVNC\vncviewer\history 迁移
/// </summary>
internal sealed class HistoryStore
{
    public const int MaxEntries = 250;
    private const int BasePort = 5900;
    private const string MutexName = @"Local\TuPigVNC-history";
    private const string LegacyHistoryKey = @"Software\TuPigVNC\vncviewer\history";

    private static readonly JsonSerializerOptions JsonOptions = new()
    {
        WriteIndented = true,
        Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
    };

    private readonly List<HistoryEntry> _entries = new();

    public IReadOnlyList<HistoryEntry> Entries => _entries;

    /// <summary>history.json 的完整路径（首次访问时解析，并会创建状态目录）。</summary>
    public static string HistoryFilePath { get; } =
        Path.Combine(ResolveStateDir(), "history.json");

    /// <summary>从磁盘读取历史；文件不存在时尝试从旧版迁移。</summary>
    public void Load()
    {
        _entries.Clear();

        string path = HistoryFilePath;
        using var _ = new HistoryLock();

        if (!File.Exists(path))
        {
            MigrateOldHistoryLocked();
            return;
        }

        try
        {
            string content = File.ReadAllText(path, Encoding.UTF8);
            var list = JsonSerializer.Deserialize<List<HistoryEntry>>(content, JsonOptions);
            if (list is not null)
            {
                foreach (var e in list)
                {
                    if (string.IsNullOrEmpty(e.Host))
                        continue;
                    if (string.IsNullOrEmpty(e.Name))
                        e.Name = e.Host;
                    if (!_entries.Exists(x => SameServer(x.Host, e.Host)))
                        _entries.Add(e);
                }
            }
            Prune();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[history] 读取失败：{ex.Message}");
        }
    }

    /// <summary>原子写入历史文件。</summary>
    public void Save()
    {
        string path = HistoryFilePath;
        string? dir = Path.GetDirectoryName(path);
        if (!string.IsNullOrEmpty(dir))
            Directory.CreateDirectory(dir);

        using var _ = new HistoryLock();
        SaveLocked();
    }

    /// <summary>已存在则累加次数并置顶，否则插入新记录。</summary>
    public void AddOrTouch(string host)
    {
        long now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        var existing = _entries.Find(e => SameServer(e.Host, host));
        if (existing is not null)
        {
            _entries.Remove(existing);
            existing.UseCount++;
            existing.LastUsed = now;
            _entries.Insert(0, existing);
        }
        else
        {
            _entries.Insert(0, new HistoryEntry
            {
                Host = host,
                Name = host,
                LastUsed = now,
                UseCount = 1,
            });
        }
        Prune();
    }

    public bool Remove(HistoryEntry entry) => _entries.Remove(entry);

    // ------------------------------------------------------------------
    // 内部实现
    // ------------------------------------------------------------------

    private void SaveLocked()
    {
        string path = HistoryFilePath;
        string tmp = path + ".tmp";
        string content = JsonSerializer.Serialize(_entries, JsonOptions);

        using (var fs = new FileStream(tmp, FileMode.Create, FileAccess.Write, FileShare.None))
        using (var sw = new StreamWriter(fs, new UTF8Encoding(false)))
        {
            sw.Write(content);
            sw.Flush();
            fs.Flush(true); // 落盘
        }

        File.Move(tmp, path, overwrite: true);
    }

    private void Prune()
    {
        while (_entries.Count > MaxEntries)
        {
            var worst = _entries[0];
            foreach (var e in _entries)
            {
                if (e.UseCount < worst.UseCount ||
                    (e.UseCount == worst.UseCount && e.LastUsed < worst.LastUsed))
                    worst = e;
            }
            _entries.Remove(worst);
        }
    }

    private void MigrateOldHistoryLocked()
    {
        if (!OperatingSystem.IsWindows())
            return;

        try
        {
            using var key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(LegacyHistoryKey);
            if (key is null)
                return;

            var old = new List<string>();
            for (int i = 0; ; i++)
            {
                object? value = key.GetValue(i.ToString());
                if (value is null)
                    break;
                string text = value.ToString() ?? "";
                if (text.Length > 0)
                    old.Add(text);
            }

            long ts = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
            foreach (string host in old)
            {
                if (_entries.Exists(x => SameServer(x.Host, host)))
                    continue;
                _entries.Add(new HistoryEntry
                {
                    Host = host,
                    Name = host,
                    LastUsed = ts--,
                    UseCount = 1,
                });
            }

            if (_entries.Count > 0)
                SaveLocked();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[history] 旧版迁移失败：{ex.Message}");
        }
    }

    /// <summary>把「最后使用时间」转成中文相对时间（照搬 humanAgo）。</summary>
    public static string HumanAgo(long lastUsed)
    {
        if (lastUsed <= 0)
            return "从未";

        long diff = DateTimeOffset.UtcNow.ToUnixTimeSeconds() - lastUsed;
        if (diff < 60)
            return "刚刚";
        if (diff < 3600)
            return (diff / 60) + " 分钟前";
        if (diff < 86400)
            return (diff / 3600) + " 小时前";
        if (diff < 86400 * 7)
            return (diff / 86400) + " 天前";
        return (diff / (86400 * 7)) + " 周前";
    }

    /// <summary>判断两个服务器地址是否指向同一台机器（照搬 same_server）。</summary>
    public static bool SameServer(string a, string b)
    {
        if (!TryParseHostPort(a, out string ha, out int pa))
            return false;
        if (!TryParseHostPort(b, out string hb, out int pb))
            return false;
        return string.Equals(ha, hb, StringComparison.Ordinal) && pa == pb;
    }

    /// <summary>
    /// 解析 "host:port" / "[::1]:5900" / "host" 等写法。
    /// 逻辑与 network::getHostAndPort(..., basePort=5900) 一致。
    /// </summary>
    public static bool TryParseHostPort(string? input, out string host, out int port)
    {
        host = "localhost";
        port = BasePort;
        if (input is null)
            return false;

        int len = input.Length;
        int i = 0;
        while (i < len && char.IsWhiteSpace(input[i]))
            i++;

        int hostStart, hostEnd;
        int portStart = -1;

        if (i < len && input[i] == '[')
        {
            // [IPv6]:port 形式
            hostStart = i + 1;
            int close = input.IndexOf(']', hostStart);
            if (close < 0)
                return false;
            hostEnd = close;
            portStart = close + 1;
            if (IsAllSpace(input, portStart))
                portStart = -1;
        }
        else
        {
            hostStart = i;
            int lastColon = input.LastIndexOf(':');
            if (lastColon < i)
            {
                // 没有端口
                hostEnd = len;
                portStart = -1;
            }
            else
            {
                hostEnd = lastColon;
                if (hostEnd > hostStart && input[hostEnd - 1] == ':')
                    hostEnd--; // 末尾 "::" 是隧道写法
                int firstColon = input.IndexOf(':', hostStart);
                if (firstColon != hostEnd)
                {
                    // 多个冒号，按 IPv6 字面量处理，不带端口
                    hostEnd = len;
                    portStart = -1;
                }
                else
                {
                    portStart = firstColon;
                }
            }
        }

        while (hostEnd > hostStart && char.IsWhiteSpace(input[hostEnd - 1]))
            hostEnd--;

        host = hostStart == hostEnd
            ? "localhost"
            : input.Substring(hostStart, hostEnd - hostStart);

        if (portStart < 0)
        {
            port = BasePort;
            return true;
        }

        if (portStart >= len || input[portStart] != ':')
            return false;

        int numStart = portStart + 1;
        if (numStart < len && input[numStart] == ':')
            numStart++; // "::" 隧道语法

        int p = numStart;
        while (p < len && char.IsDigit(input[p]))
            p++;

        int value;
        if (p == numStart)
        {
            // 没有数字：strtol 返回 0，剩余必须是空白
            value = 0;
            if (!IsAllSpace(input, numStart))
                return false;
        }
        else
        {
            if (!int.TryParse(input.AsSpan(numStart, p - numStart), out value))
                return false;
            if (!IsAllSpace(input, p))
                return false;
        }

        if (input[portStart + 1] != ':' && value < 100)
            value += BasePort;

        port = value;
        return true;
    }

    private static bool IsAllSpace(string s, int start)
    {
        for (int i = start; i < s.Length; i++)
        {
            if (!char.IsWhiteSpace(s[i]))
                return false;
        }
        return true;
    }

    /// <summary>
    /// 状态目录：exe\..\User（可写则用），否则 %LOCALAPPDATA%\TuPigVNC。
    /// 与 core::getvncstatedir() 的 Windows 分支一致。
    /// </summary>
    private static string ResolveStateDir()
    {
        try
        {
            string exeDir = AppContext.BaseDirectory.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
            string userDir = Path.Combine(exeDir, "User");
            Directory.CreateDirectory(userDir);

            string probe = Path.Combine(userDir, ".write_probe");
            File.WriteAllText(probe, "");
            File.Delete(probe);
            return userDir;
        }
        catch
        {
            // 安装目录不可写，回退到用户本地应用数据
        }

        string local = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
        return Path.Combine(local, "TuPigVNC");
    }

    /// <summary>
    /// 跨进程命名互斥量：串行化多个实例对 history.json 的「读-改-写」。
    /// Windows 互斥量可重入，同一线程重复获取不会死锁。
    /// </summary>
    private sealed class HistoryLock : IDisposable
    {
        private Mutex? _mutex;
        private bool _acquired;

        public HistoryLock()
        {
            try
            {
                _mutex = new Mutex(false, MutexName);
                _acquired = _mutex.WaitOne(TimeSpan.FromSeconds(10));
                if (!_acquired)
                    Debug.WriteLine("[history] 等待互斥量超时，将在无锁状态下继续");
            }
            catch (Exception ex)
            {
                Debug.WriteLine($"[history] 获取互斥量失败：{ex.Message}");
            }
        }

        public void Dispose()
        {
            if (_mutex is null)
                return;
            try
            {
                if (_acquired)
                    _mutex.ReleaseMutex();
            }
            catch
            {
                // 忽略释放异常
            }
            _mutex.Dispose();
            _mutex = null;
        }
    }
}
