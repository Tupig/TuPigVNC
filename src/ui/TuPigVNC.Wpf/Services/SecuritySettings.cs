namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 把「加密方式 × 身份认证方式」两个勾选组合翻译成 RFB 的安全类型列表。
/// 语义逐条对照 <c>src/vncviewer/OptionsDialog.cxx</c> 的 security 段：
/// 加密和认证是正交的两组，最终类型是两者的笛卡尔积。
/// </summary>
internal static class SecuritySettings
{
    /// <summary>
    /// 组合出安全类型列表（逗号分隔的 TigerVNC 名字）。
    /// 返回空字符串表示一个都没选——调用方必须按「拒绝连接」处理，
    /// 不能退回到默认值，否则用户以为关掉了加密实际还在用。
    /// </summary>
    public static string Build(
        bool encNone, bool encTls, bool encX509, bool encRsa,
        bool authNone, bool authVnc, bool authPlain, bool authRsa)
    {
        var types = new List<string>();

        void Add(string name) { if (!types.Contains(name)) types.Add(name); }

        // 无加密：None / VncAuth / Plain 直接对应。
        // 原版在这里还会挂 RA2ne / RA2ne_256（VNC 密码时）和 DH / MSLogonII
        // （用户名密码时）——这些是 RSA-AES 家族的**非加密**变体，
        // 归到 RSA-AES 那一组更清楚，由 encRsa 统一控制。
        if (encNone)
        {
            if (authNone) Add("None");
            if (authVnc) Add("VncAuth");
            if (authPlain) Add("Plain");
        }

        // TLS 加密（匿名证书）：TLSNone / TLSVnc / TLSPlain。
        if (encTls)
        {
            if (authNone) Add("TLSNone");
            if (authVnc) Add("TLSVnc");
            if (authPlain) Add("TLSPlain");
        }

        // X.509 证书加密：X509None / X509Vnc / X509Plain。
        if (encX509)
        {
            if (authNone) Add("X509None");
            if (authVnc) Add("X509Vnc");
            if (authPlain) Add("X509Plain");
        }

        // RSA-AES（Nettle 提供）：RA2 / RA2_256 是加密变体；
        // RA2ne / RA2ne_256 是非加密变体。原版把它们放在「无加密」分支里，
        // 因为 ne 版本不套 TLS 层。这里按 RSA-AES 统一控制——用户勾了
        // 「RSA-AES」就是要用这套认证，加密与否由 RA2/RA2ne 区分。
        // DH / MSLogonII 只在「无加密 + 用户名密码」时才有意义（原版语义）。
        if (encRsa)
        {
            Add("RA2");
            Add("RA2_256");
            if (encNone && (authVnc || authPlain)) Add("RA2ne");
            if (encNone && (authVnc || authPlain)) Add("RA2ne_256");
            if (encNone && authPlain)
            {
                Add("DH");
                Add("MSLogonII");
            }
        }

        return string.Join(",", types);
    }

    /// <summary>
    /// 本核心实际编译进去的安全类型集合（大写比较）。
    /// 用来把界面上不支持的勾选项置灰、并解释原因。
    /// </summary>
    public static HashSet<string> Supported()
    {
        string raw = VncSession.AvailableSecurityTypes();
        var set = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (string part in raw.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            set.Add(part);
        return set;
    }

    /// <summary>某一种加密方式是否被核心支持（看它下面任一认证子类型在不在）。</summary>
    public static bool EncryptionSupported(HashSet<string> supported, string encryption) => encryption switch
    {
        "无加密" => supported.Contains("None") || supported.Contains("VncAuth") || supported.Contains("Plain"),
        "TLS" => supported.Contains("TLSNone") || supported.Contains("TLSVnc") || supported.Contains("TLSPlain"),
        "X.509" => supported.Contains("X509None") || supported.Contains("X509Vnc") || supported.Contains("X509Plain"),
        "RSA-AES" => supported.Contains("RA2") || supported.Contains("RA2_256"),
        _ => false,
    };
}
