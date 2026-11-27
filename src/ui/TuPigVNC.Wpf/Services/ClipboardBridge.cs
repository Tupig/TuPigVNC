using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Threading;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 远程剪贴板与 Windows 剪贴板的双向同步。
///
/// 语义对照原版 <c>Viewport::handleClipboardChange</c> /
/// <c>handleClipboardAnnounce</c> / <c>handleClipboardData</c>：
/// <list type="bullet">
/// <item>仅查看模式完全不参与剪贴板。</item>
/// <item>只处理纯文本；非文本内容忽略，不当作空文本发出去。</item>
/// <item>会话窗口失焦时本地变化不同步到远端，避免后台复制的敏感内容泄露。</item>
/// <item>远端写入本机剪贴板后的一小段时间内，本地变化不再回传（防回环）。</item>
/// <item>断线时只清空「本次会话确实收到过远端内容」的剪贴板，
///       不会无故清掉用户自己复制的东西。</item>
/// </list>
/// WPF 没有跨进程的剪贴板变更事件，所以用 Win32 的
/// <c>AddClipboardFormatListener</c> 监听 <c>WM_CLIPBOARDUPDATE</c>；
/// 所有 Windows 剪贴板访问都必须回到 UI 线程（STA）。
/// </summary>
internal sealed class ClipboardBridge : IDisposable
{
    private const int WM_CLIPBOARDUPDATE = 0x031D;

    /// <summary>远端写入本机后，这段时间内的本地变化视为回环。</summary>
    private static readonly TimeSpan LoopbackWindow = TimeSpan.FromMilliseconds(500);

    private readonly VncSession _session;
    private readonly bool _viewOnly;
    private readonly bool _accept;
    private readonly bool _send;
    private readonly Dispatcher _dispatcher;
    private readonly DispatcherTimer _loopbackTimer;
    private HwndSource? _source;
    private bool _remoteWrite;
    private bool _receivedRemote;
    private bool _hasFocus = true;
    private bool _disposed;

    public ClipboardBridge(VncSession session, bool viewOnly, bool accept, bool send,
        Dispatcher dispatcher)
    {
        _session = session;
        _viewOnly = viewOnly;
        _accept = accept && !viewOnly;
        _send = send && !viewOnly;
        _dispatcher = dispatcher;
        // 用局部变量而不是字段避免"可能为空"的警告：定时器回调只在自己活着时触发。
        DispatcherTimer? timer = null;
        timer = new DispatcherTimer(LoopbackWindow, DispatcherPriority.Background,
            (_, _) => { timer?.Stop(); _remoteWrite = false; }, dispatcher);
        _loopbackTimer = timer;
    }

    /// <summary>
    /// 开始监听。必须在 UI 线程上、且窗口已有 HWND 时调用。
    /// </summary>
    public void Start(Window window)
    {
        if (_viewOnly || _disposed) return;
        _source = PresentationSource.FromVisual(window) as HwndSource;
        if (_source is not null)
        {
            _source.AddHook(WndProc);
            AddClipboardFormatListener(_source.Handle);
        }

        if (_accept)
        {
            _session.ClipboardReceived += OnRemoteClipboard;
            // 扩展剪贴板：服务器通知远端内容有变化 → 主动拉取。
            // 没有这两条订阅，新版 TigerVNC 的剪贴板完全不通。
            _session.ClipboardAnnounced += OnRemoteAnnounce;
            _session.ClipboardRequested += OnRemoteRequest;
            // 告知服务器我们的剪贴板为空，让它有机会推送当前内容。
            _session.AnnounceClipboard(false);
        }
    }

    /// <summary>远端剪贴板有变化：有内容就拉取，清空则什么都不做（不覆盖本地）。</summary>
    private void OnRemoteAnnounce(bool available)
    {
        if (_disposed || !_accept || !available) return;
        try { _session.RequestClipboard(); }
        catch (Exception) { /* 连接刚断时可能失败，不算错误。 */ }
    }

    /// <summary>服务器要求我们发送本地剪贴板，语义同 <see cref="PushLocalClipboard"/>。</summary>
    private void OnRemoteRequest()
    {
        // 服务器主动要，说明它确实需要这份数据，焦点检查仍需遵守——
        // 失焦期间不能把用户后来复制的内容泄露出去。
        PushLocalClipboard();
    }

    /// <summary>会话窗口获得/失去焦点。失焦期间本地变化不同步到远端。</summary>
    public void SetFocus(bool focused)
    {
        if (_viewOnly) return;
        _hasFocus = focused;
    }

    private IntPtr WndProc(IntPtr hwnd, int msg, IntPtr wParam, IntPtr lParam, ref bool handled)
    {
        if (msg == WM_CLIPBOARDUPDATE) PushLocalClipboard();
        return IntPtr.Zero;
    }

    /// <summary>本地剪贴板变化 → 远端。失焦、仅查看、关闭发送时都不回传。</summary>
    private void PushLocalClipboard()
    {
        if (_disposed || _viewOnly || !_send || !_hasFocus) return;
        // 这次变化是远端写入触发的，不能再回传，否则会无限循环。
        if (_remoteWrite) return;

        string? text;
        try { text = GetLocalText(); }
        catch (Exception) { return; }   // 剪贴板被别的程序占着时安静跳过，不打断会话。
        if (text is null || text.Length == 0) return;

        _session.SendClipboard(text);
    }

    private void OnRemoteClipboard(string text)
    {
        if (_disposed || !_accept) return;
        _dispatcher.BeginInvoke(() =>
        {
            if (_disposed) return;
            // 空文本代表服务器剪贴板被清空，不覆盖本地内容。
            // 也不能把它算作「本会话同步过远端内容」——否则断开时会清掉
            // 用户自己复制的、与会话无关的东西。
            if (text.Length == 0) return;
            if (!_hasFocus) return;     // 失焦时收到也不写本机，避免后台篡改用户剪贴板。

            _remoteWrite = true;
            _loopbackTimer.Stop();
            _loopbackTimer.Start();
            try
            {
                SetLocalText(text);
                // 只有确实写进了本机剪贴板，断开时才该清空它。
                _receivedRemote = true;
            }
            catch (Exception) { _remoteWrite = false; _loopbackTimer.Stop(); }
        });
    }

    /// <summary>
    /// 只清空「本次会话确实同步过远端内容」的剪贴板。
    /// 先置位回环标记，防止这次清空被当成用户复制再发回远端。
    /// </summary>
    public void ClearOnDisconnect()
    {
        if (_disposed || _viewOnly || !_receivedRemote) return;
        _remoteWrite = true;
        try { SetLocalText(""); }
        catch (Exception) { /* 别的程序占着剪贴板时放弃清空，但会话结束不受影响。 */ }
        finally { _remoteWrite = false; }
    }

    private static string? GetLocalText()
    {
        var data = Clipboard.GetDataObject();
        return data is not null && data.GetDataPresent(DataFormats.UnicodeText)
            ? data.GetData(DataFormats.UnicodeText) as string
            : null;
    }

    private static void SetLocalText(string text) => Clipboard.SetText(text);

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _loopbackTimer.Stop();
        if (_source is not null)
        {
            RemoveClipboardFormatListener(_source.Handle);
            _source.RemoveHook(WndProc);
            _source = null;
        }
        if (_accept)
        {
            _session.ClipboardReceived -= OnRemoteClipboard;
            _session.ClipboardAnnounced -= OnRemoteAnnounce;
            _session.ClipboardRequested -= OnRemoteRequest;
        }
    }

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool AddClipboardFormatListener(IntPtr hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RemoveClipboardFormatListener(IntPtr hwnd);
}
