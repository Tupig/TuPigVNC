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

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern void vnc_request_screen_layout(IntPtr s, int width,
        int height, [In] int[] screens, int count);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern int vnc_test_add(int a, int b);

    [DllImport(Dll, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr vnc_version();

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

    /// <summary>服务器名称/桌面名改变。</summary>
    public event Action<string>? NameChanged;

    /// <summary>服务器剪贴板文本。</summary>
    public event Action<string>? ClipboardReceived;

    /// <summary>
    /// 需要用户名/密码时调用。返回 null 表示取消连接。
    /// 参数：host、是否加密、是否需要用户名、是否需要密码。
    /// 该委托在后台网络线程上调用，实现方必须自行切回 UI 线程。
    /// </summary>
    public Func<string, bool, bool, bool, (string User, string Password)?>? CredentialProvider { get; set; }

    public VncSession()
    {
        _onServerInit = (ud, w, h, name, bpp, depth, rs, gs, bs) =>
            ServerInit?.Invoke(w, h, name ?? "");
        _onFrameUpdate = (ud, x, y, w, h) => FrameUpdate?.Invoke(x, y, w, h);
        _onError = (ud, msg, fatal) => Error?.Invoke(msg ?? "", fatal != 0);
        _onCredentials = OnCredentials;
        _onVerifyCert = (ud, status, cert, len) => 0;      // 阶段 3 接入证书确认
        _onVerifyHostkey = (ud, key, len, fp) => 0;        // 阶段 3 接入指纹确认
        _onClipboard = (ud, text) => ClipboardReceived?.Invoke(text ?? "");
        _onCursor = (ud, w, h, hx, hy, rgba) => { };
        _onCursorPos = (ud, x, y) => { };
        _onName = (ud, name) => NameChanged?.Invoke(name ?? "");
        _onLed = (ud, state) => { };
        _onResize = (ud, w, h) => { };
        _onBell = ud => { };

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

    public void SendClipboard(string text)
    {
        if (_handle != IntPtr.Zero)
            VncInterop.vnc_send_clipboard(_handle, text);
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

    private int OnCredentials(IntPtr ud, string? host, int secure,
        IntPtr user, UIntPtr userLen, IntPtr pass, UIntPtr passLen)
    {
        var provider = CredentialProvider;
        if (provider is null)
            return 0;

        var result = provider(host ?? "", secure != 0, userLen != UIntPtr.Zero, passLen != UIntPtr.Zero);
        if (result is null)
            return 0;

        WriteUtf8(user, (int)userLen.ToUInt64(), result.Value.User);
        WriteUtf8(pass, (int)passLen.ToUInt64(), result.Value.Password);
        return 1;
    }

    /// <summary>把 UTF-8 字符串写进原生缓冲，保留结尾 \0，超出容量时截断。</summary>
    private static void WriteUtf8(IntPtr dest, int capacity, string value)
    {
        if (dest == IntPtr.Zero || capacity <= 0)
            return;

        byte[] bytes = System.Text.Encoding.UTF8.GetBytes(value);
        int n = Math.Min(bytes.Length, capacity - 1);
        Marshal.Copy(bytes, 0, dest, n);
        Marshal.WriteByte(dest, n, 0);

        // 清掉剩余部分，避免残留旧内容
        for (int i = n + 1; i < capacity; i++)
            Marshal.WriteByte(dest, i, 0);
    }

    private static string DecodeUtf8CString(byte[] buffer)
    {
        int end = Array.IndexOf(buffer, (byte)0);
        if (end < 0)
            end = buffer.Length;
        return System.Text.Encoding.UTF8.GetString(buffer, 0, end);
    }

    public void Dispose()
    {
        if (_handle == IntPtr.Zero)
            return;

        try
        {
            VncInterop.vnc_destroy(_handle);
        }
        catch
        {
            // DLL 没导出该符号时不至于把 UI 拖垮
        }
        finally
        {
            _handle = IntPtr.Zero;
        }
    }
}
