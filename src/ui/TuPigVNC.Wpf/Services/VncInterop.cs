using System.Runtime.InteropServices;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// vnc_connect 的返回码（与 VncAbi.h 文件头约定一致）。
/// </summary>
internal enum VncConnectResult
{
    Ok = 0,
    InvalidArgument = -1,
    NetworkError = -2,
    HandshakeFailed = -3,
    AuthenticationFailed = -4,
    InternalError = -5,
}

// ---- 回调签名（全部 Cdecl；字符串统一 UTF-8）----
// 所有回调都在核心 DLL 的内部网络线程上触发，前端必须自行切回 UI 线程。

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ServerInitCallback(IntPtr ud, int width, int height,
    [MarshalAs(UnmanagedType.LPUTF8Str)] string? name,
    int bpp, int depth, int rs, int gs, int bs);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void FrameUpdateCallback(IntPtr ud, int x, int y, int w, int h);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ErrorCallback(IntPtr ud,
    [MarshalAs(UnmanagedType.LPUTF8Str)] string? msg, int fatal);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int CredentialsCallback(IntPtr ud,
    [MarshalAs(UnmanagedType.LPUTF8Str)] string? host, int secure,
    IntPtr user, UIntPtr userLen, IntPtr pass, UIntPtr passLen);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int VerifyCertCallback(IntPtr ud, uint status, IntPtr cert, UIntPtr len);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate int VerifyHostkeyCallback(IntPtr ud, IntPtr key, UIntPtr len,
    [MarshalAs(UnmanagedType.LPUTF8Str)] string? fp);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ClipboardCallback(IntPtr ud,
    [MarshalAs(UnmanagedType.LPUTF8Str)] string? text);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ClipboardAnnounceCallback(IntPtr ud, int available);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ClipboardRequestCallback(IntPtr ud);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void CursorCallback(IntPtr ud, int width, int height,
    int hotX, int hotY, IntPtr rgba);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void CursorPosCallback(IntPtr ud, int x, int y);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void NameCallback(IntPtr ud,
    [MarshalAs(UnmanagedType.LPUTF8Str)] string? name);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void LedCallback(IntPtr ud, uint state);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void ResizeCallback(IntPtr ud, int width, int height);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
internal delegate void BellCallback(IntPtr ud);

/// <summary>
/// 与 VncAbi.h 的 struct VncCallbacks 严格同布局：
/// void* ud + 13 个函数指针。字段顺序不能动。
/// </summary>
[StructLayout(LayoutKind.Sequential)]
internal struct VncCallbacks
{
    public IntPtr ud;
    public ServerInitCallback? on_server_init;
    public FrameUpdateCallback? on_frame_update;
    public ErrorCallback? on_error;
    public CredentialsCallback? on_credentials;
    public VerifyCertCallback? on_verify_cert;
    public VerifyHostkeyCallback? on_verify_hostkey;
    public ClipboardCallback? on_clipboard;
    public ClipboardAnnounceCallback? on_clipboard_announce;
    public ClipboardRequestCallback? on_clipboard_request;
    public CursorCallback? on_cursor;
    public CursorPosCallback? on_cursor_pos;
    public NameCallback? on_name;
    public LedCallback? on_led;
    public ResizeCallback? on_resize;
    public BellCallback? on_bell;
}

/// <summary>
/// TuPigVNC.Core.dll 的原始 P/Invoke 声明。
/// 接口是纯 C ABI（const char* / int / void*），跨 DLL 边界只用基础类型。
/// </summary>
internal static class VncInterop
{
    private const string Dll = "TuPigVNC.Core";

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vnc_create(ref VncCallbacks cb);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_connect(IntPtr s,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string host, int port,
        [Out] byte[]? err, UIntPtr errLen);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_listen(IntPtr s, int port, int acceptTimeoutMs,
        [Out] byte[]? err, UIntPtr errLen);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_start_loop(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_stop(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_destroy(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_framebuffer(IntPtr s, IntPtr data,
        int width, int height, int stride);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_send_key(IntPtr s, int systemKeyCode,
        uint keyCode, uint keySym);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_send_key_release(IntPtr s, int systemKeyCode);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_send_pointer(IntPtr s, int x, int y,
        ushort buttonmask);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_refresh(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_shared(IntPtr s, int shared);

    // ---- 自动更新（阻塞，调用方负责放后台线程）----

    [StructLayout(LayoutKind.Sequential)]
    public struct VncReleaseInfo
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string version;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)] public string name;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)] public string publishedAt;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string htmlUrl;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string downloadUrl;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)] public string checksumUrl;
        public long size;
        public IntPtr notes;
        public UIntPtr notesCapacity;
    }

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_check_update(ref VncReleaseInfo info,
        [Out] byte[]? err, UIntPtr errLen);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_compare_versions(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? a,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? b);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_download_and_install(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string url,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? checksumUrl,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? version,
        [Out] byte[]? err, UIntPtr errLen);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_request_clipboard(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_announce_clipboard(IntPtr s, int available);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_send_clipboard(IntPtr s,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string text);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_encoding(IntPtr s, int encoding);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_compress_level(IntPtr s, int level);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_quality_level(IntPtr s, int level);

    // ---- 安全类型 ----

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern int vnc_set_security_types(IntPtr s, string types);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_security_types(byte[] buffer, UIntPtr capacity);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern void vnc_set_x509_ca(IntPtr s, string path);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    public static extern void vnc_set_x509_crl(IntPtr s, string path);

    // ---- 音频 ----

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_audio_enabled(IntPtr s, int enabled);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_audio_available(IntPtr s);

    // ---- 网络统计 ----

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_get_stats(IntPtr s,
        out uint updates, out uint pixels, out uint bytes, out ulong bitsPerSecond);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_set_auto_select_quality(IntPtr s, int enabled);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_request_remote_size(IntPtr s, int width, int height);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_request_screen_layout(IntPtr s, int width,
        int height, [In] int[] screens, int count);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_test_add(int a, int b);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vnc_version();

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_request_stop(IntPtr s);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_copy_frame(IntPtr s, [Out] byte[]? data,
        UIntPtr capacity, int stride, out int width, out int height);

    // ---- 托管侧便捷方法 ----

    public static int TestAdd(int a, int b) => vnc_test_add(a, b);

    public static string Version()
    {
        try
        {
            IntPtr p = vnc_version();
            return p == IntPtr.Zero ? "(null)" : Marshal.PtrToStringUTF8(p) ?? "(null)";
        }
        catch (Exception ex)
        {
            return $"核心 DLL 不可用（{ex.GetType().Name}）";
        }
    }
}

/// <summary>
/// VncSession 的托管包装：负责保持回调委托存活、把原生字符串转成托管字符串，
/// 并在 Dispose 时释放原生会话（vnc_destroy 内部会停事件循环）。
/// </summary>
internal sealed class VncSession : IDisposable
{
    private IntPtr _handle;
    private readonly VncCallbacks _callbacks;

    // 必须用字段钉住这些委托：原生侧只存函数指针，托管对象一旦被 GC
    // 回收就会变成野指针。
    private readonly ServerInitCallback _onServerInit;
    private readonly FrameUpdateCallback _onFrameUpdate;
    private readonly ErrorCallback _onError;
    private readonly CredentialsCallback _onCredentials;
    private readonly VerifyCertCallback _onVerifyCert;
    private readonly VerifyHostkeyCallback _onVerifyHostkey;
    private readonly ClipboardCallback _onClipboard;
    private readonly ClipboardAnnounceCallback _onClipboardAnnounce;
    private readonly ClipboardRequestCallback _onClipboardRequest;
    private readonly CursorCallback _onCursor;
    private readonly CursorPosCallback _onCursorPos;
    private readonly NameCallback _onName;
    private readonly LedCallback _onLed;
    private readonly ResizeCallback _onResize;
    private readonly BellCallback _onBell;

    /// <summary>连接建立（服务器初始化完成）。参数：宽、高、桌面名称。</summary>
    public event Action<int, int, string>? ServerInit;

    /// <summary>一帧更新完成。参数：脏矩形 x、y、w、h。</summary>
    public event Action<int, int, int, int>? FrameUpdate;

    /// <summary>连接断开或致命错误。参数：消息、是否致命。</summary>
    public event Action<string, bool>? Error;

    private string? _fatalError;
    // 订阅后再读此值，接住握手线程与会话窗口交接期间的断线。
    internal string? FatalError => System.Threading.Volatile.Read(ref _fatalError);

    /// <summary>服务器名称/桌面名改变。</summary>
    public event Action<string>? NameChanged;

    /// <summary>
    /// 光标形状改变。参数：宽、高、热点 X、热点 Y、预乘 RGBA 位图。
    /// 位图按回调期间有效处理，订阅方需要自己拷贝。
    /// </summary>
    public event Action<int, int, int, int, byte[]>? CursorChanged;

    /// <summary>服务器剪贴板文本。</summary>
    public event Action<string>? ClipboardReceived;

    /// <summary>
    /// 扩展剪贴板：远端剪贴板内容有变化。参数为 true 时应调用
    /// <see cref="RequestClipboard"/> 拉取内容；false 表示远端已清空。
    /// 新版服务器默认走扩展协议，不处理这里剪贴板会完全不通。
    /// </summary>
    public event Action<bool>? ClipboardAnnounced;

    /// <summary>扩展剪贴板：服务器要求发送本地剪贴板，应读取本机内容后调 <see cref="SendClipboard"/>。</summary>
    public event Action? ClipboardRequested;

    /// <summary>
    /// 需要用户名/密码时调用。返回 null 表示取消连接。
    /// 参数：host、是否加密、是否需要用户名、是否需要密码。
    /// 该委托在后台网络线程上调用，实现方必须自行切回 UI 线程。
    /// </summary>
    public Func<string, bool, bool, bool, (string User, string Password)?>? CredentialProvider { get; set; }

    /// <summary>服务器要求响铃。原版就是 <c>fl_beep()</c>，这里交给界面播放系统提示音。</summary>
    public event Action? Bell;

    /// <summary>键盘 LED 状态：bit0=CapsLock、bit1=NumLock、bit2=ScrollLock。</summary>
    public event Action<uint>? LedChanged;

    /// <summary>
    /// 服务器证书需要用户确认时调用。返回 true 才继续连接。
    /// 参数：gnutls 状态位、证书 DER 字节、SHA-256 指纹。
    /// 未设置时一律拒绝——不会静默接受未知证书。
    /// 该委托在后台网络线程上调用，实现方必须自行切回 UI 线程。
    /// </summary>
    public Func<uint, byte[], string, bool>? CertificateVerifier { get; set; }

    /// <summary>
    /// SSH 主机密钥需要用户确认时调用。返回 true 才继续连接。
    /// 参数：公钥字节、SHA-256 指纹（服务端给的原样字符串）。
    /// 未设置时一律拒绝。该委托在后台网络线程上调用。
    /// </summary>
    public Func<byte[], string, bool>? HostKeyVerifier { get; set; }

    /// <summary>最近一次证书/主机密钥确认的指纹，供界面在断线后补显示。</summary>
    internal string? LastTrustFingerprint => System.Threading.Volatile.Read(ref _trustFingerprint);

    private string? _trustFingerprint;

    public VncSession()
    {
        _onServerInit = OnServerInit;
        _onFrameUpdate = OnFrameUpdate;
        _onError = OnError;
        _onCredentials = OnCredentials;
        _onVerifyCert = OnVerifyCert;
        _onVerifyHostkey = OnVerifyHostkey;
        _onClipboard = OnClipboard;
        _onClipboardAnnounce = OnClipboardAnnounce;
        _onClipboardRequest = OnClipboardRequest;
        _onCursor = OnCursor;
        _onCursorPos = (ud, x, y) => { };
        _onName = OnName;
        _onLed = OnLed;
        _onResize = (ud, w, h) => { };
        _onBell = OnBell;

        _callbacks = new VncCallbacks
        {
            ud = IntPtr.Zero,
            on_server_init = _onServerInit,
            on_frame_update = _onFrameUpdate,
            on_error = _onError,
            on_credentials = _onCredentials,
            on_verify_cert = _onVerifyCert,
            on_verify_hostkey = _onVerifyHostkey,
            on_clipboard = _onClipboard,
            on_clipboard_announce = _onClipboardAnnounce,
            on_clipboard_request = _onClipboardRequest,
            on_cursor = _onCursor,
            on_cursor_pos = _onCursorPos,
            on_name = _onName,
            on_led = _onLed,
            on_resize = _onResize,
            on_bell = _onBell,
        };

        _handle = VncInterop.vnc_create(ref _callbacks);
        if (_handle == IntPtr.Zero)
            throw new InvalidOperationException("vnc_create 返回 NULL");
    }

    public bool IsValid => _handle != IntPtr.Zero;

    /// <summary>同步连接（会阻塞，调用方负责放到后台线程）。</summary>
    public VncConnectResult Connect(string host, int port, out string error)
    {
        var buffer = new byte[1024];
        int rc = VncInterop.vnc_connect(_handle, host, port, buffer, (UIntPtr)buffer.Length);
        error = rc == 0 ? "" : DecodeUtf8CString(buffer);
        return (VncConnectResult)rc;
    }

    /// <summary>
    /// 监听模式：绑定端口等服务器反向连入（阻塞，调用方负责放后台线程）。
    /// </summary>
    public VncConnectResult Listen(int port, int acceptTimeoutMs, out string error)
    {
        var buffer = new byte[1024];
        int rc = VncInterop.vnc_listen(_handle, port, acceptTimeoutMs, buffer, (UIntPtr)buffer.Length);
        error = rc == 0 ? "" : DecodeUtf8CString(buffer);
        return (VncConnectResult)rc;
    }

    public void RequestStop()
    {
        if (_handle != IntPtr.Zero) VncInterop.vnc_request_stop(_handle);
    }

    public byte[]? CopyFrame(out int width, out int height)
    {
        VncInterop.vnc_copy_frame(_handle, null, UIntPtr.Zero, 0, out width, out height);
        if (width <= 0 || height <= 0) return null;
        int stride = checked(width * 4);
        var pixels = new byte[checked(stride * height)];
        int rc = VncInterop.vnc_copy_frame(_handle, pixels, (UIntPtr)pixels.Length,
            stride, out int actualWidth, out int actualHeight);
        if (rc != 0 || actualWidth != width || actualHeight != height) return null;
        return pixels;
    }

    public void StartLoop()
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_start_loop(_handle);
    }

    public void Stop()
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_stop(_handle);
    }

    public void SetFramebuffer(IntPtr data, int width, int height, int stride)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_set_framebuffer(_handle, data, width, height, stride);
    }

    public void SendKey(int systemKeyCode, uint keyCode, uint keySym)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_send_key(_handle, systemKeyCode, keyCode, keySym);
    }

    public void SendKeyRelease(int systemKeyCode)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_send_key_release(_handle, systemKeyCode);
    }

    public void SendPointer(int x, int y, ushort buttonmask)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_send_pointer(_handle, x, y, buttonmask);
    }

    public void Refresh()
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_refresh(_handle);
    }

    /// <summary>
    /// 是否共享会话。必须在 <see cref="Connect"/> 之前调用。
    /// 旧核心 DLL 没有这个导出时安静跳过：共享只是"不断开其他查看器"，
    /// 缺它不影响连接本身，绝不能因此让用户连不上。
    /// </summary>
    public void SetShared(bool shared)
    {
        if (_handle == IntPtr.Zero) return;
        try { VncInterop.vnc_set_shared(_handle, shared ? 1 : 0); }
        catch (EntryPointNotFoundException) { /* 旧核心：保持默认（不共享）。 */ }
        catch (DllNotFoundException) { /* 交给 Connect 报错，不在这里重复。 */ }
    }

    /// <summary>
    /// 设置安全类型（逗号分隔的 TigerVNC 名字）。必须在 <see cref="Connect"/> 之前调用。
    /// 返回 true 表示核心接受了这份组合；false 表示有名字本核心不认识，
    /// 整份被拒绝。调用方应把这个错误如实告诉用户，不能假装设置成功。
    /// </summary>
    public bool SetSecurityTypes(string types)
    {
        if (_handle == IntPtr.Zero) return false;
        try { return VncInterop.vnc_set_security_types(_handle, types) == 0; }
        catch (EntryPointNotFoundException) { return false; }
        catch (DllNotFoundException) { return false; }
    }

    /// <summary>本核心实际编译进去的安全类型（逗号分隔），供界面把不支持的项置灰。</summary>
    public static string AvailableSecurityTypes()
    {
        var buffer = new byte[512];
        try
        {
            int written = VncInterop.vnc_security_types(buffer, (UIntPtr)buffer.Length);
            return written < 0 ? "" : DecodeUtf8CString(buffer);
        }
        catch (EntryPointNotFoundException) { return ""; }
        catch (DllNotFoundException) { return ""; }
    }

    public void SetX509CA(string path) => TrySetX509(path, isCrl: false);
    public void SetX509CRL(string path) => TrySetX509(path, isCrl: true);

    /// <summary>
    /// 是否协商并播放服务器音频。必须在 <see cref="Connect"/> 之前调用：
    /// 音频能力位随 SetEncodings 一起发出。
    /// </summary>
    public void SetAudioEnabled(bool enabled)
    {
        if (_handle == IntPtr.Zero) return;
        try { VncInterop.vnc_set_audio_enabled(_handle, enabled ? 1 : 0); }
        catch (EntryPointNotFoundException) { /* 旧核心没有音频，忽略。 */ }
        catch (DllNotFoundException) { /* 交给 Connect 报错。 */ }
    }

    /// <summary>音频后端是否可用（没有播放设备时为 false）。</summary>
    public bool IsAudioAvailable
    {
        get
        {
            if (_handle == IntPtr.Zero) return false;
            try { return VncInterop.vnc_audio_available(_handle) != 0; }
            catch (EntryPointNotFoundException) { return false; }
            catch (DllNotFoundException) { return false; }
        }
    }

    /// <summary>网络统计快照：累计帧更新数、解码像素数、收到字节数、带宽估算。</summary>
    public (uint Updates, uint Pixels, uint Bytes, ulong BitsPerSecond) GetStats()
    {
        if (_handle == IntPtr.Zero) return (0, 0, 0, 0);
        try
        {
            VncInterop.vnc_get_stats(_handle, out uint updates, out uint pixels,
                out uint bytes, out ulong bitsPerSecond);
            return (updates, pixels, bytes, bitsPerSecond);
        }
        catch (EntryPointNotFoundException) { return (0, 0, 0, 0); }
        catch (DllNotFoundException) { return (0, 0, 0, 0); }
    }

    /// <summary>
    /// 按实测带宽自动调整 JPEG 质量（原版 autoSelect 行为）。
    /// 必须在 <see cref="Connect"/> 之前调用。
    /// </summary>
    public void SetAutoSelectQuality(bool enabled)
    {
        if (_handle == IntPtr.Zero) return;
        try { VncInterop.vnc_set_auto_select_quality(_handle, enabled ? 1 : 0); }
        catch (EntryPointNotFoundException) { /* 旧核心：保持固定质量。 */ }
        catch (DllNotFoundException) { }
    }

    /// <summary>
    /// 请求服务器把远程桌面设成指定尺寸（RemoteResize）。
    /// 服务器不支持 SetDesktopSize 时会忽略——不保证一定生效。
    /// </summary>
    public void RequestRemoteSize(int width, int height)
    {
        if (_handle == IntPtr.Zero) return;
        try { VncInterop.vnc_request_remote_size(_handle, width, height); }
        catch (EntryPointNotFoundException) { /* 旧核心：忽略。 */ }
        catch (DllNotFoundException) { }
    }

    private void TrySetX509(string path, bool isCrl)
    {
        if (_handle == IntPtr.Zero) return;
        try
        {
            if (isCrl) VncInterop.vnc_set_x509_crl(_handle, path);
            else VncInterop.vnc_set_x509_ca(_handle, path);
        }
        catch (EntryPointNotFoundException) { /* 旧核心或未编译 GnuTLS：忽略。 */ }
        catch (DllNotFoundException) { /* 交给 Connect 报错。 */ }
    }

    public void SendClipboard(string text)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_send_clipboard(_handle, text);
    }

    /// <summary>向服务器请求当前剪贴板内容（收到远端 announce 后调用）。</summary>
    public void RequestClipboard()
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_request_clipboard(_handle);
    }

    /// <summary>告诉服务器本机剪贴板是否有内容；false 表示请服务器推送当前内容。</summary>
    public void AnnounceClipboard(bool available)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_announce_clipboard(_handle, available ? 1 : 0);
    }

    public void SetEncoding(int encoding)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_set_encoding(_handle, encoding);
    }

    public void SetCompressLevel(int level)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_set_compress_level(_handle, level);
    }

    public void SetQualityLevel(int level)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_set_quality_level(_handle, level);
    }

    public void RequestScreenLayout(int width, int height, int[] screens, int count)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_request_screen_layout(_handle, width, height, screens, count);
    }

    private void OnServerInit(IntPtr ud, int w, int h, string? name,
        int bpp, int depth, int rs, int gs, int bs)
    {
        try { ServerInit?.Invoke(w, h, name ?? ""); }
        catch (Exception) { ReportCallbackFailure(nameof(OnServerInit)); }
    }

    private void OnFrameUpdate(IntPtr ud, int x, int y, int w, int h)
    {
        try { FrameUpdate?.Invoke(x, y, w, h); }
        catch (Exception) { ReportCallbackFailure(nameof(OnFrameUpdate)); }
    }

    private void OnError(IntPtr ud, string? message, int fatal)
    {
        string reason = message ?? "";
        if (fatal != 0)
        {
            if (string.IsNullOrWhiteSpace(reason)) reason = "远程会话已断开。";
            Interlocked.CompareExchange(ref _fatalError, reason, null);
            reason = FatalError!;
        }
        try { Error?.Invoke(reason, fatal != 0); }
        catch (Exception) { ReportCallbackFailure(nameof(OnError)); }
    }

    private int OnVerifyCert(IntPtr ud, uint status, IntPtr cert, UIntPtr len)
    {
        try
        {
            var verifier = CertificateVerifier;
            if (verifier is null)
            {
                // 没有界面接管就拒绝；未知证书不能因为缺实现而被放行。
                ReportCallbackFailure(nameof(OnVerifyCert));
                return 0;
            }

            byte[] bytes = ReadBuffer(cert, len);
            string fingerprint = TrustFingerprint.ComputeSha256(bytes);
            System.Threading.Interlocked.Exchange(ref _trustFingerprint, fingerprint);
            return verifier(status, bytes, fingerprint) ? 1 : 0;
        }
        catch (Exception)
        {
            ReportCallbackFailure(nameof(OnVerifyCert));
            return 0;
        }
    }

    private int OnVerifyHostkey(IntPtr ud, IntPtr key, UIntPtr len, string? fp)
    {
        try
        {
            var verifier = HostKeyVerifier;
            if (verifier is null)
            {
                ReportCallbackFailure(nameof(OnVerifyHostkey));
                return 0;
            }

            byte[] bytes = ReadBuffer(key, len);
            string fingerprint = string.IsNullOrWhiteSpace(fp)
                ? TrustFingerprint.ComputeSha256(bytes)
                : fp;
            System.Threading.Interlocked.Exchange(ref _trustFingerprint, fingerprint);
            return verifier(bytes, fingerprint) ? 1 : 0;
        }
        catch (Exception)
        {
            ReportCallbackFailure(nameof(OnVerifyHostkey));
            return 0;
        }
    }

    /// <summary>按原生长度拷贝缓冲区；长度异常时直接拒绝，避免越界读。</summary>
    private static byte[] ReadBuffer(IntPtr data, UIntPtr length)
    {
        ulong size = length.ToUInt64();
        if (size == 0)
            return Array.Empty<byte>();
        if (data == IntPtr.Zero)
            throw new ArgumentException("Missing verifier buffer");
        // 证书和公钥都不可能接近 int 上限；异常长度按不可信处理。
        if (size > int.MaxValue)
            throw new ArgumentOutOfRangeException(nameof(length));

        var bytes = new byte[(int)size];
        Marshal.Copy(data, bytes, 0, bytes.Length);
        return bytes;
    }

    private void OnCursor(IntPtr ud, int width, int height, int hotX, int hotY, IntPtr rgba)
    {
        try
        {
            // 尺寸和指针都要先校验：原生端可能传空光标（0×0）表示"用默认形状"。
            if (width <= 0 || height <= 0)
            {
                CursorChanged?.Invoke(0, 0, 0, 0, Array.Empty<byte>());
                return;
            }
            long bytes = (long)width * height * 4;
            if (bytes > int.MaxValue || rgba == IntPtr.Zero)
            {
                ReportCallbackFailure(nameof(OnCursor));
                return;
            }

            // 回调返回后原生缓冲可能被复用，必须当场拷贝。
            var pixels = new byte[(int)bytes];
            Marshal.Copy(rgba, pixels, 0, pixels.Length);
            CursorChanged?.Invoke(width, height, hotX, hotY, pixels);
        }
        catch (Exception) { ReportCallbackFailure(nameof(OnCursor)); }
    }

    private void OnClipboard(IntPtr ud, string? text)
    {
        try { ClipboardReceived?.Invoke(text ?? ""); }
        catch (Exception) { ReportCallbackFailure(nameof(OnClipboard)); }
    }

    /// <summary>扩展剪贴板：远端剪贴板内容有变化（available=0 表示被清空）。</summary>
    private void OnClipboardAnnounce(IntPtr ud, int available)
    {
        try { ClipboardAnnounced?.Invoke(available != 0); }
        catch (Exception) { ReportCallbackFailure(nameof(OnClipboardAnnounce)); }
    }

    /// <summary>扩展剪贴板：服务器要求我们发送本地剪贴板。</summary>
    private void OnClipboardRequest(IntPtr ud)
    {
        try { ClipboardRequested?.Invoke(); }
        catch (Exception) { ReportCallbackFailure(nameof(OnClipboardRequest)); }
    }

    private void OnBell(IntPtr ud)
    {
        try { Bell?.Invoke(); }
        catch (Exception) { ReportCallbackFailure(nameof(OnBell)); }
    }

    private void OnLed(IntPtr ud, uint state)
    {
        try { LedChanged?.Invoke(state); }
        catch (Exception) { ReportCallbackFailure(nameof(OnLed)); }
    }

    private void OnName(IntPtr ud, string? name)
    {
        try { NameChanged?.Invoke(name ?? ""); }
        catch (Exception) { ReportCallbackFailure(nameof(OnName)); }
    }

    private static void ReportCallbackFailure(string callback)
    {
        // 不能转发到 Error：它自己的订阅者也可能抛异常。不要记录异常或回调数据。
        try { System.Diagnostics.Trace.WriteLine($"VNC managed callback failed: {callback}"); }
        catch (Exception) { /* 诊断监听器也不能把异常带回原生线程。 */ }
    }

    private int OnCredentials(IntPtr ud, string? host, int secure,
        IntPtr user, UIntPtr userLen, IntPtr pass, UIntPtr passLen)
    {
        byte[]? userBytes = null;
        byte[]? passBytes = null;
        try
        {
            var provider = CredentialProvider;
            if (provider is null)
                return 0;

            var result = provider(host ?? "", secure != 0,
                userLen != UIntPtr.Zero, passLen != UIntPtr.Zero);
            if (result is null)
                return 0;

            int userCapacity = checked((int)userLen.ToUInt64());
            int passCapacity = checked((int)passLen.ToUInt64());
            // 两个字段都完整校验成功后才开始写，绝不截断凭据后报成功。
            userBytes = EncodeCredential(user, userCapacity, result.Value.User);
            passBytes = EncodeCredential(pass, passCapacity, result.Value.Password);
            WriteUtf8(user, userCapacity, userBytes);
            WriteUtf8(pass, passCapacity, passBytes);
            return 1;
        }
        catch (Exception)
        {
            ReportCallbackFailure(nameof(OnCredentials));
            return 0;
        }
        finally
        {
            if (userBytes is not null) Array.Clear(userBytes);
            if (passBytes is not null) Array.Clear(passBytes);
        }
    }

    private static byte[]? EncodeCredential(IntPtr dest, int capacity, string value)
    {
        if (capacity == 0)
            return null; // 原生端不需要该字段。
        if (dest == IntPtr.Zero)
            throw new ArgumentException("Missing credential buffer");
        ArgumentNullException.ThrowIfNull(value);
        if (value.Contains('\0'))
            throw new ArgumentException("Embedded credential terminator");

        var encoding = new System.Text.UTF8Encoding(false, true);
        if (encoding.GetByteCount(value) >= capacity)
            throw new ArgumentException("Credential buffer too small");
        return encoding.GetBytes(value);
    }

    /// <summary>只写入已完整校验的 UTF-8 字节，保留结尾 NUL 并清空尾部。</summary>
    private static void WriteUtf8(IntPtr dest, int capacity, byte[]? bytes)
    {
        if (bytes is null)
            return;

        Marshal.Copy(bytes, 0, dest, bytes.Length);
        for (int i = bytes.Length; i < capacity; i++)
            Marshal.WriteByte(dest, i, 0);
    }

    private static string DecodeUtf8CString(byte[] buffer)
    {
        int end = Array.IndexOf(buffer, (byte)0);
        if (end < 0)
            end = buffer.Length;
        return System.Text.Encoding.UTF8.GetString(buffer, 0, end);
    }

    /// <summary>
    /// 兜底释放：调用方忘了 Dispose 时，GC 在回收本对象前把原生会话
    /// 关掉。没有这一层的话，原生事件循环会继续跑并调用早已被回收的
    /// 委托——那是野指针，进程直接崩。
    ///
    /// 注意：终结器运行在专门的 GC 线程上，不能碰任何托管对象
    /// （包括锁），所以这里只做「取句柄 + 调 vnc_destroy」。
    /// </summary>
    ~VncSession()
    {
        IntPtr handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle == IntPtr.Zero)
            return;
        try
        {
            VncInterop.vnc_destroy(handle);
        }
        catch
        {
        }
    }

    public void Dispose()
    {
        // 原子地「取走」句柄：并发 Dispose 时只有一个线程拿到非零值，
        // 另一个拿到 Zero 直接返回。若用「先检查再置零」，两个线程都会
        // 通过检查并各调一次 vnc_destroy —— 那是 double free。
        IntPtr handle = Interlocked.Exchange(ref _handle, IntPtr.Zero);
        if (handle == IntPtr.Zero)
            return;

        // 已经走正常释放路径，别让终结器再跑一遍
        GC.SuppressFinalize(this);

        try
        {
            VncInterop.vnc_destroy(handle);
        }
        catch
        {
            // DLL 没导出该符号时不至于把 UI 拖垮
        }
    }
}
