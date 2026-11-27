namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 从 .tigervnc 配置里取出编码相关设置，转成核心 ABI 认识的数值。
/// 这里只做「配置文本 → 协议编号」的翻译，不判断服务器是否支持；
/// 不支持的编码由核心按 rfb::Decoder::supported() 自行跳过。
/// </summary>
internal readonly record struct EncodingSettings(
    int PreferredEncoding,
    int CompressLevel,
    int QualityLevel,
    bool AllowJpeg,
    bool AutoSelect,
    bool CustomCompression)
{
    /// <summary>rfb::encodingTight：旧版自动选择时的默认编码。</summary>
    private const int EncodingTight = 7;

    /// <summary>压缩 / 质量未启用时传给核心的哨兵值，对应原版 compressLevel = -1。</summary>
    private const int Disabled = -1;

    /// <summary>配置里的编码名称 → RFB 编号。与 src/common/rfb/encodings.cxx 保持一致。</summary>
    private static readonly Dictionary<string, int> EncodingNumbers = new(StringComparer.OrdinalIgnoreCase)
    {
        ["Raw"] = 0,
        ["Hextile"] = 5,
        ["Tight"] = 7,
        ["ZRLE"] = 16,
        ["JPEG"] = 21,
        ["H.264"] = 50,
    };

    public static EncodingSettings FromConfiguration(VncConfiguration configuration)
    {
        bool autoSelect = configuration.GetBoolean("AutoSelect") ?? true;
        // 键名必须和 TigerVNC 原生 parameters.cxx 一致，否则读别人的配置文件时静默失效。
        bool customCompression = configuration.GetBoolean("CustomCompressLevel") ?? false;
        // 原生是取反的 NoJPEG（"禁用有损 JPEG"），不是 Jpeg。
        bool allowJpeg = !(configuration.GetBoolean("NoJPEG") ?? false);

        // AutoSelect 打开时固定用 Tight，跟旧版 updateEncoding() 一致；
        // 关掉才读首选编码，认不出的名字保持 Tight，不产生非法编号。
        int preferred = EncodingTight;
        if (!autoSelect)
        {
            string? name = configuration.Get("PreferredEncoding");
            if (!string.IsNullOrWhiteSpace(name) && EncodingNumbers.TryGetValue(name.Trim(), out int number))
                preferred = number;
        }

        // 压缩等级只有勾了「自定义压缩」才生效，且必须是 0–9；其余情况交给核心用默认值。
        int compress = Disabled;
        if (customCompression)
            compress = ClampLevel(configuration.Get("CompressLevel"));

        // JPEG 质量在原版里是「自动选择关闭时才生效」；这里额外要求允许 JPEG，
        // 否则服务器根本不会用 JPEG 编码，质量值没有意义。
        int quality = Disabled;
        if (!autoSelect && allowJpeg)
            quality = ClampLevel(configuration.Get("QualityLevel"));

        return new EncodingSettings(preferred, compress, quality, allowJpeg, autoSelect, customCompression);
    }

    /// <summary>只接受单个 0–9 字符；其他情况一律视为未设置。</summary>
    private static int ClampLevel(string? value)
    {
        if (value is null) return Disabled;
        string trimmed = value.Trim();
        return trimmed.Length == 1 && trimmed[0] is >= '0' and <= '9'
            ? trimmed[0] - '0'
            : Disabled;
    }

    /// <summary>把这份设置套到会话上。调用方保证在 StartLoop 之后、第一次更新之前调用。</summary>
    public void ApplyTo(VncSession session)
    {
        session.SetEncoding(PreferredEncoding);
        session.SetCompressLevel(CompressLevel);
        session.SetQualityLevel(QualityLevel);
    }
}
