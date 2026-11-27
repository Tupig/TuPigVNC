using System.ComponentModel;
using System.Diagnostics;
using System.Windows;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Threading;
using TuPigVNC.Wpf.Services;

namespace TuPigVNC.Wpf;

public partial class SessionWindow : Window
{
    private readonly VncSession _session;
    private readonly bool _viewOnly;
    private readonly ClipboardBridge _clipboard;
    private readonly MiddleButtonEmulator? _middle;
    private readonly PointerThrottle _pointer;
    private readonly KeyboardGrabber? _grabber;
    private readonly ShortcutHandler? _shortcut;
    private readonly bool _alwaysCursor;
    private readonly DispatcherTimer _timer;
    private readonly DispatcherTimer _middleTimer;
    private readonly DispatcherTimer _statsTimer;
    private (uint Updates, uint Pixels, uint Bytes, ulong BitsPerSecond) _lastStats;
    private long _lastStatsTicks;
    private readonly HashSet<int> _keys = new();
    private WriteableBitmap? _bitmap;
    private bool _closing;
    private bool _closed;
    private bool _fullscreen;
    private bool _originalSize;
    private WindowState _windowedState;
    private Rect? _windowedBounds;
    private readonly string _displayMode;
    private readonly IReadOnlyCollection<int> _selectedMonitors;
    private readonly bool _remoteResize;
    private readonly DispatcherTimer _resizeTimer;
    private string _fullscreenNotice = "";
    internal string? TerminationError { get; private set; }
    private ushort _buttons;
    private (int X, int Y) _lastPoint;

    internal SessionWindow(VncSession session, string name, bool viewOnly)
        : this(session, name, viewOnly, acceptClipboard: true, sendClipboard: true,
            emulateMiddle: false, grabSystemKeys: false)
    {
    }

    internal SessionWindow(VncSession session, string name, bool viewOnly,
        bool acceptClipboard, bool sendClipboard, bool emulateMiddle, bool grabSystemKeys,
        bool alwaysCursor = true, string displayMode = "窗口",
        ShortcutHandler.Modifier shortcutModifiers = ShortcutHandler.Modifier.None,
        IReadOnlyCollection<int>? selectedMonitors = null, bool remoteResize = false,
        int pointerEventInterval = 17)
    {
        InitializeComponent();
        _session = session;
        _viewOnly = viewOnly;
        _alwaysCursor = alwaysCursor;
        _displayMode = displayMode;
        _selectedMonitors = selectedMonitors ?? Array.Empty<int>();
        _remoteResize = remoteResize && !viewOnly;
        // 远程调整大小限速：原版是 100ms 一次，避免拖拽窗口时
        // 每像素都发一个 SetDesktopSize。用局部变量避免「可能为空」的警告。
        DispatcherTimer? resizeTimer = null;
        resizeTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(100),
            DispatcherPriority.Background, (_, _) => { resizeTimer?.Stop(); PushRemoteSize(); }, Dispatcher);
        _resizeTimer = resizeTimer;
        // 修饰键组合来自设置；空组合表示不拦截任何按键（全部直接发给远端）。
        if (shortcutModifiers != ShortcutHandler.Modifier.None)
        {
            _shortcut = new ShortcutHandler();
            _shortcut.SetModifiers(shortcutModifiers);
        }
        _clipboard = new ClipboardBridge(session, viewOnly, acceptClipboard, sendClipboard, Dispatcher);
        // 鼠标限速：按键变化立即发，纯移动按 PointerEventInterval 合并。
        _pointer = new PointerThrottle(
            (x, y, mask) => _session.SendPointer(x, y, mask),
            pointerEventInterval, Dispatcher);
        if (emulateMiddle && !viewOnly)
        {
            _middleTimer = new DispatcherTimer(TimeSpan.FromMilliseconds(MiddleButtonEmulator.TimeoutMs),
                DispatcherPriority.Input, (_, _) => { _middleTimer!.Stop(); _middle?.HandleTimeout(); }, Dispatcher);
            _middle = new MiddleButtonEmulator(
                (x, y, mask) => _session.SendPointer(x, y, mask),
                _ => _middleTimer.Start(),
                () => _middleTimer.Stop());
        }
        else _middleTimer = new DispatcherTimer(DispatcherPriority.Input, Dispatcher);

        // 全屏接管系统组合键：只有本窗口全屏且是前台窗口时才拦，别的时候完全放行。
        if (grabSystemKeys && !viewOnly)
            _grabber = new KeyboardGrabber(
                (key, down) => ForwardGrabbedKey(key, down),
                () => _fullscreen && IsActive && !_closing);
        Title = $"{name} — TuPigVNC";
        ConnectionText.Text = viewOnly ? $"{name} · 仅查看" : name;
        _timer = new DispatcherTimer(TimeSpan.FromMilliseconds(33), DispatcherPriority.Render,
            (_, _) => RenderFrame(), Dispatcher);
        // 网络统计 1 秒刷新一次：太频繁只会让界面抖动，看不出趋势。
        _statsTimer = new DispatcherTimer(TimeSpan.FromSeconds(1), DispatcherPriority.Background,
            (_, _) => RefreshStats(), Dispatcher);
        SourceInitialized += (_, _) => App.ApplyDarkTitleBar(this);
        SourceInitialized += (_, _) => _clipboard.Start(this);
        Loaded += (_, _) => DesktopImage.Focus();
        Activated += Window_Activated;
        // 窗口尺寸变化时，如果开了远程调整大小就把新尺寸发给服务器。
        // 用 ClientSize 而不是 Window 尺寸：边框不算进远程桌面。
        SizeChanged += Window_SizeChanged;
        _session.Error += OnError;
        _session.CursorChanged += OnRemoteCursor;
        _session.Bell += OnRemoteBell;
        _session.NameChanged += OnRemoteName;
        if (_session.FatalError is { } reason) OnError(reason, true);

        // 启动渲染定时器。它是**唯一**把核心的帧缓冲写进 WriteableBitmap 的路径：
        // 不启动的话远程画面永远不显示，缩放按钮永远禁用，网络统计也不会开始。
        _timer.Start();
    }

    /// <summary>服务器响铃。原版就是系统提示音，不自定义音效。</summary>
    private void OnRemoteBell() => Dispatcher.BeginInvoke(() =>
    {
        if (!_closing) System.Media.SystemSounds.Beep.Play();
    });

    /// <summary>桌面名称改变（比如服务器重启换了名字）。标题要跟着变，不能一直显示旧名。</summary>
    private void OnRemoteName(string name) => Dispatcher.BeginInvoke(() =>
    {
        if (_closing || string.IsNullOrWhiteSpace(name)) return;
        Title = $"{name} — TuPigVNC";
        ConnectionText.Text = _viewOnly ? $"{name} · 仅查看" : name;
        System.Windows.Automation.AutomationProperties.SetName(ConnectionText, name);
    });

    /// <summary>
    /// 远端光标形状。服务器给了自定义光标就用它替换本机鼠标指针，
    /// 热点按原版语义对齐；空光标表示恢复系统默认。
    /// </summary>
    private void OnRemoteCursor(int width, int height, int hotX, int hotY, byte[] rgba)
    {
        Dispatcher.BeginInvoke(() =>
        {
            if (_closing) return;
            // 关掉"显示本地光标"时不接管指针，交给系统默认。
            if (!_alwaysCursor) { Cursor = null; return; }
            try
            {
                Cursor = width <= 0 || height <= 0 || rgba.Length < (long)width * height * 4
                    ? System.Windows.Input.Cursors.Arrow
                    : BuildCursor(width, height, hotX, hotY, rgba);
            }
            catch (Exception ex)
            {
                // 自定义光标失败不影响画面和输入，退回箭头即可。
                Debug.WriteLine($"[cursor] {ex.GetType().Name}");
                Cursor = System.Windows.Input.Cursors.Arrow;
            }
        });
    }

    private static System.Windows.Input.Cursor BuildCursor(int width, int height,
        int hotX, int hotY, byte[] rgba)
    {
        // WPF 的 Cursor 只吃 .cur 流，不能直接塞位图。
        // 转成 32 位 DIB 再用 CreateIconIndirect 造 HICON，最后包成 Cursor。
        var stream = RemoteCursorImage.BuildCursorStream(width, height, hotX, hotY, rgba);
        return new System.Windows.Input.Cursor(stream);
    }

    private void OnError(string message, bool fatal)
    {
        Dispatcher.BeginInvoke(new Action(() =>
        {
            if (_closing) return;
            StatusText.Text = message;
            if (fatal)
            {
                TerminationError = message;
                Close();
            }
        }));
    }

    private void RenderFrame()
    {
        if (_closing) return;
        try
        {
            var pixels = _session.CopyFrame(out int w, out int h);
            if (pixels is null) return;
            if (_bitmap is null || _bitmap.PixelWidth != w || _bitmap.PixelHeight != h)
            {
                _bitmap = new WriteableBitmap(w, h, 96, 96, PixelFormats.Bgr32, null);
                DesktopImage.Source = _bitmap;
                FrameInfoText.Text = $"{w} × {h}";
                StatusText.Text = "已收到远程画面";
                WaitingPanel.Visibility = Visibility.Collapsed;
                ScaleButton.IsEnabled = true;
                ApplyScale();
                // 第一帧到了才开始统计：之前的数据都是 0，显示出来没意义。
                _lastStats = _session.GetStats();
                _lastStatsTicks = System.Diagnostics.Stopwatch.GetTimestamp();
                _statsTimer.Start();
            }
            _bitmap.WritePixels(new Int32Rect(0, 0, w, h), pixels, checked(w * 4), 0);
        }
        catch (Exception ex)
        {
            TerminationError = "显示失败：" + ex.Message;
            StatusText.Text = TerminationError;
            Close();
        }
    }

    /// <summary>
    /// 刷新网络统计。算法对照原版 CConn 的每秒采样：
    /// 帧率 = 一秒内的更新数；带宽 = 一秒内新增字节数 × 8；
    /// 像素率直接给累计值。计数器可能回退（重连后归零），
    /// 用无符号差值会绕回天文数字，所以先判断大小再相减。
    /// </summary>
    private void RefreshStats()
    {
        if (_closing) return;
        try
        {
            var now = _session.GetStats();
            long ticks = System.Diagnostics.Stopwatch.GetTimestamp();
            double seconds = _lastStatsTicks == 0
                ? 1.0
                : Math.Max(0.001, (ticks - _lastStatsTicks) / (double)System.Diagnostics.Stopwatch.Frequency);

            static uint Delta(uint current, uint previous) =>
                current >= previous ? current - previous : 0;

            double fps = Delta(now.Updates, _lastStats.Updates) / seconds;
            double mps = Delta(now.Pixels, _lastStats.Pixels) / seconds / 1_000_000.0;
            // 带宽用核心的滑动平均估算，比单帧差分稳。
            double mbps = now.BitsPerSecond / 1_000_000.0;

            StatsText.Text = $"{fps:0.0} fps · {mps:0.00} Mpx/s · {mbps:0.0} Mbps · 累计 {now.Bytes / 1024.0 / 1024.0:0.0} MB";

            _lastStats = now;
            _lastStatsTicks = ticks;
        }
        catch (Exception ex)
        {
            // 统计失败不该影响会话本身，停掉定时器即可。
            Debug.WriteLine($"[stats] {ex.GetType().Name}");
            _statsTimer.Stop();
            StatsText.Text = "统计不可用";
        }
    }

    private bool MapPoint(MouseEventArgs e, out int x, out int y)
    {
        x = y = 0;
        if (_bitmap is null) return false;
        var pos = e.GetPosition(DesktopImage);
        double scale = Math.Min(DesktopImage.ActualWidth / _bitmap.PixelWidth,
            DesktopImage.ActualHeight / _bitmap.PixelHeight);
        if (scale <= 0) return false;
        double left = (DesktopImage.ActualWidth - _bitmap.PixelWidth * scale) / 2;
        double top = (DesktopImage.ActualHeight - _bitmap.PixelHeight * scale) / 2;
        x = Math.Clamp((int)((pos.X - left) / scale), 0, _bitmap.PixelWidth - 1);
        y = Math.Clamp((int)((pos.Y - top) / scale), 0, _bitmap.PixelHeight - 1);
        return _buttons != 0 || (pos.X >= left && pos.Y >= top &&
            pos.X < left + _bitmap.PixelWidth * scale && pos.Y < top + _bitmap.PixelHeight * scale);
    }

    private void Desktop_MouseMove(object sender, MouseEventArgs e)
    {
        if (_closing || _viewOnly || !MapPoint(e, out int x, out int y)) return;
        _lastPoint = (x, y);
        if (_middle is not null) _middle.Move(x, y, _buttons);
        else _pointer.Move(x, y, _buttons);
    }
    private static ushort ButtonMask(MouseButton button) => button switch
    { MouseButton.Left => 1, MouseButton.Middle => 2, MouseButton.Right => 4,
      MouseButton.XButton1 => 128, MouseButton.XButton2 => 256, _ => 0 };
    private void Desktop_MouseDown(object sender, MouseButtonEventArgs e)
    {
        if (_closing || _viewOnly || !MapPoint(e, out _, out _)) return;
        DesktopImage.Focus();
        DesktopImage.CaptureMouse();
        _buttons |= ButtonMask(e.ChangedButton);
        // 左/右键按下只是状态变化：开了中键模拟时由状态机决定延迟还是合并成中键。
        Desktop_MouseMove(sender, e);
        e.Handled = true;
    }
    private void Desktop_MouseUp(object sender, MouseButtonEventArgs e)
    {
        if (_closing || _viewOnly) return;
        bool wasPressed = (_buttons & ButtonMask(e.ChangedButton)) != 0;
        // 清除按钮前先映射：捕获期间画面外的释放也必须送到远端。
        bool mapped = MapPoint(e, out int x, out int y);
        _buttons &= (ushort)~ButtonMask(e.ChangedButton);
        if (wasPressed)
        {
            if (mapped) _lastPoint = (x, y);
            if (_middle is not null) _middle.Update(_lastPoint.X, _lastPoint.Y, _buttons);
            else _pointer.Move(_lastPoint.X, _lastPoint.Y, _buttons);
        }
        // 模拟状态机可能还攥着延迟中的按键，不能在这时松掉鼠标捕获。
        if (_buttons == 0 && (_middle is null || !_middle.HasPendingButtons))
            DesktopImage.ReleaseMouseCapture();
        e.Handled = true;
    }
    private void Desktop_MouseWheel(object sender, MouseWheelEventArgs e)
    {
        if (_closing || _viewOnly || !MapPoint(e, out int x, out int y)) return;
        // 模拟出的中键位也要带上，同时**保留**真实的侧键 / 中键按下位。
        // 只取 EmulatedMask 会把 XButton1/2 清成 0，远端以为松开——
        // 比如浏览器会把 XButton1 弹起直接当「后退」执行。
        // 左右键（0x5）交给状态机，其余位原样保留。对照 MiddleButtonEmulator.Move。
        ushort buttons = _middle is null
            ? _buttons
            : (ushort)((_buttons & ~0x5) | _middle.EmulatedMask);
        ushort wheel = e.Delta > 0 ? (ushort)8 : (ushort)16;
        for (int i = 0; i < Math.Max(1, Math.Abs(e.Delta) / 120); i++)
        {
            _session.SendPointer(x, y, (ushort)(buttons | wheel));
            _session.SendPointer(x, y, buttons);
        }
        e.Handled = true;
    }
    private void Window_KeyDown(object sender, KeyEventArgs e)
    {
        if (_closing || _viewOnly || !DesktopImage.IsKeyboardFocused) return;
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;
        uint sym = RemoteKeyMapper.SpecialKey(key);
        if (sym == 0) sym = RemoteKeyMapper.CharacterKey(key);
        if (sym == 0) return;

        // 会话快捷键：识别出「修饰键 + 字母」就执行本地动作，不发给远端。
        // 必须在记录 _keys 之前判断，否则快捷键键会被当成普通键留在按下状态。
        if (_shortcut is not null)
        {
            int physical = RemoteKeyMapper.ScanCode(key) switch {
                var code when code != 0 => (int)code,
                _ => (int)key,
            };
            switch (_shortcut.HandleKeyPress(physical, sym))
            {
                case ShortcutHandler.KeyAction.Ignore:
                    e.Handled = true;
                    return;
                case ShortcutHandler.KeyAction.Shortcut:
                    e.Handled = true;
                    FireShortcut(key);
                    return;
            }
        }

        int id = (int)key;
        _keys.Add(id);
        _session.SendKey(id, RemoteKeyMapper.ScanCode(key), sym);
        e.Handled = true;
    }
    private void Window_KeyUp(object sender, KeyEventArgs e)
    {
        Key key = e.Key == Key.System ? e.SystemKey : e.Key;

        if (_shortcut is not null)
        {
            int physical = RemoteKeyMapper.ScanCode(key) switch {
                var code when code != 0 => (int)code,
                _ => (int)key,
            };
            switch (_shortcut.HandleKeyRelease(physical))
            {
                case ShortcutHandler.KeyAction.Ignore:
                case ShortcutHandler.KeyAction.Shortcut:
                    // 这个键在按下时已经被吃掉，松开也不能漏给远端。
                    _keys.Remove((int)key);
                    e.Handled = true;
                    return;
                case ShortcutHandler.KeyAction.Unarm:
                    // 修饰键全松开：解除键盘抓取（退出全屏）。
                    if (_fullscreen) Fullscreen_Click(this, new RoutedEventArgs());
                    break;
            }
        }

        if (_keys.Remove((int)key))
        {
            _session.SendKeyRelease((int)key);
            e.Handled = true;
        }
    }

    /// <summary>
    /// 执行会话快捷键。对照原版 <c>Viewport::handleKeyPress</c> 的动作表：
    /// 修饰键 + G（抓取键盘）、+ M（会话菜单）、+ Enter（全屏）、+ Space（发组合键）。
    /// 本窗口没有独立菜单，M 改成「最小化」；Space 表示「只发修饰键、不带字母」，
    /// 这里把当前按着的修饰键原样发给远端。
    /// </summary>
    private void FireShortcut(Key key)
    {
        switch (key)
        {
            case Key.G:
            case Key.M:
            case Key.Enter:
                // 原版这三个动作都需要「先松开远端按住的键」再执行，
                // 否则远端会停留在按住状态。
                ReleaseAllKeys();
                if (key == Key.G) _grabber?.Start();
                else if (key == Key.Enter && _fullscreen) Fullscreen_Click(this, new RoutedEventArgs());
                else if (key == Key.M) WindowState = WindowState.Minimized;
                break;

            case Key.Space:
                // 修饰键 + 空格：只发修饰键，让用户接着按别的键组成组合。
                // 按下时已经在 _keys 里记录过修饰键，这里不需要补发。
                break;
        }
    }

    /// <summary>
    /// 窗口尺寸变化：节流后把新尺寸发给服务器（RemoteResize）。
    /// 全屏时不要改——全屏已经把本地画面铺满，改远程尺寸会来回打架。
    /// </summary>
    private void Window_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        if (!_remoteResize || _closing || _fullscreen) return;
        _resizeTimer.Stop();
        _resizeTimer.Start();
    }

    /// <summary>
    /// 把当前客户端区域尺寸发给服务器。服务器不支持 SetDesktopSize 时
    /// 会忽略这次请求——这是协议允许的，不当作错误。
    /// </summary>
    private void PushRemoteSize()
    {
        if (!_remoteResize || _closing || _fullscreen || _bitmap is null) return;
        // 用 DIP 尺寸：WPF 已经按 DPI 缩放过，发远程像素要有意义。
        int width = (int)Math.Round(DesktopViewport.ActualWidth);
        int height = (int)Math.Round(DesktopViewport.ActualHeight);
        if (width <= 0 || height <= 0) return;
        _session.RequestRemoteSize(width, height);
    }

    /// <summary>松开所有远端按住的键。快捷键触发和失焦清理都要用。</summary>
    private void ReleaseAllKeys()
    {
        foreach (int pressed in _keys) _session.SendKeyRelease(pressed);
        _keys.Clear();
        // 中键模拟可能还攥着延迟中的按键，一起清掉。
        if (_buttons != 0 || _middle is { EmulatedMask: not 0 })
            _session.SendPointer(_lastPoint.X, _lastPoint.Y, 0);
        _buttons = 0;
        _middleTimer.Stop();
        _middle?.Reset();
    }
    private void Window_Deactivated(object? sender, EventArgs e)
    {
        ReleaseAllKeys();
        // 失焦后状态机的按下记录已经和现实脱节，必须清空，
        // 否则回来后第一次松键会误判成快捷键。
        _shortcut?.Reset();
        DesktopImage.ReleaseMouseCapture();
        _clipboard.SetFocus(false);
    }

    private void Window_Activated(object? sender, EventArgs e) => _clipboard.SetFocus(true);

    /// <summary>
    /// 低级键盘钩子转来的按键。这些是系统组合键（Alt+Tab、Win 等），
    /// 走和普通按键同一套映射，保证远端收到的是同一个键。
    /// </summary>
    private void ForwardGrabbedKey(Key key, bool down)
    {
        if (_closing || _viewOnly) return;
        if (!down)
        {
            if (_keys.Remove((int)key)) _session.SendKeyRelease((int)key);
            return;
        }
        if (!_keys.Add((int)key)) return;    // 系统会重复投递自动重复按键，只发第一次。
        uint sym = RemoteKeyMapper.SpecialKey(key);
        if (sym == 0) sym = RemoteKeyMapper.CharacterKey(key);
        if (sym == 0)
        {
            // 映射不出 keysym（输入法组合键等）：撤销记账。
            // 否则松开时会对着一个从没按过的键发 KeyUp，记账失衡。
            _keys.Remove((int)key);
            return;
        }
        _session.SendKey((int)key, RemoteKeyMapper.ScanCode(key), sym);
    }
    private void Scale_Click(object sender, RoutedEventArgs e)
    {
        _originalSize = !_originalSize;
        ApplyScale();
    }

    private void ApplyScale()
    {
        // 两种显示模式继续使用现有的等比缩放鼠标坐标映射。
        // 原始大小按一个远程像素对应一个 WPF 布局单位显示，并支持滚动。
        DesktopImage.Width = _originalSize && _bitmap is not null ? _bitmap.PixelWidth : double.NaN;
        DesktopImage.Height = _originalSize && _bitmap is not null ? _bitmap.PixelHeight : double.NaN;
        DesktopViewport.HorizontalScrollBarVisibility = _originalSize
            ? System.Windows.Controls.ScrollBarVisibility.Auto
            : System.Windows.Controls.ScrollBarVisibility.Disabled;
        DesktopViewport.VerticalScrollBarVisibility = _originalSize
            ? System.Windows.Controls.ScrollBarVisibility.Auto
            : System.Windows.Controls.ScrollBarVisibility.Disabled;
        DesktopImage.Stretch = _originalSize ? Stretch.None : Stretch.Uniform;
        ScaleButtonText.Text = _originalSize ? "适应窗口" : "原始大小";
        ScaleStatusText.Text = _originalSize ? "原始大小 · 可滚动" : "适应窗口";
        System.Windows.Automation.AutomationProperties.SetName(ScaleButton,
            _originalSize ? "当前为原始大小，切换为适应窗口" : "当前为适应窗口，切换为原始大小");
    }

    private void Refresh_Click(object sender, RoutedEventArgs e) => _session.Refresh();
    private void Disconnect_Click(object sender, RoutedEventArgs e) => Close();
    private void Fullscreen_Click(object sender, RoutedEventArgs e)
    {
        string mode = _displayMode;

        if (!_fullscreen)
        {
            _windowedState = WindowState;
            // 先记下窗口位置，退出全屏时还原回去。
            _windowedBounds = new Rect(Left, Top, Width, Height);
        }

        _fullscreen = !_fullscreen;
        if (_fullscreen)
        {
            WindowState = WindowState.Normal;
            WindowStyle = WindowStyle.None;
            WindowState = WindowState.Maximized;
            // 三种模式对照原版 fullscreen_on()：全部显示器 / 选定显示器 / 当前显示器。
            switch (mode)
            {
                case "全部显示器全屏":
                    ExpandToAllMonitors();
                    break;
                case "选定显示器全屏":
                    if (!ExpandToSelectedMonitors())
                        _fullscreenNotice = "选中的显示器无法读取，已按当前显示器全屏。";
                    break;
                default:
                    FitToCurrentMonitor();
                    break;
            }
            // 钩子装不上（被安全软件拦截等）时，系统组合键会切走本机窗口。
            // 这是用户能感知的差异，必须明确告知，不能静默。
            if (_grabber is not null && !_grabber.Start())
                _fullscreenNotice = "无法接管系统组合键（Alt+Tab、Win 等仍会切走本机窗口）。";
        }
        else
        {
            WindowStyle = WindowStyle.SingleBorderWindow;
            WindowState = _windowedState;
            if (_windowedBounds is { } bounds && _windowedState == WindowState.Normal)
            {
                Left = bounds.Left;
                Top = bounds.Top;
                Width = bounds.Width;
                Height = bounds.Height;
            }
            _grabber?.Stop();
        }

        FullscreenButtonText.Text = _fullscreen ? "退出全屏" : "进入全屏";
        System.Windows.Automation.AutomationProperties.SetName(FullscreenButton, FullscreenButtonText.Text);
        if (_fullscreenNotice is { Length: > 0 })
        {
            StatusText.Text = _fullscreenNotice;
            _fullscreenNotice = "";
        }
    }

    /// <summary>
    /// 把窗口铺满「选中的若干台显示器」的并集。对照原版
    /// <c>DesktopWindow::fullscreen_on</c> 的 selectedMonitors 分支：
    /// 取所有选中屏幕的并集矩形；一台都没选中时回落到当前显示器。
    /// 返回 false 表示读不到显示器，调用方负责说明。
    /// </summary>
    private bool ExpandToSelectedMonitors()
    {
        // 显示器编号是 1 起的连续序号（和设置页的显示顺序一致）。
        var selected = _selectedMonitors;
        var monitors = EnumerateMonitors();
        if (monitors.Count == 0) return false;

        var chosen = new List<NativeRect>();
        foreach (int index in selected)
            if (index >= 1 && index <= monitors.Count)
                chosen.Add(monitors[index - 1].Monitor);

        if (chosen.Count == 0)
        {
            // 原版语义：没选中任何屏幕就用窗口当前所在的那台。
            var handle = new System.Windows.Interop.WindowInteropHelper(this).Handle;
            if (handle == IntPtr.Zero || !TryGetWorkArea(handle, out var area)) return false;
            Left = area.Left;
            Top = area.Top;
            Width = area.Right - area.Left;
            Height = area.Bottom - area.Top;
            return true;
        }

        int left = chosen.Min(r => r.Left), top = chosen.Min(r => r.Top);
        int right = chosen.Max(r => r.Right), bottom = chosen.Max(r => r.Bottom);
        Left = left;
        Top = top;
        Width = Math.Max(1, right - left);
        Height = Math.Max(1, bottom - top);
        return true;
    }

    /// <summary>
    /// 枚举本机所有显示器。用 Win32 的 <c>EnumDisplayMonitors</c>，
    /// 和设置页的 <c>MainWindow</c> 保持同一套编号和坐标语义。
    /// </summary>
    private static List<(string Device, NativeRect Monitor)> EnumerateMonitors()
    {
        var result = new List<(string, NativeRect)>();
        try
        {
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data) =>
            {
                var info = new MonitorInfo { Size = System.Runtime.InteropServices.Marshal.SizeOf<MonitorInfo>() };
                if (GetMonitorInfo(monitor, ref info))
                    result.Add((info.Device, info.Monitor));
                return true;
            }, IntPtr.Zero);
        }
        catch (Exception) { /* 枚举失败按「没有显示器」处理，调用方回落。 */ }
        return result;
    }

    private delegate bool MonitorEnumProc(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data);

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    [return: System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.Bool)]
    private static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorEnumProc callback, IntPtr data);

    /// <summary>
    /// 把无边框窗口贴满当前显示器的工作区（不含任务栏）。
    /// 用 Win32 而不是 WinForms：项目没有启用 WinForms，不该为一次查询多引一个框架。
    /// </summary>
    private void FitToCurrentMonitor()
    {
        var handle = new System.Windows.Interop.WindowInteropHelper(this).Handle;
        if (handle == IntPtr.Zero || !TryGetWorkArea(handle, out var area)) return;
        Left = area.Left;
        Top = area.Top;
        Width = area.Right - area.Left;
        Height = area.Bottom - area.Top;
    }

    private const int MONITOR_DEFAULTTONEAREST = 2;

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct NativeRect { public int Left, Top, Right, Bottom; }

    [System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
    private struct MonitorInfo
    {
        public int Size;
        public NativeRect Monitor;
        public NativeRect Work;
        public uint Flags;
        [System.Runtime.InteropServices.MarshalAs(
            System.Runtime.InteropServices.UnmanagedType.ByValTStr, SizeConst = 32)]
        public string Device;
    }

    [System.Runtime.InteropServices.DllImport("user32.dll")]
    private static extern IntPtr MonitorFromWindow(IntPtr hwnd, int flags);

    [System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
    [return: System.Runtime.InteropServices.MarshalAs(System.Runtime.InteropServices.UnmanagedType.Bool)]
    private static extern bool GetMonitorInfo(IntPtr monitor,
        ref MonitorInfo info);

    private static bool TryGetWorkArea(IntPtr handle, out NativeRect area)
    {
        area = default;
        IntPtr monitor = MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST);
        if (monitor == IntPtr.Zero) return false;
        var info = new MonitorInfo { Size = System.Runtime.InteropServices.Marshal.SizeOf<MonitorInfo>() };
        if (!GetMonitorInfo(monitor, ref info)) return false;
        area = info.Work;
        return true;
    }

    /// <summary>跨越所有显示器铺满整个虚拟桌面。</summary>
    private void ExpandToAllMonitors()
    {
        Left = SystemParameters.VirtualScreenLeft;
        Top = SystemParameters.VirtualScreenTop;
        Width = SystemParameters.VirtualScreenWidth;
        Height = SystemParameters.VirtualScreenHeight;
    }
    private async void Window_Closing(object? sender, CancelEventArgs e)
    {
        if (_closed) return;
        e.Cancel = true;
        if (_closing) return;
        _closing = true;
        _timer.Stop();
        _statsTimer.Stop();
        _resizeTimer.Stop();
        _pointer.Dispose();
        _middleTimer.Stop();
        _middle?.Reset();
        _grabber?.Dispose();
        Window_Deactivated(this, EventArgs.Empty);
        Activated -= Window_Activated;
        _clipboard.ClearOnDisconnect();
        _clipboard.Dispose();
        _session.CursorChanged -= OnRemoteCursor;
        _session.Bell -= OnRemoteBell;
        _session.NameChanged -= OnRemoteName;
        _session.Error -= OnError;
        _session.RequestStop();
        await Task.Run(_session.Dispose);
        // 清理可能立即完成；让第一次 Closing 先返回，再真正关闭窗口。
        await Dispatcher.InvokeAsync(() =>
        {
            _closed = true;
            Close();
        });
    }
}
