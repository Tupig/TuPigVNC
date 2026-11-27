using System.Text;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 命令行参数解析。逐条对照 <c>src/vncviewer/vncviewer.cxx</c> 的 main 循环：
/// <list type="bullet">
/// <item><c>host</c> / <c>host:display</c> / <c>host::port</c> —— 直接连接</item>
/// <item><c>*.tigervnc</c> 等含路径分隔符的参数 —— 当作配置文件载入</item>
/// <item><c>-listen [port]</c> —— 监听模式，等服务器反向连入</item>
/// <item><c>-via &lt;gateway&gt;</c> —— 经 SSH 网关建隧道</item>
/// <item><c>-h</c>/<c>--help</c>、<c>-v</c>/<c>--version</c></item>
/// </list>
/// 其他 <c>-参数</c> 原版交给 <c>core::Configuration::handleParamArg</c> 覆盖配置；
/// 本版不修改原生全局配置，遇到未知参数明确报错而不是静默忽略。
/// </summary>
public sealed record CommandLineOptions
{
    /// <summary>要连接的服务器地址（含 <c>:display</c> 或 <c>::port</c>）。</summary>
    public string? Server { get; init; }

    /// <summary>要载入的配置文件路径。</summary>
    public string? ConfigFile { get; init; }

    /// <summary>是否进入监听模式（等服务器反向连入）。</summary>
    public bool Listen { get; init; }

    /// <summary>监听端口，默认 5500（原版行为）。</summary>
    public int ListenPort { get; init; } = 5500;

    /// <summary>SSH 网关主机（<c>-via</c>）。</summary>
    public string? Via { get; init; }

    public bool ShowHelp { get; init; }
    public bool ShowVersion { get; init; }

    /// <summary>解析失败时的中文说明；成功时为 null。</summary>
    public string? Error { get; init; }

    public static CommandLineOptions Parse(IReadOnlyList<string> args)
    {
        string? server = null;
        string? configFile = null;
        string? via = null;
        bool listen = false;
        int listenPort = 5500;

        for (int i = 0; i < args.Count; i++)
        {
            string arg = args[i];

            switch (arg)
            {
                case "-h" or "--help":
                    return new CommandLineOptions { ShowHelp = true };
                case "-v" or "--version":
                    return new CommandLineOptions { ShowVersion = true };
                case "-via" or "--via":
                    if (i + 1 >= args.Count)
                        return new CommandLineOptions { Error = "-via 后面需要网关主机名。" };
                    via = args[++i];
                    continue;
                case "-listen" or "--listen":
                    listen = true;
                    // 端口是可选的：下一个参数是纯数字才当作端口。
                    if (i + 1 < args.Count && IsPort(args[i + 1]))
                        listenPort = int.Parse(args[++i]);
                    continue;
            }

            if (arg.StartsWith('-') && arg.Length > 1)
                return new CommandLineOptions
                {
                    Error = $"无法识别的参数「{arg}」。本版不支持用命令行覆盖原生配置项，" +
                            $"请在界面里设置，或用 -h 查看支持的形式。",
                };

            // 含路径分隔符的参数当作配置文件（原版 potentiallyLoadConfigurationFile）。
            if (arg.Contains('/') || arg.Contains('\\') || arg.Contains('='))
            {
                if (configFile is not null)
                    return new CommandLineOptions { Error = "只能指定一个配置文件。" };
                configFile = arg;
                continue;
            }

            if (server is not null)
                return new CommandLineOptions { Error = $"多余的参数「{arg}」；只能指定一个服务器地址。" };

            // 地址里显式写了端口就必须是合法端口：host::port 或 host:display。
            // 不校验的话，host:99999 会一路走到 TCP 连接，最后报一个
            // 跟参数完全无关的网络错误，用户根本不知道是地址写错了。
            if (TrySplitExplicitPort(arg, out string hostPart, out string portText))
            {
                if (!IsPort(portText))
                    return new CommandLineOptions
                    {
                        Error = $"端口「{portText}」超出 1-65535 范围（地址：{arg}）。",
                    };
                if (hostPart.Length == 0)
                    return new CommandLineOptions { Error = $"地址「{arg}」缺少主机名。" };
            }

            server = arg;
        }

        // 同时指定 -via 和 -listen 毫无意义（原版明确报错）。
        if (listen && via is not null)
            return new CommandLineOptions { Error = "-listen 和 -via 不能同时使用。" };

        if (listen)
        {
            if (server is not null && !IsPort(server))
                return new CommandLineOptions { Error = $"-listen 只接受端口号，收到「{server}」。" };
            if (server is not null && IsPort(server))
                listenPort = int.Parse(server);
            return new CommandLineOptions { Listen = true, ListenPort = listenPort };
        }

        return new CommandLineOptions { Server = server, ConfigFile = configFile, Via = via };
    }

    private static bool IsPort(string value) =>
        value.Length is > 0 and <= 5 && value.All(char.IsAsciiDigit) && int.TryParse(value, out int port)
        && port is > 0 and <= 65535;

    /// <summary>
    /// 拆出地址里「显式指定的端口」。
    /// 支持 <c>host::port</c>（双冒号，直接端口）和 <c>host:display</c>（显示号）。
    /// 没有冒号、或末尾不是纯数字时返回 false（交给后续解析）。
    /// IPv6 字面量（含多个冒号）不在此处理，视为无需校验。
    /// </summary>
    private static bool TrySplitExplicitPort(string address, out string host, out string portText)
    {
        host = address;
        portText = "";

        if (string.IsNullOrEmpty(address)) return false;

        // 双冒号形式：host::port
        int doubleColon = address.LastIndexOf("::", StringComparison.Ordinal);
        if (doubleColon > 0)
        {
            // 多个双冒号说明是 IPv6，不在这里校验。
            if (address.IndexOf("::", StringComparison.Ordinal) != doubleColon) return false;
            host = address[..doubleColon];
            portText = address[(doubleColon + 2)..];
            return portText.Length > 0;
        }

        // 单冒号形式：host:display，且主机部分不能再有冒号（否则是 IPv6）
        int colon = address.LastIndexOf(':');
        if (colon <= 0) return false;
        if (address[..colon].Contains(':')) return false;

        host = address[..colon];
        portText = address[(colon + 1)..];
        return portText.Length > 0;
    }

    /// <summary>帮助文本。对照原版 usage()，只列本版实际支持的形式。</summary>
    public static string HelpText(string programName) => new StringBuilder()
        .AppendLine()
        .AppendLine($"用法：{programName} [参数] [主机][:显示号]")
        .AppendLine($"      {programName} [参数] [主机][::端口]")
        .AppendLine($"      {programName} [参数] -listen [端口]")
        .AppendLine($"      {programName} [参数] [.tigervnc 配置文件]")
        .AppendLine()
        .AppendLine("参数：")
        .AppendLine("  -listen [端口]   监听模式，等待 VNC 服务器反向连入（默认端口 5500）")
        .AppendLine("  -via <网关>      经 SSH 网关建立隧道；需要 OpenSSH 和已配置的密钥")
        .AppendLine("  -h, --help       显示本帮助")
        .AppendLine("  -v, --version    显示版本号")
        .AppendLine()
        .AppendLine("显示号与端口：主机:1 表示 5901 端口；主机::5901 直接指定端口。")
        .ToString();
}
