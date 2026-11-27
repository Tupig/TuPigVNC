using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Automation;
using System.Windows.Controls;
using System.Windows.Controls.Primitives;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Threading;
using TuPigVNC.Wpf.Services;
using ShapePath = System.Windows.Shapes.Path;

namespace TuPigVNC.Wpf;

public partial class MainWindow : Window
{
    private VncConfiguration _configuration = VncConfiguration.Create();
    private readonly HistoryStore _history = new();
    private readonly Dictionary<string, FrameworkElement> _settingControls = new();
    private readonly List<MonitorInfo> _monitors = new();
    private UiSettings _draft = new();
    private HistoryEntry? _selected;
    private HistoryEntry? _deleted;
    private string _page = "recent";
    private string _category = "压缩";
    private string _usedDir = "";
    private bool _refreshing;
    private bool _connecting;
    private bool _closeWhenIdle;
    private CancellationTokenSource? _connectionCancellation;
    private TaskCompletionSource<(string User, string Password)?>? _credentials;
    private TaskCompletionSource<bool>? _trust;
    private bool _needsUser;
    /* -via 建立的 SSH 隧道；窗口关闭时统一清理，不留孤儿进程。 */
    private SshTunnel? _tunnel;
    /* 是否已经用过记住的凭据。首次认证失败后必须让用户手动输入，
       否则会拿同一份错密码反复重试。 */
    private bool _credentialRetried;
    private bool _needsPassword;
    private SessionWindow? _sessionWindow;
    private string _connectedAddress = "";

    public MainWindow() : this(null) { }

    /// <summary>
    /// 带命令行参数启动。地址、配置文件、SSH 隧道都在这里落地；
    /// 参数为 null 表示正常启动（只用历史记录和已保存的配置）。
    /// </summary>
    public MainWindow(CommandLineOptions? options)
    {
        InitializeComponent();
        SetButton(ConnectButton, "连接", "connect");
        SetLabel(NavRecent, "最近连接", "history");
        SetLabel(NavAll, "全部记录", "all");
        SetLabel(NavSettings, "设置", "settings");
        SetLabel(NavAbout, "更新与关于", "info");
        SetButton(LoadButton, "载入配置…", "file");
        SetButton(DetailConnect, "连接此设备", "connect");
        SetButton(OptionsButton, "连接选项", "settings");
        SetButton(RenameButton, "重命名", "edit");
        SetButton(SaveButton, "另存为配置…", "save");
        SetButton(DeleteButton, "删除记录…", "delete");
        SetButton(RenameSave, "保存", "check");
        SetButton(RenameCancel, "取消", "cancel");
        SetButton(DeleteConfirm, "删除", "delete");
        SetButton(DeleteCancel, "保留", "cancel");
        SetButton(UndoButton, "撤销", "refresh");
        SetButton(AuthContinue, "继续", "check");
        SetButton(AuthCancel, "取消", "cancel");
        SetButton(ApplySettings, "应用", "check");
        SetButton(ResetSettings, "恢复默认", "refresh");
        SetButton(CheckUpdate, "检查更新", "refresh");
        // 版本号只认程序集，不在 XAML 里硬编码，避免和实际构建不一致。
        AboutVersionText.Text = $"v{CurrentVersion} · Windows x64 · 基于 TigerVNC";
        CurrentVersionText.Text = $"当前版本 v{CurrentVersion} · 尚未获取远端发布说明";
        SetButton(TrustAccept, "仅本次继续", "check");
        SetButton(TrustReject, "停止连接", "cancel");
        _history.Load();
        ReadMonitors();
        BuildSettingsTabs();
        NavRecent.IsChecked = true;
        RefreshHistory();
        Loaded += (_, _) => ServerInput.Focus();
        if (options is not null) ApplyCommandLine(options);
    }

    /// <summary>
    /// 把命令行参数落到界面：载入配置文件、填地址、准备 SSH 隧道。
    /// 任何一步失败都明确提示，不静默忽略。
    /// </summary>
    private void ApplyCommandLine(CommandLineOptions options)
    {
        try
        {
            if (options.ConfigFile is { } configFile)
            {
                ApplyLoadedConfiguration(configFile);
                _usedDir = Path.GetDirectoryName(configFile) ?? _usedDir;
            }

            if (options.Server is { } server)
                ServerInput.Text = server;

            // 监听模式：窗口显示后开始等服务器反向连入。
            if (options.Listen)
            {
                int listenPort = options.ListenPort;
                Loaded += (_, _) => _ = ListenAsync(listenPort);
            }

            // 隧道必须等窗口真正显示后再建：建隧道会阻塞，
            // 在构造函数里做会让窗口迟迟不出现。
            if (options.Via is { } gateway)
            {
                Loaded += (_, _) => _ = StartTunnelAsync(gateway, ServerInput.Text.Trim());
            }
        }
        catch (Exception ex)
        {
            ShowError("处理命令行参数失败：" + ex.Message);
        }
    }

    /// <summary>
    /// 监听模式：绑定端口等服务器反向连入（<c>-listen</c>）。
    /// 对照原版 <c>vncviewer.cxx</c> 的 listenMode 分支：默认端口 5500，
    /// 拿到连接后走和主动连接完全相同的协议初始化和窗口流程。
    /// 只监听回环——反向连接通常来自本机隧道，绑公网会暴露机器。
    /// </summary>
    private async Task ListenAsync(int port)
    {
        if (_connecting || _sessionWindow is not null) return;

        bool viewOnly = _configuration.GetBoolean("ViewOnly") ?? false;
        var quality = EncodingSettings.FromConfiguration(_configuration);
        bool acceptClipboard = _configuration.GetBoolean("AcceptClipboard") ?? true;
        bool sendClipboard = _configuration.GetBoolean("SendClipboard") ?? true;
        bool emulateMiddle = _configuration.GetBoolean("EmulateMiddleButton") ?? false;
        bool grabSystemKeys = _configuration.GetBoolean("FullscreenSystemKeys") ?? false;
        bool alwaysCursor = _configuration.GetBoolean("AlwaysCursor") ?? true;
        bool reconnectOnError = _configuration.GetBoolean("ReconnectOnError") ?? true;
        string displayMode = _configuration.Get("FullScreenMode") ?? "窗口";
        var shortcutModifiers = ShortcutHandler.ParseModifiers(_configuration.Get("ShortcutModifiers"));
        var selectedMonitorIndices = ParseMonitorIndices(_configuration.Get("FullScreenSelectedMonitors"));
        bool remoteResize = _configuration.GetBoolean("RemoteResize") ?? true;
        int pointerEventInterval = int.TryParse(_configuration.Get("PointerEventInterval"), out int interval)
            && interval >= 0 ? interval : 17;

        ClearError();
        _credentialRetried = false;
        _connecting = true;
        using var cancellation = new CancellationTokenSource();
        _connectionCancellation = cancellation;
        SetConnectingControls(true);
        SetStatus($"正在监听端口 {port}，等待服务器反向连入…", "AccentBrush");

        try
        {
            var connected = await Task.Run(() =>
            {
                var session = new VncSession();
                bool transferred = false;
                try
                {
                    using var registration = cancellation.Token.Register(session.RequestStop);
                    var initialized = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
                    void OnInitialized(int width, int height, string name) => initialized.TrySetResult(name);
                    void OnInitializationError(string message, bool fatal)
                    { if (fatal) initialized.TrySetException(new IOException(message)); }
                    session.ServerInit += OnInitialized;
                    session.Error += OnInitializationError;
                    session.CredentialProvider = (credHost, secure, needsUser, needsPassword) =>
                        RequestCredentials(credHost, secure, needsUser, needsPassword, cancellation.Token);
                    session.CertificateVerifier = (status, certificate, fingerprint) =>
                        RequestTrust("无法验证服务器证书",
                            $"反向连入的服务器证书存在需要人工判断的问题（状态位 0x{status:X8}）。" +
                            "请通过可信渠道核对下面的指纹；确认前不要继续。",
                            fingerprint, cancellation.Token);
                    session.HostKeyVerifier = (key, fingerprint) =>
                        RequestTrust("需要核对 SSH 主机身份",
                            "反向连入的服务器提供了 SSH 主机密钥。首次连接或身份变化时必须人工核对。",
                            fingerprint, cancellation.Token);
                    try
                    {
                        cancellation.Token.ThrowIfCancellationRequested();
                        // 监听模式：acceptTimeoutMs = 0 表示一直等，由取消令牌控制退出。
                        var result = session.Listen(port, 0, out string error);
                        cancellation.Token.ThrowIfCancellationRequested();
                        if (result != VncConnectResult.Ok)
                            throw new IOException($"监听失败（{result}）：{error}");
                        quality.ApplyTo(session);
                        if (_configuration.GetBoolean("Audio") ?? true) session.SetAudioEnabled(true);
                        if (quality.AutoSelect) session.SetAutoSelectQuality(true);
                        session.StartLoop();
                        string name = initialized.Task.WaitAsync(TimeSpan.FromMinutes(30), cancellation.Token)
                            .GetAwaiter().GetResult();
                        cancellation.Token.ThrowIfCancellationRequested();
                        transferred = true;
                        return (Session: session, Name: name);
                    }
                    finally
                    {
                        session.ServerInit -= OnInitialized;
                        session.Error -= OnInitializationError;
                        session.CredentialProvider = null;
                        session.CertificateVerifier = null;
                        session.HostKeyVerifier = null;
                    }
                }
                finally
                {
                    if (!transferred) { session.RequestStop(); session.Dispose(); }
                }
            });

            SessionWindow remote;
            try
            {
                remote = new SessionWindow(connected.Session, connected.Name, viewOnly,
                    acceptClipboard, sendClipboard, emulateMiddle, grabSystemKeys, alwaysCursor,
                    displayMode, shortcutModifiers, selectedMonitorIndices, remoteResize,
                    pointerEventInterval);
            }
            catch { connected.Session.RequestStop(); await Task.Run(connected.Session.Dispose); throw; }

            _sessionWindow = remote;
            _connectedAddress = $"listen::{port}";
            remote.Closed += (_, _) =>
            {
                _sessionWindow = null;
                _connectedAddress = "";
                if (!_closeWhenIdle)
                {
                    Show();
                    Activate();
                    if (remote.TerminationError is { Length: > 0 } reason)
                    {
                        SetStatus("反向连接中断", "ErrBrush");
                        ShowError("反向会话已中断：" + reason);
                        if (reconnectOnError) _ = ListenAsync(port);
                    }
                }
                if (_closeWhenIdle) Close();
            };
            Hide();
            remote.Show();
            SetStatus($"已接受反向连接：{connected.Name}", "OkBrush");
        }
        catch (OperationCanceledException)
        {
            SetStatus("已取消监听", "Text3Brush");
        }
        catch (Exception ex)
        {
            ShowError("监听失败：" + ex.Message);
            SetStatus("未连接", "Text3Brush");
        }
        finally
        {
            _connecting = false;
            _connectionCancellation = null;
            SetConnectingControls(false);
        }
    }

    /// <summary>
    /// 建立 SSH 隧道并把地址换成隧道入口。
    /// 隧道进程在窗口关闭时统一清理，不留孤儿 ssh.exe。
    /// </summary>
    private async Task StartTunnelAsync(string gateway, string address)
    {
        if (!HistoryStore.TryParseConnectionAddress(address, out string host, out int port))
        {
            ShowError("-via 需要配合有效的服务器地址使用。");
            return;
        }

        SetStatus("正在通过 SSH 网关建立隧道…", "AccentBrush");
        var tunnel = new SshTunnel();
        string? error = await Task.Run(() => tunnel.Start(gateway, host, port));
        if (error is not null)
        {
            tunnel.Dispose();
            ShowError(error);
            return;
        }

        _tunnel = tunnel;
        ServerInput.Text = tunnel.LocalAddress;
        SetStatus($"SSH 隧道已就绪：{tunnel.LocalAddress} → {host}:{port}", "OkBrush");
    }

    private void Window_SourceInitialized(object? sender, EventArgs e)
    {
        // 边框和圆角由 XAML 自己画，系统再切一次会在四角出现缺口。
        App.ApplyDarkTitleBar(this, systemRoundCorners: false);
        StateChanged += (_, _) => UpdateWindowFrame();
        UpdateWindowFrame();
    }

    // 自绘标题栏在最大化时贴住工作区；WPF 无边框窗口需要自己让出边框和圆角。
    private void UpdateWindowFrame()
    {
        bool maximized = WindowState == WindowState.Maximized;
        WindowFrame.CornerRadius = maximized ? new CornerRadius(0) : new CornerRadius(9);
        WindowFrame.BorderThickness = maximized ? new Thickness(0) : new Thickness(1);
        MaximizeGlyph.Data = maximized
            ? Geometry.Parse("M2.5,0.5 H9.5 V7.5 H2.5 Z M0.5,2.5 V9.5 H7.5")
            : Geometry.Parse("M0.5,0.5 H9.5 V9.5 H0.5 Z");
        AutomationProperties.SetName(MaximizeButton, maximized ? "还原" : "最大化");
        MaximizeButton.ToolTip = maximized ? "还原" : "最大化";
    }

    private void Minimize_Click(object sender, RoutedEventArgs e) => WindowState = WindowState.Minimized;
    private void Maximize_Click(object sender, RoutedEventArgs e) =>
        WindowState = WindowState == WindowState.Maximized ? WindowState.Normal : WindowState.Maximized;
    private void Close_Click(object sender, RoutedEventArgs e) => Close();

    // 同步原生 callback 只等待后台线程上的任务；UI 不使用 Wait、ShowDialog 或嵌套消息循环。
    private async Task ConnectAsync()
    {
        if (_connecting) { CancelConnection(); return; }
        if (_sessionWindow is not null) { _sessionWindow.Activate(); return; }
        string server = ServerInput.Text.Trim();
        if (!HistoryStore.TryParseConnectionAddress(server, out string host, out int port))
        {
            ShowError("请输入有效地址，例如 192.168.1.20、host:1 或 [::1]::5900；端口范围为 1–65535。");
            ServerInput.Focus();
            return;
        }

        // 在开始异步连接前保存一份，设置改动不会影响正在连接或已打开的会话。
        bool viewOnly = _configuration.GetBoolean("ViewOnly") ?? false;
        // 编码 / 压缩 / 质量必须在服务器第一次 SetEncodings 之前设好，
        // 连上后再改只会让服务器重发一次同样的配置，所以这里一次性取快照。
        var quality = EncodingSettings.FromConfiguration(_configuration);
        bool acceptClipboard = _configuration.GetBoolean("AcceptClipboard") ?? true;
        bool sendClipboard = _configuration.GetBoolean("SendClipboard") ?? true;
        bool emulateMiddle = _configuration.GetBoolean("EmulateMiddleButton") ?? false;
        bool grabSystemKeys = _configuration.GetBoolean("FullscreenSystemKeys") ?? false;
        bool sharedSession = _configuration.GetBoolean("Shared") ?? false;
        bool alwaysCursor = _configuration.GetBoolean("AlwaysCursor") ?? true;
        bool reconnectOnError = _configuration.GetBoolean("ReconnectOnError") ?? true;
        string displayMode = _configuration.Get("FullScreenMode") ?? "窗口";
        // 安全类型同样必须在 connect 之前设好：SecurityTypes 消息在握手时读取。
        string securityTypes = _configuration.Get("SecurityTypes") ?? "";
        string x509Ca = _configuration.Get("X509CA") ?? "";
        string x509Crl = _configuration.Get("X509CRL") ?? "";
        // 快捷键修饰键：配置里是逗号分隔的名字，翻成状态机的位掩码。
        var shortcutModifiers = ShortcutHandler.ParseModifiers(
            _configuration.Get("ShortcutModifiers"));
        // 选定显示器全屏用的屏幕编号（1 起），配置里是逗号分隔的整数。
        var selectedMonitorIndices = ParseMonitorIndices(_configuration.Get("FullScreenSelectedMonitors"));
        // 原版 playAudio 默认 true（HAVE_AUDIO 下）。
        bool playAudio = _configuration.GetBoolean("Audio") ?? true;
        // 原版 RemoteResize 默认 true：窗口变化时同步远程分辨率。
        bool remoteResize = _configuration.GetBoolean("RemoteResize") ?? true;
        // 鼠标事件合并间隔（毫秒）；0 表示不限速，每个事件都发。
        int pointerEventInterval = int.TryParse(_configuration.Get("PointerEventInterval"), out int interval)
            && interval >= 0 ? interval : 17;
        ClearError();
        _credentialRetried = false;   // 每次新连接都允许先试一次记住的凭据。
        _connecting = true;
        using var cancellation = new CancellationTokenSource();
        _connectionCancellation = cancellation;
        SetConnectingControls(true);
        SetStatus("正在连接…", "AccentBrush");
        try
        {
            var connected = await Task.Run(() =>
            {
                var session = new VncSession();
                bool transferred = false;
                try
                {
                    // 先销毁取消注册，再销毁原生句柄，避免取消与 Dispose 并发使用句柄。
                    using var registration = cancellation.Token.Register(session.RequestStop);
                    var initialized = new TaskCompletionSource<string>(TaskCreationOptions.RunContinuationsAsynchronously);
                    session.ServerInit += OnInitialized;
                    session.Error += OnInitializationError;
                    session.CredentialProvider = (credHost, secure, needsUser, needsPassword) =>
                        RequestCredentials(credHost, secure, needsUser, needsPassword, cancellation.Token);
                    session.CertificateVerifier = (status, certificate, fingerprint) =>
                        RequestTrust(
                            "无法验证服务器证书",
                            $"服务器 {host} 的证书存在需要人工判断的问题（状态位 0x{status:X8}）。" +
                            "证书签发者不受本机信任，或与地址不匹配。请联系管理员核对下面的指纹；" +
                            "确认前不要继续，也不要通过其他渠道输入密码。",
                            fingerprint, cancellation.Token);
                    session.HostKeyVerifier = (key, fingerprint) =>
                        RequestTrust(
                            "需要核对 SSH 主机身份",
                            $"服务器 {host} 提供了 SSH 主机密钥。首次连接或身份变化时必须人工核对，" +
                            "避免连到冒充的服务器。请通过可信渠道向管理员索取指纹。",
                            fingerprint, cancellation.Token);
                    try
                    {
                        cancellation.Token.ThrowIfCancellationRequested();
                        // 共享标志随 ClientInit 一起发出，必须在 connect 之前设。
                        session.SetShared(sharedSession);
                        // 安全类型也必须在 connect 之前设；空串代表用户一个都没选，
                        // 核心会拒绝（-1），不能静默回落到默认值。
                        if (securityTypes.Length > 0)
                        {
                            if (!session.SetSecurityTypes(securityTypes))
                                throw new IOException(
                                    $"安全类型组合无效：{securityTypes}。本核心不支持其中某些类型，或一个都没选。");
                        }
                        if (x509Ca.Length > 0) session.SetX509CA(x509Ca);
                        if (x509Crl.Length > 0) session.SetX509CRL(x509Crl);
                        // 音频能力位同样随 SetEncodings 发出，必须在 connect 之前设。
                        if (playAudio) session.SetAudioEnabled(true);
                        // 自动选择质量：让核心按实测带宽在 JPEG 质量 8/6 之间切换。
                        if (quality.AutoSelect) session.SetAutoSelectQuality(true);
                        var result = session.Connect(host, port, out string error);
                        cancellation.Token.ThrowIfCancellationRequested();
                        if (result != VncConnectResult.Ok)
                            throw new IOException($"连接失败（{result}）：{error}");
                        // setPreferredEncoding / setCompressLevel / setQualityLevel
                        // 只是置 encodingChange 标志，真正的 SetEncodings 在事件循环
                        // 第一次 updateEncodings() 时发出。必须在 StartLoop 之前设，
                        // 否则第一次请求已经带上了默认编码。
                        quality.ApplyTo(session);
                        session.StartLoop();
                        string name = initialized.Task.WaitAsync(TimeSpan.FromMinutes(5), cancellation.Token).GetAwaiter().GetResult();
                        cancellation.Token.ThrowIfCancellationRequested();
                        transferred = true;
                        return (Session: session, Name: name);
                    }
                    finally
                    {
                        session.ServerInit -= OnInitialized;
                        session.Error -= OnInitializationError;
                        session.CredentialProvider = null;
                        session.CertificateVerifier = null;
                        session.HostKeyVerifier = null;
                    }
                    void OnInitialized(int width, int height, string name) => initialized.TrySetResult(name);
                    void OnInitializationError(string message, bool fatal)
                    { if (fatal) initialized.TrySetException(new IOException(message)); }
                }
                finally
                {
                    if (!transferred) { session.RequestStop(); session.Dispose(); }
                }
            });

            if (cancellation.IsCancellationRequested)
            {
                connected.Session.RequestStop();
                await Task.Run(connected.Session.Dispose);
                cancellation.Token.ThrowIfCancellationRequested();
            }

            SessionWindow remote;
            try
            {
                remote = new SessionWindow(connected.Session, connected.Name, viewOnly,
                    acceptClipboard, sendClipboard, emulateMiddle, grabSystemKeys, alwaysCursor,
                    displayMode, shortcutModifiers, selectedMonitorIndices, remoteResize,
                    pointerEventInterval);
            }
            catch { connected.Session.RequestStop(); await Task.Run(connected.Session.Dispose); throw; }
            _sessionWindow = remote;
            _connectedAddress = server;
            remote.Closed += (_, _) =>
            {
                _sessionWindow = null;
                _connectedAddress = "";
                if (!_closeWhenIdle)
                {
                    Show();
                    Activate();
                    if (remote.TerminationError is { Length: > 0 } reason)
                    {
                        SetStatus("连接中断", "ErrBrush");
                        ShowError("远程会话已中断：" + reason);
                        // 断线（不是用户主动断开）才按配置重连；
                        // 主动断开或关闭工作台都不该自动连回去。
                        if (reconnectOnError) _ = TryReconnectAsync(server);
                    }
                    else
                    {
                        SetStatus("未连接 · 会话已结束", "Text3Brush");
                        ShowInfo("远程会话已结束，可以再次连接。");
                    }
                    RefreshDetail();
                }
            };
            // 只有真实帧到达并绘制后才隐藏工作台，不用认证成功冒充桌面已显示。
            var firstFrame = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(60) };
            firstFrame.Tick += (_, _) =>
            {
                if (remote.FindName("DesktopImage") is Image { Source: not null } && remote.IsVisible)
                {
                    firstFrame.Stop();
                    SetStatus("已连接", "OkBrush");
                    Hide();
                }
            };
            remote.Closed += (_, _) => firstFrame.Stop();
            remote.Show();
            firstFrame.Start();
            _history.AddOrTouch(server);
            bool saved = SaveHistorySafe();
            _selected = _history.Entries.FirstOrDefault(entry => HistoryStore.SameServer(entry.Host, server));
            RefreshHistory();
            SetStatus("已连接 · 等待远程画面", "OkBrush");
            ShowInfo(saved ? "远程会话已建立；收到画面后自动收起工作台。" : "远程会话已建立，但连接记录保存失败。");
        }
        catch (OperationCanceledException)
        {
            ShowInfo("连接已取消，可以修改地址后重新连接。");
            SetStatus("未连接 · 已取消", "Text3Brush");
        }
        catch (DllNotFoundException)
        {
            ShowError("核心 DLL 未找到：TuPigVNC.Core.dll 需要与程序放在同一目录。");
            SetStatus("连接失败", "ErrBrush");
        }
        catch (EntryPointNotFoundException)
        {
            ShowError("核心 DLL 缺少所需接口，请核对前端与核心版本。");
            SetStatus("连接失败", "ErrBrush");
        }
        catch (Exception ex)
        {
            string message = ex.Message;
            if (message.Contains("cert", StringComparison.OrdinalIgnoreCase) ||
                message.Contains("certificate", StringComparison.OrdinalIgnoreCase) ||
                message.Contains("hostkey", StringComparison.OrdinalIgnoreCase) ||
                message.Contains("fingerprint", StringComparison.OrdinalIgnoreCase) || message.Contains("证书"))
                message += "\n证书 / 主机指纹确认已按拒绝处理。请通过可信渠道核对服务器身份后重试；不会自动接受未知证书。";
            ShowError(message);
            SetStatus("连接失败", "ErrBrush");
        }
        finally
        {
            _credentials?.TrySetResult(null);
            _credentials = null;
            _trust?.TrySetResult(false);
            _trust = null;
            TrustCheck.IsChecked = false;
            TrustPanel.Visibility = Visibility.Collapsed;
            AuthPassword.Clear();
            AuthUser.Clear();
            AuthPanel.Visibility = Visibility.Collapsed;
            _connectionCancellation = null;
            _connecting = false;
            SetConnectingControls(false);
            RefreshDetail();
            if (_closeWhenIdle) Close();
        }
    }

    private (string User, string Password)? RequestCredentials(string host, bool secure, bool needsUser, bool needsPassword, CancellationToken token)
    {
        // 先看有没有记住的凭据。有的话直接返回，不打扰用户。
        // 服务器拒绝时核心会再调一次，那时 _credentialRetried 为 true，改走表单。
        if (!_credentialRetried && _connectedAddress is { } address
            && CredentialStore.Load(address) is { } saved)
        {
            _credentialRetried = true;
            return saved;
        }

        var response = new TaskCompletionSource<(string User, string Password)?>(TaskCreationOptions.RunContinuationsAsynchronously);
        Dispatcher.InvokeAsync(() =>
        {
            if (token.IsCancellationRequested || _closeWhenIdle) { response.TrySetResult(null); return; }
            _credentials?.TrySetResult(null);
            _credentials = response;
            _needsUser = needsUser;
            _needsPassword = needsPassword;
            AuthCaption.Text = $"{host} 需要认证 · {(secure ? "传输已加密" : "传输未加密，请确认网络可信")}。";
            AuthUserField.Visibility = needsUser ? Visibility.Visible : Visibility.Collapsed;
            AuthPasswordField.Visibility = needsPassword ? Visibility.Visible : Visibility.Collapsed;
            // 记住密码只在需要密码时才有意义。
            AuthKeepPassword.Visibility = needsPassword ? Visibility.Visible : Visibility.Collapsed;
            AuthKeepPassword.IsChecked = false;
            // 用户名可以先填上次用过的（密码仍留空，用户自己决定）。
            string? lastUser = _connectedAddress is { } addr ? CredentialStore.Load(addr)?.Username : null;
            AuthUser.Text = needsUser && lastUser is not null ? lastUser : "";
            AuthPassword.Clear();
            AuthError.Text = "";
            AuthPanel.Visibility = Visibility.Visible;
            SetStatus("等待认证", "AccentBrush");
            if (needsUser) AuthUser.Focus(); else if (needsPassword) AuthPassword.Focus(); else AuthContinue.Focus();
        }).Task.WaitAsync(token).GetAwaiter().GetResult();
        return response.Task.WaitAsync(token).GetAwaiter().GetResult();
    }

    /// <summary>
    /// 证书或 SSH 主机密钥需要确认。默认拒绝：没有用户勾选并点“仅本次继续”，
    /// 连接就停在原地，不会静默接受未知身份。
    /// </summary>
    private bool RequestTrust(string title, string caption, string fingerprint, CancellationToken token)
    {
        var response = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
        try
        {
            Dispatcher.InvokeAsync(() =>
            {
                if (token.IsCancellationRequested || _closeWhenIdle) { response.TrySetResult(false); return; }
                _trust?.TrySetResult(false);
                _trust = response;
                TrustTitle.Text = title;
                TrustCaption.Text = caption;
                TrustFingerprintText.Text = "SHA-256：" + fingerprint;
                TrustCheck.IsChecked = false;   // 每次重新确认，不沿用上一次的勾选。
                TrustAccept.IsEnabled = false;
                TrustPanel.Visibility = Visibility.Visible;
                SetStatus("等待核对服务器身份", "WarnBrush");
                TrustCheck.Focus();
            }).Task.WaitAsync(token).GetAwaiter().GetResult();
            return response.Task.WaitAsync(token).GetAwaiter().GetResult();
        }
        catch (OperationCanceledException)
        {
            // 取消不是“接受”：静默拒绝，绝不把取消异常抛回原生网络线程。
            return false;
        }
        finally
        {
            // 无论走哪条路径都摘掉等待，避免界面留着一个永远等不到结果的确认。
            if (ReferenceEquals(_trust, response)) _trust = null;
        }
    }

    private void TrustCheck_Changed(object sender, RoutedEventArgs e)
    {
        // 只有用户自己勾选后才允许继续；代码里的清除不会解锁。
        TrustAccept.IsEnabled = TrustCheck.IsChecked == true;
    }

    private void TrustAccept_Click(object sender, RoutedEventArgs e)
    {
        if (_trust is null || TrustCheck.IsChecked != true) return;
        var response = _trust;
        _trust = null;
        TrustPanel.Visibility = Visibility.Collapsed;
        SetStatus("正在使用本次确认的身份", "AccentBrush");
        response.TrySetResult(true);
    }

    private void TrustReject_Click(object sender, RoutedEventArgs e) => CancelConnection();

    /// <summary>
    /// 断线后按退避策略自动重连。每次都用新的策略实例，
    /// 所以一次断线最多重试 <see cref="ReconnectPolicy.MaxAttempts"/> 次；
    /// 重连成功后再断线会重新开始计数。
    /// </summary>
    private async Task TryReconnectAsync(string address)
    {
        var policy = new ReconnectPolicy();
        while (policy.CanRetry && !_closeWhenIdle)
        {
            var delay = policy.NextDelay();
            if (delay is not { } wait) break;

            SetStatus($"连接中断 · {wait.TotalSeconds:F0} 秒后重连（第 {policy.Attempts}/{ReconnectPolicy.MaxAttempts} 次）", "WarnBrush");
            ShowInfo($"连接已中断，{wait.TotalSeconds:F0} 秒后自动重连（第 {policy.Attempts}/{ReconnectPolicy.MaxAttempts} 次）。");
            try { await Task.Delay(wait); }
            catch (OperationCanceledException) { break; }

            if (_closeWhenIdle || _connecting || _sessionWindow is not null) return;

            // 只有地址没被用户改掉才重连，避免连到别的服务器。
            if (!string.Equals(ServerInput.Text.Trim(), address, StringComparison.Ordinal)) return;
            ClearError();
            await ConnectAsync();
            // ConnectAsync 成功会建出会话窗口；失败则状态回到未连接，继续下一轮。
            if (_sessionWindow is not null)
            {
                SetStatus("已重连", "OkBrush");
                return;
            }
        }

        if (!_closeWhenIdle)
        {
            SetStatus("重连失败", "ErrBrush");
            ShowError($"自动重连 {ReconnectPolicy.MaxAttempts} 次仍未成功，请检查网络或服务器状态后手动重试。");
        }
    }

    private void AuthContinue_Click(object sender, RoutedEventArgs e)
    {
        if (_credentials is null) return;
        if (_needsUser && string.IsNullOrWhiteSpace(AuthUser.Text))
        {
            AuthError.Text = "请填写服务器要求的用户名。";
            AuthUser.Focus();
            return;
        }
        var response = _credentials;
        _credentials = null;
        string user = _needsUser ? AuthUser.Text : "";
        string password = _needsPassword ? AuthPassword.Password : "";
        var value = (user, password);

        // 勾了「记住密码」就存起来；没勾则清掉旧的记录，避免留着过期密码。
        if (_needsPassword && _connectedAddress is { } address)
        {
            if (AuthKeepPassword.IsChecked == true)
            {
                if (CredentialStore.Save(address, user, password) is { } saveError)
                    SetStatus(saveError, "ErrBrush");
            }
            else CredentialStore.Forget(address);
        }
        // 核心在服务器拒绝认证时会再调一次 RequestCredentials；
        // 只有第一次允许用记住的凭据，第二次必须让用户手动改。
        _credentialRetried = true;

        AuthPassword.Clear();
        AuthPanel.Visibility = Visibility.Collapsed;
        response.TrySetResult(value);
        SetStatus("正在认证…", "AccentBrush");
    }
    private void AuthPassword_KeyDown(object sender, KeyEventArgs e)
    { if (e.Key == Key.Enter) { e.Handled = true; AuthContinue_Click(sender, e); } }
    private void CancelConnect_Click(object sender, RoutedEventArgs e) => CancelConnection();
    private void CancelConnection()
    {
        _credentials?.TrySetResult(null);
        _credentials = null;
        _trust?.TrySetResult(false);
        _trust = null;
        TrustCheck.IsChecked = false;
        TrustPanel.Visibility = Visibility.Collapsed;
        AuthPassword.Clear();
        AuthPanel.Visibility = Visibility.Collapsed;
        _connectionCancellation?.Cancel();
        SetStatus("正在取消…", "Text3Brush");
        ConnectButton.IsEnabled = false;
    }
    private void SetConnectingControls(bool busy)
    {
        ServerInput.IsEnabled = !busy;
        LoadButton.IsEnabled = !busy;
        DetailConnect.IsEnabled = !busy && _selected is not null;
        ConnectButton.IsEnabled = true;
        SetButton(ConnectButton, busy ? "取消" : "连接", busy ? "cancel" : "connect");
    }
    private void Window_Closing(object? sender, CancelEventArgs e)
    {
        if (_connecting)
        {
            e.Cancel = true;
            _closeWhenIdle = true;
            CancelConnection();
        }
        else if (_sessionWindow is not null)
        {
            // 关闭工作台时先按现有 SessionWindow 的异步清理流程断开。
            e.Cancel = true;
            _closeWhenIdle = true;
            _sessionWindow.Closed += (_, _) => Close();
            _sessionWindow.Close();
        }
        else
        {
            // 真正关窗：清理 SSH 隧道，不留孤儿 ssh.exe。
            _tunnel?.Dispose();
            _tunnel = null;
        }
    }
    private void Connect_Click(object sender, RoutedEventArgs e) => _ = ConnectAsync();
    private void ServerInput_KeyDown(object sender, KeyEventArgs e)
    { if (e.Key == Key.Enter) { e.Handled = true; _ = ConnectAsync(); } }
    private void ServerInput_TextChanged(object sender, TextChangedEventArgs e)
    {
        if (ServerPlaceholder is null) return;
        ServerPlaceholder.Visibility = ServerInput.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
    }
    private void SetStatus(string text, string brush)
    { StatusText.Text = text; StatusLight.Fill = (Brush)FindResource(brush); }

    private void Navigation_Checked(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: string page } || RecordsPage is null) return;
        _page = page;
        bool records = page is "recent" or "all";
        RecordsPage.Visibility = Inspector.Visibility = records ? Visibility.Visible : Visibility.Collapsed;
        SettingsPage.Visibility = page == "settings" ? Visibility.Visible : Visibility.Collapsed;
        AboutPage.Visibility = page == "about" ? Visibility.Visible : Visibility.Collapsed;
        if (records) RefreshHistory();
        if (page == "settings") RenderSettings();
    }
    private void Search_TextChanged(object sender, TextChangedEventArgs e)
    {
        // 提示文字在输入框内，不额外占一行（对照定稿 .search 占位符）。
        if (SearchPlaceholder is not null)
            SearchPlaceholder.Visibility = SearchInput.Text.Length == 0 ? Visibility.Visible : Visibility.Collapsed;
        if (HistoryList is not null) RefreshHistory();
    }
    /// <summary>渲染当前页的更新说明。页码越界时钳到合法范围。</summary>
    private void RenderReleasePage()
    {
        int pageCount = Math.Max(1, (_releaseLines.Length + ReleaseLinesPerPage - 1) / ReleaseLinesPerPage);
        _releasePage = Math.Clamp(_releasePage, 0, pageCount - 1);

        int start = _releasePage * ReleaseLinesPerPage;
        int count = Math.Min(ReleaseLinesPerPage, _releaseLines.Length - start);
        ReleaseNotes.Text = count > 0
            ? string.Join("\n", _releaseLines.Skip(start).Take(count))
            : "该版本没有提供更新说明。";

        // 只有一页时不该显示「1 / 1」这种噪音。
        ReleasePageInfo.Text = pageCount > 1 ? $"{_releasePage + 1} / {pageCount}" : "—";
        ReleasePrev.IsEnabled = _releasePage > 0;
        ReleaseNext.IsEnabled = _releasePage < pageCount - 1;
    }

    private void ReleasePrev_Click(object sender, RoutedEventArgs e)
    {
        if (_releasePage <= 0) return;
        _releasePage--;
        RenderReleasePage();
    }

    private void ReleaseNext_Click(object sender, RoutedEventArgs e)
    {
        int pageCount = Math.Max(1, (_releaseLines.Length + ReleaseLinesPerPage - 1) / ReleaseLinesPerPage);
        if (_releasePage >= pageCount - 1) return;
        _releasePage++;
        RenderReleasePage();
    }

    private void RefreshHistory()
    {
        string query = SearchInput.Text.Trim();
        IEnumerable<HistoryEntry> entries = _history.Entries.OrderByDescending(entry => entry.LastUsed);
        if (_page == "recent") entries = entries.Take(12);
        var visible = entries.Where(entry => entry.Name.Contains(query, StringComparison.OrdinalIgnoreCase) || entry.Host.Contains(query, StringComparison.OrdinalIgnoreCase)).ToList();
        _refreshing = true;
        HistoryList.ItemsSource = visible;
        if (_selected is null || !visible.Contains(_selected)) _selected = visible.FirstOrDefault();
        HistoryList.SelectedItem = _selected;
        _refreshing = false;
        ListTitle.Text = _page == "all" ? "全部记录" : "最近连接";
        VisibleCount.Text = visible.Count.ToString();
        RecordTotal.Text = $"{_history.Entries.Count} 条连接记录";
        EmptyRecords.Visibility = visible.Count == 0 ? Visibility.Visible : Visibility.Collapsed;
        HistoryList.Visibility = visible.Count == 0 ? Visibility.Collapsed : Visibility.Visible;
        EmptyRecords.Text = query.Length > 0 ? "没有匹配的记录。\n换个名称或地址试试。" : "还没有连接记录。\n在顶部输入地址开始连接。";
        RefreshDetail();
    }
    private void History_SelectionChanged(object sender, SelectionChangedEventArgs e)
    {
        if (_refreshing) return;
        _selected = HistoryList.SelectedItem as HistoryEntry;
        if (!_connecting && _selected is not null) ServerInput.Text = _selected.Host;
        RefreshDetail();
    }
    private void RefreshDetail()
    {
        RenamePanel.Visibility = DeletePanel.Visibility = Visibility.Collapsed;
        DetailBody.Visibility = _selected is null ? Visibility.Collapsed : Visibility.Visible;
        DetailName.Text = _selected?.Name ?? "选择一条连接记录";
        DetailHost.Text = _selected?.Host ?? "在左侧选择记录，查看详情。";
        DetailTime.Text = _selected?.AgoText ?? "";
        DetailConnect.IsEnabled = !_connecting && _selected is not null;
        DetailStatus.Text = _selected is not null && HistoryStore.SameServer(_selected.Host, _connectedAddress) ? "已连接" :
            _connecting && _selected is not null && HistoryStore.SameServer(_selected.Host, ServerInput.Text.Trim()) ? "连接中" : "未连接";
    }
    private void History_DoubleClick(object sender, MouseButtonEventArgs e)
    {
        if (e.OriginalSource is DependencyObject source && ItemsControl.ContainerFromElement(HistoryList, source) is ListBoxItem)
            ConnectSelected();
    }
    private void History_KeyDown(object sender, KeyEventArgs e)
    { if (e.Key == Key.Enter) { e.Handled = true; ConnectSelected(); } }
    private void ConnectSelected()
    { if (!_connecting && _selected is not null) { ServerInput.Text = _selected.Host; _ = ConnectAsync(); } }
    private void DetailConnect_Click(object sender, RoutedEventArgs e) => ConnectSelected();
    private void Options_Click(object sender, RoutedEventArgs e) => NavSettings.IsChecked = true;
    private void Rename_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        DeletePanel.Visibility = Visibility.Collapsed;
        RenamePanel.Visibility = Visibility.Visible;
        RenameInput.Text = _selected.Name;
        RenameError.Text = "";
        RenameInput.Focus();
        RenameInput.SelectAll();
    }
    private void RenameSave_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        string name = RenameInput.Text.Trim();
        if (name.Length == 0) { RenameError.Text = "名称不能为空。"; RenameInput.Focus(); return; }
        string oldName = _selected.Name;
        _selected.Name = name;
        if (!SaveHistorySafe()) { _selected.Name = oldName; RenameError.Text = "保存失败，名称没有修改。"; return; }
        RefreshHistory();
        Notice("名称已修改，只影响这条连接记录。");
    }
    private void RecordCancel_Click(object sender, RoutedEventArgs e) => RefreshDetail();
    private void Delete_Click(object sender, RoutedEventArgs e)
    { RenamePanel.Visibility = Visibility.Collapsed; DeletePanel.Visibility = Visibility.Visible; DeleteCancel.Focus(); }
    private void DeleteConfirm_Click(object sender, RoutedEventArgs e)
    {
        if (_selected is null) return;
        var removed = _selected;
        _history.Remove(removed);
        if (!SaveHistorySafe())
        {
            _selected = RestoreEntry(removed);
            RefreshHistory();
            Notice("删除未保存，记录已保留。");
            return;
        }
        _deleted = removed;
        _selected = null;
        RefreshHistory();
        Notice("记录已删除，可以撤销这次删除。", true);
    }
    private HistoryEntry RestoreEntry(HistoryEntry entry)
    {
        _history.AddOrTouch(entry.Host);
        var restored = _history.Entries.First(item => HistoryStore.SameServer(item.Host, entry.Host));
        restored.Name = entry.Name;
        restored.LastUsed = entry.LastUsed;
        restored.UseCount = entry.UseCount;
        return restored;
    }
    private void UndoDelete_Click(object sender, RoutedEventArgs e)
    {
        if (_deleted is null) return;
        // 若删除后同一地址已重新连接，则只恢复原名称，不覆盖新连接的次数和时间。
        var existing = _history.Entries.FirstOrDefault(item => HistoryStore.SameServer(item.Host, _deleted.Host));
        if (existing is null && _history.Entries.Count >= HistoryStore.MaxEntries)
        {
            Notice("记录已满 250 条。请先腾出一条记录，再撤销；没有自动淘汰其他记录。", true);
            return;
        }
        string? previousName = existing?.Name;
        _selected = existing ?? RestoreEntry(_deleted);
        _selected.Name = _deleted.Name;
        if (!SaveHistorySafe())
        {
            if (existing is null) _history.Remove(_selected); else existing.Name = previousName!;
            RefreshHistory();
            Notice("恢复未保存，请重试撤销。", true);
            return;
        }
        _deleted = null;
        RefreshHistory();
        Notice("已恢复连接记录。");
    }
    private bool SaveHistorySafe()
    {
        try { _history.Save(); return true; }
        catch (Exception ex) { Debug.WriteLine($"[history] 保存失败：{ex.GetType().Name}"); return false; }
    }
    private void Notice(string text, bool undo = false)
    {
        GlobalNotice.Text = text;
        NoticePanel.Visibility = Visibility.Visible;
        UndoButton.Visibility = undo || _deleted is not null ? Visibility.Visible : Visibility.Collapsed;
    }

    // 保留完整配置文档和 Windows 原生文件框；界面草稿未接通前不写入配置。
    private void LoadConfig_Click(object sender, RoutedEventArgs e)
    {
        string? file = NativeFileDialog.OpenConfig(_usedDir);
        if (string.IsNullOrEmpty(file)) return;
        _usedDir = Path.GetDirectoryName(file) ?? _usedDir;
        try
        {
            ApplyLoadedConfiguration(file);
            ClearError();
            ServerInput.Focus();
            ServerInput.CaretIndex = ServerInput.Text.Length;
            Notice("已载入地址和已接通的设置，下一次连接生效；未接通的选项原样保留。不保存密码。");
        }
        catch (Exception ex) { ShowError("载入配置失败：" + ex.Message + " 原地址和设置未改动。"); }
    }
    private void ApplyLoadedConfiguration(string file)
    {
        var loaded = VncConfiguration.Load(file);
        string? server = loaded.ServerName;
        if (!HistoryStore.TryParseConnectionAddress(server, out _, out _))
            throw new InvalidDataException("ServerName 缺失或地址无效。");
        // 解析和地址校验全部成功后再提交，失败不改变已有状态。
        _configuration = loaded;
        SyncDraftFromConfiguration();
        if (_page == "settings") RenderSettings();
        ServerInput.Text = server;
    }

    /// <summary>
    /// 把已载入的配置同步回界面草稿。只覆盖「已真正接通」的字段，
    /// 尚未接通的项保持原值和说明，不能因为载入一份配置文件就假装它们生效了。
    /// </summary>
    private void SyncDraftFromConfiguration()
    {
        _draft.ViewOnly = _configuration.GetBoolean("ViewOnly") ?? false;
        _draft.AutoSelect = _configuration.GetBoolean("AutoSelect") ?? true;
        _draft.Jpeg = !(_configuration.GetBoolean("NoJPEG") ?? false);
        _draft.CustomCompression = _configuration.GetBoolean("CustomCompressLevel") ?? false;
        _draft.Encoding = _configuration.Get("PreferredEncoding") ?? _draft.Encoding;
        _draft.Compression = _configuration.Get("CompressLevel") ?? _draft.Compression;
        _draft.Quality = _configuration.Get("QualityLevel") ?? _draft.Quality;
        _draft.AcceptClipboard = _configuration.GetBoolean("AcceptClipboard") ?? true;
        _draft.SendClipboard = _configuration.GetBoolean("SendClipboard") ?? true;
        _draft.EmulateMiddle = _configuration.GetBoolean("EmulateMiddleButton") ?? false;
        _draft.SystemKeys = _configuration.GetBoolean("FullscreenSystemKeys") ?? false;
        _draft.AlwaysCursor = _configuration.GetBoolean("AlwaysCursor") ?? true;
        _draft.Cursor = _configuration.Get("CursorType") ?? _draft.Cursor;
        _draft.Shared = _configuration.GetBoolean("Shared") ?? false;
        _draft.Reconnect = _configuration.GetBoolean("ReconnectOnError") ?? true;
        _draft.Audio = _configuration.GetBoolean("Audio") ?? true;
        _draft.DisplayMode = _configuration.Get("FullScreenMode") ?? _draft.DisplayMode;
        _draft.RemoteResize = _configuration.GetBoolean("RemoteResize") ?? true;

        // 选定显示器：配置里存的是 1 起的序号，反推回 Device 名。
        var monitorIndices = ParseMonitorIndices(_configuration.Get("FullScreenSelectedMonitors"));
        if (monitorIndices.Length > 0)
        {
            _draft.SelectedMonitors.Clear();
            foreach (int index in monitorIndices)
                if (index <= _monitors.Count)
                    _draft.SelectedMonitors.Add(_monitors[index - 1].Device);
        }

        // 快捷键修饰键：从逗号分隔的名字反推回四个勾选状态。
        // 配置里没有这个键时保留界面默认值（Ctrl+Alt），不能清成「全部不勾」——
        // 那等于用户载入一份旧配置后，快捷键被悄悄关掉了。
        string? shortcut = _configuration.Get("ShortcutModifiers");
        if (shortcut is not null)
        {
            var modifiers = ShortcutHandler.ParseModifiers(shortcut);
            _draft.Ctrl = (modifiers & ShortcutHandler.Modifier.Control) != 0;
            _draft.Shift = (modifiers & ShortcutHandler.Modifier.Shift) != 0;
            _draft.Alt = (modifiers & ShortcutHandler.Modifier.Alt) != 0;
            _draft.Super = (modifiers & ShortcutHandler.Modifier.Super) != 0;
        }

        // 安全类型是「加密 × 认证」的组合。配置里存的是最终类型串，
        // 反向拆回勾选状态：只要该加密方式下任一认证子类型在列表里就算勾上。
        var selected = new HashSet<string>(
            (_configuration.Get("SecurityTypes") ?? "").Split(',',
                StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries),
            StringComparer.OrdinalIgnoreCase);
        if (selected.Count > 0)
        {
            _draft.EncNone = selected.Contains("None") || selected.Contains("VncAuth") || selected.Contains("Plain");
            _draft.EncTls = selected.Contains("TLSNone") || selected.Contains("TLSVnc") || selected.Contains("TLSPlain");
            _draft.EncX509 = selected.Contains("X509None") || selected.Contains("X509Vnc") || selected.Contains("X509Plain");
            _draft.EncRsa = selected.Contains("RA2") || selected.Contains("RA2_256");
            _draft.AuthNone = selected.Contains("None") || selected.Contains("TLSNone") || selected.Contains("X509None");
            _draft.AuthVnc = selected.Contains("VncAuth") || selected.Contains("TLSVnc") || selected.Contains("X509Vnc");
            _draft.AuthPlain = selected.Contains("Plain") || selected.Contains("TLSPlain") || selected.Contains("X509Plain");
            _draft.AuthRsa = _draft.EncRsa;
        }
        _draft.Ca = _configuration.Get("X509CA") ?? "";
        _draft.Crl = _configuration.Get("X509CRL") ?? "";
    }

    private void SaveAs_Click(object sender, RoutedEventArgs e)
    {
        if (!HistoryStore.TryParseConnectionAddress(ServerInput.Text.Trim(), out _, out _))
        { ShowError("请先在顶部输入有效的服务器地址。"); return; }
        string? file = NativeFileDialog.SaveConfig(_usedDir, "vnc.tigervnc");
        if (string.IsNullOrEmpty(file)) return;
        _usedDir = Path.GetDirectoryName(file) ?? _usedDir;
        try
        {
            SaveConfiguration(file);
            Notice($"已另存为 {Path.GetFileName(file)}；保留载入的配置，不包含未接通的界面草稿或密码。");
        }
        catch (Exception ex) { ShowError("保存配置失败：" + ex.Message); }
    }
    private void SaveConfiguration(string file)
    {
        string server = ServerInput.Text.Trim();
        if (!HistoryStore.TryParseConnectionAddress(server, out _, out _))
            throw new InvalidDataException("服务器地址无效。");
        // 保存失败时不修改当前文档；地址只取当前输入，不混入历史选择。
        var saved = VncConfiguration.Parse(_configuration.Serialize());
        saved.ServerName = server;
        saved.Save(file);
        _configuration = saved;
    }
    private ReleaseInfo? _release;
    /* 更新说明分页：按行切页，每页固定行数，避免长说明把页面撑爆。 */
    private const int ReleaseLinesPerPage = 12;
    private string[] _releaseLines = Array.Empty<string>();
    private int _releasePage;
    private CancellationTokenSource? _updateCancellation;

    private async void CheckUpdate_Click(object sender, RoutedEventArgs e)
    {
        if (_updateCancellation is not null)
        {
            // 再点一次就是取消：网络请求是阻塞的，靠取消令牌在回到界面时丢弃结果。
            _updateCancellation.Cancel();
            return;
        }

        CheckUpdate.IsEnabled = false;
        UpdateNotice.Text = "正在检查更新…";
        DownloadUpdate.IsEnabled = false;
        CancelUpdate.IsEnabled = true;
        var cancellation = new CancellationTokenSource();
        _updateCancellation = cancellation;
        try
        {
            var release = await Task.Run(UpdateService.FetchLatest, cancellation.Token);
            if (cancellation.IsCancellationRequested) return;

            _release = release;
            ReleaseMetadata.Text = $"v{release.Version} · {(string.IsNullOrEmpty(release.PublishedAt) ? "日期未知" : release.PublishedAt)} · Windows x64";
            _releaseLines = string.IsNullOrWhiteSpace(release.Notes)
                ? new[] { "该版本没有提供更新说明。" }
                : release.Notes.ReplaceLineEndings("\n").Split('\n');
            _releasePage = 0;
            RenderReleasePage();

            if (!release.HasUpdate(CurrentVersion))
            {
                UpdateNotice.Text = $"已经是最新版本（当前 v{CurrentVersion}）。";
                DownloadUpdate.IsEnabled = false;
            }
            else
            {
                string size = release.SizeMegabytes > 0 ? $" · 安装包 {release.SizeMegabytes:F1} MB" : "";
                string checksum = string.IsNullOrWhiteSpace(release.ChecksumUrl)
                    ? "；该版本未提供校验文件，不能自动安装"
                    : "；安装前会强制校验 SHA-256";
                UpdateNotice.Text = $"发现新版本 v{release.Version}{size}{checksum}。";
                // 没有校验文件时禁用下载按钮，避免走到原生侧再被拒。
                DownloadUpdate.IsEnabled = !string.IsNullOrWhiteSpace(release.ChecksumUrl);
            }
        }
        catch (OperationCanceledException)
        {
            UpdateNotice.Text = "已取消检查更新。";
        }
        catch (Exception ex)
        {
            UpdateNotice.Text = "无法检查更新：" + ex.Message + " 当前版本仍可正常使用。";
        }
        finally
        {
            if (ReferenceEquals(_updateCancellation, cancellation)) _updateCancellation = null;
            cancellation.Dispose();
            CheckUpdate.IsEnabled = true;
            CancelUpdate.IsEnabled = false;
        }
    }

    private async void DownloadUpdate_Click(object sender, RoutedEventArgs e)
    {
        if (_release is null) { UpdateNotice.Text = "请先检查更新。"; return; }
        var release = _release;
        DownloadUpdate.IsEnabled = false;
        CancelUpdate.IsEnabled = true;
        var cancellation = new CancellationTokenSource();
        _updateCancellation = cancellation;

        // 原生下载是整体读写、不暴露逐字节进度，所以这里只做「进行中」的
        // 不确定进度条（IsIndeterminate），不画假的百分比。
        UpdateProgress.Value = 0;
        UpdateProgress.IsIndeterminate = true;
        string size = release.SizeMegabytes > 0 ? $"（{release.SizeMegabytes:F1} MB）" : "";
        DownloadAmount.Text = $"正在下载{size}";
        DownloadSpeed.Text = "";
        DownloadRemaining.Text = "正在校验 SHA-256，通过后自动安装…";
        UpdateNotice.Text = $"正在下载 v{release.Version}，校验通过后会自动安装…";

        var elapsed = Stopwatch.StartNew();
        try
        {
            await Task.Run(() => UpdateService.DownloadAndInstall(release), cancellation.Token);
            UpdateProgress.IsIndeterminate = false;
            UpdateProgress.Value = 100;
            DownloadAmount.Text = $"下载完成{size}";
            DownloadRemaining.Text = $"耗时 {elapsed.Elapsed.TotalSeconds:F1} 秒 · 校验通过 · 正在启动新版本…";
            UpdateNotice.Text = "更新已安装，正在启动新版本…";
        }
        catch (OperationCanceledException)
        {
            UpdateProgress.IsIndeterminate = false;
            UpdateProgress.Value = 0;
            DownloadAmount.Text = "已取消";
            DownloadRemaining.Text = "本次下载已取消，没有安装任何内容。";
            UpdateNotice.Text = "已取消下载更新。";
        }
        catch (Exception ex)
        {
            UpdateProgress.IsIndeterminate = false;
            UpdateProgress.Value = 0;
            DownloadAmount.Text = "下载失败";
            DownloadRemaining.Text = "没有安装任何内容。";
            UpdateNotice.Text = "更新失败：" + ex.Message;
        }
        finally
        {
            if (ReferenceEquals(_updateCancellation, cancellation)) _updateCancellation = null;
            cancellation.Dispose();
            CancelUpdate.IsEnabled = false;
            DownloadUpdate.IsEnabled = _release is not null;
        }
    }

    private void CancelUpdate_Click(object sender, RoutedEventArgs e) => _updateCancellation?.Cancel();

    /// <summary>当前程序版本（与 csproj 的 Version 保持一致）。</summary>
    private static string CurrentVersion =>
        typeof(MainWindow).Assembly.GetName().Version?.ToString(3) ?? "0.0.0";
    private void OpenLink_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: string url }) return;
        try { Process.Start(new ProcessStartInfo(url) { UseShellExecute = true }); }
        catch (Exception ex) { ShowError("无法打开链接：" + ex.Message); }
    }
    private void ShowError(string message)
    { ErrorText.Text = message; ErrorText.Foreground = (Brush)FindResource("ErrBrush"); ErrorText.Visibility = Visibility.Visible; }
    private void ShowInfo(string message)
    { ErrorText.Text = message; ErrorText.Foreground = (Brush)FindResource("Text2Brush"); ErrorText.Visibility = Visibility.Visible; }
    private void ClearError() { ErrorText.Text = ""; ErrorText.Visibility = Visibility.Collapsed; }

    // 完整的正式 UI 草稿模型。没有核心 setter 调用，必须始终明确说明尚未生效。
    private sealed class UiSettings
    {
        public bool AutoSelect { get; set; } = true;
        public bool Jpeg { get; set; } = true;
        public string Encoding { get; set; } = "Tight";
        public string Color { get; set; } = "全彩";
        public bool CustomCompression { get; set; }
        public string Compression { get; set; } = "2";
        public string Quality { get; set; } = "8";
        public bool EncNone { get; set; } = true;
        public bool EncTls { get; set; } = true;
        public bool EncX509 { get; set; } = true;
        public bool EncRsa { get; set; } = true;
        public string Ca { get; set; } = "";
        public string Crl { get; set; } = "";
        public bool AuthNone { get; set; } = true;
        public bool AuthVnc { get; set; } = true;
        public bool AuthPlain { get; set; } = true;
        public bool AuthRsa { get; set; } = true;
        public bool ViewOnly { get; set; }
        public bool EmulateMiddle { get; set; }
        public bool AlwaysCursor { get; set; }
        public string Cursor { get; set; } = "圆点";
        public bool SystemKeys { get; set; } = true;
        public bool AcceptClipboard { get; set; } = true;
        public bool SendClipboard { get; set; } = true;
        public bool Ctrl { get; set; } = true;
        public bool Shift { get; set; }
        public bool Alt { get; set; } = true;
        public bool Super { get; set; }
        public string DisplayMode { get; set; } = "窗口";
        public bool RemoteResize { get; set; } = true;
        public HashSet<string> SelectedMonitors { get; } = new();
        public bool Shared { get; set; }
        public bool Reconnect { get; set; } = true;
        public bool Audio { get; set; } = true;
    }
    private void BuildSettingsTabs()
    {
        // 定稿 .tabs 是允许换行、按内容宽度排列的按钮条，不是六列等宽。
        foreach (string category in new[] { "压缩", "安全", "输入", "快捷键", "显示", "杂项" })
        {
            var tab = new RadioButton
            {
                Content = category, Tag = category, GroupName = "SettingCategory",
                Style = (Style)FindResource("OptionTab"),
            };
            tab.Checked += (_, _) => { _category = (string)tab.Tag; RenderSettings(); };
            SettingsTabs.Children.Add(tab);
        }
        ((RadioButton)SettingsTabs.Children[0]).IsChecked = true;
    }
    private void RenderSettings()
    {
        SettingFields.Children.Clear();
        SettingFields.RowDefinitions.Clear();
        SettingFields.ColumnDefinitions.Clear();
        SettingFields.ColumnDefinitions.Add(new ColumnDefinition());
        SettingFields.ColumnDefinitions.Add(new ColumnDefinition());
        _settingControls.Clear();
        int cell = 0;
        void Add(FrameworkElement element, bool span = false)
        {
            if (span && cell % 2 != 0) cell++;
            int row = cell / 2;
            while (SettingFields.RowDefinitions.Count <= row) SettingFields.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
            Grid.SetRow(element, row);
            Grid.SetColumn(element, cell % 2);
            Grid.SetColumnSpan(element, span ? 2 : 1);
            element.Margin = new Thickness(0, 0, span || cell % 2 == 1 ? 0 : 14, 10);
            SettingFields.Children.Add(element);
            cell += span ? 2 : 1;
        }
        void Heading(string text) => Add(new TextBlock { Text = text, FontWeight = FontWeights.SemiBold, Margin = new Thickness(0, 4, 0, 0) }, true);
        void Note(string text) => Add(new TextBlock { Text = text, TextWrapping = TextWrapping.Wrap, FontSize = 11, Foreground = (Brush)FindResource("Text2Brush") }, true);
        void Check(string label, string property)
        {
            var info = typeof(UiSettings).GetProperty(property)!;
            var check = new CheckBox
            {
                Content = new TextBlock { Text = label, TextWrapping = TextWrapping.Wrap, MaxWidth = 150 },
                Style = (Style)FindResource("DarkCheckBox"),
                IsChecked = (bool)info.GetValue(_draft)!,
                VerticalAlignment = VerticalAlignment.Center,
            };
            AutomationProperties.SetName(check, label);
            check.Click += (_, _) =>
            {
                info.SetValue(_draft, check.IsChecked == true);
                if (property == nameof(UiSettings.EncRsa) && check.IsChecked == true)
                { _draft.AuthVnc = _draft.AuthPlain = true; RenderSettings(); }
                UpdateDependencies();
                SettingsNotice.Text = "有尚未确认的界面修改；还没有应用到连接。";
            };
            _settingControls[property] = check;
            Add(check);
        }
        void Field(string label, string property, string[]? choices = null, bool number = false)
        {
            var info = typeof(UiSettings).GetProperty(property)!;
            var panel = new StackPanel();
            panel.Children.Add(new TextBlock { Text = label, TextWrapping = TextWrapping.Wrap, MinHeight = 20, Margin = new Thickness(0, 0, 0, 5) });
            FrameworkElement control;
            if (choices is not null)
            {
                var combo = new ComboBox { ItemsSource = choices, SelectedItem = (string)info.GetValue(_draft)!, Style = (Style)FindResource("DarkComboBox"), Height = 30 };
                combo.SelectionChanged += (_, _) =>
                { if (combo.SelectedItem is string value) { info.SetValue(_draft, value); UpdateDependencies(); SettingsNotice.Text = "有尚未确认的界面修改。"; } };
                control = combo;
            }
            else
            {
                var input = new TextBox { Text = (string)info.GetValue(_draft)!, Style = (Style)FindResource("DarkTextBox"), Height = 30, Padding = new Thickness(8, 0, 8, 0), HorizontalAlignment = number ? HorizontalAlignment.Left : HorizontalAlignment.Stretch };
                if (number) { input.Width = 64; input.MaxLength = 2; }
                input.TextChanged += (_, _) => { info.SetValue(_draft, input.Text); SettingsNotice.Text = "有尚未确认的界面修改。"; };
                control = input;
            }
            AutomationProperties.SetName(control, label);
            _settingControls[property] = control;
            if (property is nameof(UiSettings.Ca) or nameof(UiSettings.Crl))
            {
                var pathRow = new Grid();
                pathRow.ColumnDefinitions.Add(new ColumnDefinition());
                pathRow.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
                pathRow.Children.Add(control);
                var browse = new Button
                {
                    Content = "浏览…", Style = (Style)FindResource("GhostButton"),
                    Margin = new Thickness(4, 0, 0, 0), Height = 30,
                };
                AutomationProperties.SetName(browse, "选择" + label);
                browse.Click += (_, _) =>
                {
                    var picker = new Microsoft.Win32.OpenFileDialog
                    {
                        Title = "选择" + label,
                        Filter = "证书和吊销列表 (*.pem;*.crt;*.cer;*.crl)|*.pem;*.crt;*.cer;*.crl|所有文件 (*.*)|*.*",
                        CheckFileExists = true,
                        InitialDirectory = AppContext.BaseDirectory,
                    };
                    if (picker.ShowDialog(this) == true) ((TextBox)control).Text = picker.FileName;
                };
                Grid.SetColumn(browse, 1);
                pathRow.Children.Add(browse);
                _settingControls[property + "Browse"] = browse;
                panel.Children.Add(pathRow);
            }
            else panel.Children.Add(control);
            Add(panel);
        }
        switch (_category)
        {
            case "压缩":
                Check("自动选择像素格式与编码", nameof(UiSettings.AutoSelect));
                Check("允许 JPEG 压缩", nameof(UiSettings.Jpeg));
                Field("首选编码", nameof(UiSettings.Encoding), new[] { "Tight", "JPEG", "ZRLE", "Hextile", "H.264", "Raw" });
                Field("颜色等级", nameof(UiSettings.Color), new[] { "全彩", "中", "低", "极低" });
                Check("自定义压缩等级", nameof(UiSettings.CustomCompression));
                Field("JPEG 质量（0–9）", nameof(UiSettings.Quality), number: true);
                Field("压缩等级（0–9）", nameof(UiSettings.Compression), number: true);
                Note("0：速度快 / 质量低；9：压缩最好 / 质量高。编码、压缩等级和 JPEG 质量会在下一次连接时发给服务器；H.264 依赖核心编译支持，服务器不支持时自动跳过。 ");
                break;
            case "安全":
                Heading("加密");
                Check("无加密", nameof(UiSettings.EncNone)); Check("TLS（匿名加密）", nameof(UiSettings.EncTls));
                Check("X.509 证书加密", nameof(UiSettings.EncX509)); Check("RSA-AES", nameof(UiSettings.EncRsa));
                Field("CA 证书文件", nameof(UiSettings.Ca)); Field("证书吊销列表（CRL）", nameof(UiSettings.Crl));
                Heading("身份认证");
                Check("无认证", nameof(UiSettings.AuthNone)); Check("VNC 密码", nameof(UiSettings.AuthVnc));
                Check("用户名与密码", nameof(UiSettings.AuthPlain)); Check("RSA 用户名与密码", nameof(UiSettings.AuthRsa));
                Note(SecurityNote());
                break;
            case "输入":
                Check("仅查看，不发送鼠标和键盘", nameof(UiSettings.ViewOnly));
                Heading("鼠标");
                Check("同时按左右键模拟中键", nameof(UiSettings.EmulateMiddle)); Check("服务器未提供光标时显示本地光标", nameof(UiSettings.AlwaysCursor));
                Field("本地光标类型", nameof(UiSettings.Cursor), new[] { "圆点", "系统" });
                Heading("键盘"); Check("全屏时发送系统组合键", nameof(UiSettings.SystemKeys));
                Heading("剪贴板"); Check("接受服务器剪贴板", nameof(UiSettings.AcceptClipboard)); Check("向服务器发送剪贴板", nameof(UiSettings.SendClipboard));
                break;
            case "快捷键":
                Note("触发本地会话快捷操作的修饰键：");
                Check("Ctrl", nameof(UiSettings.Ctrl)); Check("Shift", nameof(UiSettings.Shift)); Check("Alt", nameof(UiSettings.Alt)); Check("Win", nameof(UiSettings.Super));
                var summary = new TextBlock { Name = "ShortcutSummary", TextWrapping = TextWrapping.Wrap, Foreground = (Brush)FindResource("Text2Brush") };
                _settingControls["ShortcutSummary"] = summary;
                Add(summary, true);
                Note("在会话窗口里按下列组合键执行本地操作（不会发给远端）：\n"
                    + "· 松开全部修饰键：解除键盘抓取，退出全屏\n"
                    + "· 修饰键 + G：重新抓取全部键盘输入\n"
                    + "· 修饰键 + Enter：切换全屏\n"
                    + "· 修饰键 + M：最小化会话窗口\n"
                    + "· 修饰键 + 空格：只发送修饰键，方便用户自己组成组合键\n"
                    + "一个修饰键都不勾选时，所有按键原样发给远端。");
                break;
            case "显示":
                Heading("显示模式");
                foreach (string mode in new[] { "窗口", "当前显示器全屏", "全部显示器全屏", "选定显示器全屏" })
                {
                    var radio = new RadioButton { Content = new TextBlock { Text = mode, TextWrapping = TextWrapping.Wrap }, GroupName = "DisplayMode", Foreground = (Brush)FindResource("TextBrush"), IsChecked = _draft.DisplayMode == mode };
                    radio.Checked += (_, _) =>
                    {
                        _draft.DisplayMode = mode;
                        UpdateDependencies();
                        SettingsNotice.Text = mode == "选定显示器全屏"
                            ? "选定显示器全屏尚未接通，会话窗口会按当前显示器全屏并说明。"
                            : "显示模式已选好；点「应用」后，会话窗口的全屏按钮按这个模式切换。";
                    };
                    Add(radio);
                }
                Check("窗口变化时同步远程分辨率", nameof(UiSettings.RemoteResize));
                Heading("本机显示器布局");
                var map = new Canvas { Height = 118, Background = (Brush)FindResource("CardBrush"), ClipToBounds = true };
                _settingControls["MonitorMap"] = map;
                Add(map, true);
                map.SizeChanged += (_, _) => DrawMonitors(map);
                Note(_monitors.Count == 0
                    ? "无法读取本机显示器，未绘制示例屏幕。"
                    : "位置和大小按本机桌面比例绘制，仅作参考；选定显示器全屏需要多选交互，尚未接通，当前按「当前显示器全屏」处理。");
                break;
            case "杂项":
                Check("共享连接，不断开其他查看器", nameof(UiSettings.Shared)); Check("连接出错后允许重新连接", nameof(UiSettings.Reconnect)); Check("播放服务器音频", nameof(UiSettings.Audio));
                Note("三项都会写进配置并在下一次连接生效。音频需要服务器支持 QEMU 音频扩展，且本机有可用的播放设备；没有声卡时自动关闭，不影响画面和输入。");
                break;
        }
        UpdateDependencies();
    }
    /// <summary>
    /// 安全页的说明文字。必须如实反映「本核心实际编译进去哪些类型」，
    /// 不能让用户勾了 GnuTLS/Nettle 没编进来的项还以为能连。
    /// </summary>
    private string SecurityNote()
    {
        var supported = SecuritySettings.Supported();
        if (supported.Count == 0)
            return "无法从核心读取支持的安全类型；连接时如果安全类型无效会明确报错，不会静默降级。";

        var available = new List<string>();
        if (SecuritySettings.EncryptionSupported(supported, "无加密")) available.Add("无加密");
        if (SecuritySettings.EncryptionSupported(supported, "TLS")) available.Add("TLS");
        if (SecuritySettings.EncryptionSupported(supported, "X.509")) available.Add("X.509");
        if (SecuritySettings.EncryptionSupported(supported, "RSA-AES")) available.Add("RSA-AES");

        return $"本核心支持：{string.Join("、", available)}。"
            + "加密方式和认证方式会组合成完整的安全类型列表发给服务器；"
            + "证书和 SSH 主机指纹会在连接时弹到地址栏下要求你本人核对，未确认一律拒绝。";
    }

    private void UpdateDependencies()
    {
        void Enable(string property, bool enabled)
        { if (_settingControls.TryGetValue(property, out var control)) control.IsEnabled = enabled; }
        Enable(nameof(UiSettings.Encoding), !_draft.AutoSelect);
        Enable(nameof(UiSettings.Color), !_draft.AutoSelect);
        Enable(nameof(UiSettings.Quality), !_draft.AutoSelect && _draft.Jpeg);
        Enable(nameof(UiSettings.Compression), _draft.CustomCompression);
        Enable(nameof(UiSettings.Ca), _draft.EncX509);
        Enable(nameof(UiSettings.Crl), _draft.EncX509);
        Enable(nameof(UiSettings.Ca) + "Browse", _draft.EncX509);
        Enable(nameof(UiSettings.Crl) + "Browse", _draft.EncX509);
        Enable(nameof(UiSettings.Cursor), _draft.AlwaysCursor);
        // 核心没编译进去的加密方式置灰：勾了也连不上，只能误导用户。
        var supported = SecuritySettings.Supported();
        if (supported.Count > 0)
        {
            Enable(nameof(UiSettings.EncTls), SecuritySettings.EncryptionSupported(supported, "TLS"));
            Enable(nameof(UiSettings.EncX509), SecuritySettings.EncryptionSupported(supported, "X.509"));
            Enable(nameof(UiSettings.EncRsa), SecuritySettings.EncryptionSupported(supported, "RSA-AES"));
        }
        if (_settingControls.TryGetValue("ShortcutSummary", out var summary))
        {
            var mask = ShortcutHandler.Modifier.None;
            if (_draft.Ctrl) mask |= ShortcutHandler.Modifier.Control;
            if (_draft.Shift) mask |= ShortcutHandler.Modifier.Shift;
            if (_draft.Alt) mask |= ShortcutHandler.Modifier.Alt;
            if (_draft.Super) mask |= ShortcutHandler.Modifier.Super;
            string text = ShortcutHandler.ModifierString(mask);
            ((TextBlock)summary).Text = text.Length == 0
                ? "当前未设置快捷键修饰键：所有按键原样发给远端。"
                : $"当前组合：{text} + 字母键（G 抓取键盘 / M 最小化 / Enter 全屏 / 空格只发修饰键）";
        }
        if (_settingControls.TryGetValue("MonitorMap", out var map)) DrawMonitors((Canvas)map);
    }
    private void ApplySettings_Click(object sender, RoutedEventArgs e)
    {
        if (!IsLevel(_draft.Compression) || !IsLevel(_draft.Quality))
        {
            SelectSettingsCategory("压缩");
            SettingsNotice.Text = "草稿未确认：压缩等级和 JPEG 质量必须是 0–9 的整数。";
            _settingControls[!IsLevel(_draft.Compression) ? nameof(UiSettings.Compression) : nameof(UiSettings.Quality)].Focus();
            return;
        }
        if (_draft.DisplayMode == "选定显示器全屏" && _draft.SelectedMonitors.Count == 0)
        { SelectSettingsCategory("显示"); SettingsNotice.Text = "草稿未确认：请至少选择一台显示器。"; return; }
        // 已接通的字段：仅查看、编码、压缩、JPEG 质量。其余仍在草稿阶段，
        // 提示里必须分开说，不能让用户以为全都生效了。
        var applied = new List<string>();
        try
        {
            ApplyBool("ViewOnly", _draft.ViewOnly, applied, "仅查看");
            ApplyBool("AutoSelect", _draft.AutoSelect, applied, "自动选择编码");
            // 原生键是取反的 NoJPEG；界面勾选"允许 JPEG"对应 NoJPEG=0。
            if ((_configuration.GetBoolean("NoJPEG") ?? false) == _draft.Jpeg)
            {
                _configuration.Set("NoJPEG", _draft.Jpeg ? "0" : "1");
                applied.Add("允许 JPEG");
            }
            ApplyBool("CustomCompressLevel", _draft.CustomCompression, applied, "自定义压缩等级");
            ApplyString("PreferredEncoding", _draft.Encoding, applied, "首选编码");
            ApplyString("CompressLevel", _draft.Compression, applied, "压缩等级");
            ApplyString("QualityLevel", _draft.Quality, applied, "JPEG 质量");
            ApplyBool("AcceptClipboard", _draft.AcceptClipboard, applied, "接受服务器剪贴板");
            ApplyBool("SendClipboard", _draft.SendClipboard, applied, "发送剪贴板");
            ApplyBool("EmulateMiddleButton", _draft.EmulateMiddle, applied, "中键模拟");
            ApplyBool("FullscreenSystemKeys", _draft.SystemKeys, applied, "全屏系统组合键");
            ApplyBool("AlwaysCursor", _draft.AlwaysCursor, applied, "本地光标");
            ApplyString("CursorType", _draft.Cursor, applied, "光标类型");
            ApplyBool("Shared", _draft.Shared, applied, "共享连接");
            ApplyBool("ReconnectOnError", _draft.Reconnect, applied, "断线重连");
            ApplyString("FullScreenMode", _draft.DisplayMode, applied, "显示模式");
            // 显示器选择只在「选定显示器全屏」下有意义；其他模式不写，避免残留脏数据。
            if (_draft.DisplayMode == "选定显示器全屏")
                ApplyString("FullScreenSelectedMonitors", SerializeSelectedMonitors(), applied, "选定显示器");
            ApplyBool("RemoteResize", _draft.RemoteResize, applied, "同步远程分辨率");
            ApplyBool("Audio", _draft.Audio, applied, "播放服务器音频");

            // 安全类型：加密 × 认证的笛卡尔积。空串代表一个组合都没选，
            // 这时必须拒绝保存，否则连接阶段会因为「没有任何可用安全类型」失败。
            string security = SecuritySettings.Build(
                _draft.EncNone, _draft.EncTls, _draft.EncX509, _draft.EncRsa,
                _draft.AuthNone, _draft.AuthVnc, _draft.AuthPlain, _draft.AuthRsa);
            if (security.Length == 0)
            {
                SelectSettingsCategory("安全");
                SettingsNotice.Text = "草稿未确认：至少选择一种加密方式和一种认证方式，或使用「无加密 + 无认证」。";
                return;
            }
            ApplyString("SecurityTypes", security, applied, "安全类型");
            ApplyString("X509CA", _draft.Ca, applied, "CA 证书");
            ApplyString("X509CRL", _draft.Crl, applied, "证书吊销列表");

            // 快捷键修饰键：勾哪几个就存哪几个，空串表示不拦截任何按键。
            var modifiers = ShortcutHandler.Modifier.None;
            if (_draft.Ctrl) modifiers |= ShortcutHandler.Modifier.Control;
            if (_draft.Shift) modifiers |= ShortcutHandler.Modifier.Shift;
            if (_draft.Alt) modifiers |= ShortcutHandler.Modifier.Alt;
            if (_draft.Super) modifiers |= ShortcutHandler.Modifier.Super;
            ApplyString("ShortcutModifiers", ShortcutHandler.ModifierString(modifiers),
                applied, "快捷键修饰键");
        }
        catch (InvalidDataException)
        {
            SettingsNotice.Text = "配置已达到大小上限，本次没有应用任何设置。";
            return;
        }

        if (applied.Count == 0)
            SettingsNotice.Text = "界面值和已保存的配置一致，没有需要应用的改动。";
        else if (_sessionWindow is not null)
            SettingsNotice.Text = $"已保存 {string.Join("、", applied)}。编码、音频和安全类型需要重新连接才会重新协商。";
        else
            SettingsNotice.Text = $"已应用 {string.Join("、", applied)}，下一次连接生效；可另存为配置。";

        void ApplyBool(string key, bool value, List<string> changed, string label)
        {
            if ((_configuration.GetBoolean(key) ?? false) == value) return;
            _configuration.Set(key, value ? "1" : "0");
            changed.Add(label);
        }
        void ApplyString(string key, string value, List<string> changed, string label)
        {
            if (string.Equals(_configuration.Get(key) ?? "", value, StringComparison.Ordinal)) return;
            _configuration.Set(key, value);
            changed.Add(label);
        }
    }
    private static bool IsLevel(string value) => value.Length == 1 && value[0] is >= '0' and <= '9';

    /// <summary>
    /// 解析配置里的 <c>FullScreenSelectedMonitors</c>（逗号分隔的显示器编号，1 起）。
    /// 非法项直接跳过——宁可少选也不要因为一个坏值整份失效。
    /// </summary>
    private static int[] ParseMonitorIndices(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return Array.Empty<int>();
        var indices = new List<int>();
        foreach (string part in value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            if (int.TryParse(part, out int index) && index >= 1 && index <= 64 && !indices.Contains(index))
                indices.Add(index);
        return indices.ToArray();
    }

    /// <summary>
    /// 把界面上选中的显示器（按 Device 名记录）翻成配置里的序号串（1 起）。
    /// 顺序必须和 <see cref="_monitors"/> 一致，否则全屏会贴到错误的屏幕上。
    /// </summary>
    private string SerializeSelectedMonitors()
    {
        var indices = new List<int>();
        for (int i = 0; i < _monitors.Count; i++)
            if (_draft.SelectedMonitors.Contains(_monitors[i].Device))
                indices.Add(i + 1);
        return string.Join(",", indices);
    }
    private void SelectSettingsCategory(string category)
    {
        foreach (RadioButton tab in SettingsTabs.Children)
            if ((string)tab.Tag == category) { tab.IsChecked = true; break; }
    }
    private void ResetSettings_Click(object sender, RoutedEventArgs e)
    {
        _draft = new UiSettings();
        if (_monitors.Count > 0) _draft.SelectedMonitors.Add(_monitors[0].Device);
        RenderSettings();
        SettingsNotice.Text = "默认值已填入界面；实际连接设置未改变。";
    }

    private sealed record MonitorInfo(string Device, int X, int Y, int Width, int Height);
    [StructLayout(LayoutKind.Sequential)] private struct NativeRect { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct NativeMonitorInfo
    { public int Size; public NativeRect Monitor; public NativeRect Work; public uint Flags; [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Device; }
    private delegate bool MonitorCallback(IntPtr monitor, IntPtr dc, IntPtr rect, IntPtr data);
    [DllImport("user32.dll")] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool EnumDisplayMonitors(IntPtr dc, IntPtr clip, MonitorCallback callback, IntPtr data);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetMonitorInfo(IntPtr monitor, ref NativeMonitorInfo info);
    private void ReadMonitors()
    {
        // 只读显示拓扑；不调用更改分辨率、注册表或系统设置的接口。
        try
        {
            EnumDisplayMonitors(IntPtr.Zero, IntPtr.Zero, (monitor, dc, rect, data) =>
            {
                var info = new NativeMonitorInfo { Size = Marshal.SizeOf<NativeMonitorInfo>(), Device = "" };
                if (GetMonitorInfo(monitor, ref info) && info.Monitor.Right > info.Monitor.Left && info.Monitor.Bottom > info.Monitor.Top)
                    _monitors.Add(new MonitorInfo(info.Device, info.Monitor.Left, info.Monitor.Top, info.Monitor.Right - info.Monitor.Left, info.Monitor.Bottom - info.Monitor.Top));
                return true;
            }, IntPtr.Zero);
            if (_monitors.Count > 0) _draft.SelectedMonitors.Add(_monitors[0].Device);
        }
        catch (Exception ex) { Debug.WriteLine($"[monitor] {ex.GetType().Name}"); }
    }
    private void DrawMonitors(Canvas canvas)
    {
        canvas.Children.Clear();
        if (_monitors.Count == 0 || canvas.ActualWidth < 1) return;
        int left = _monitors.Min(m => m.X), top = _monitors.Min(m => m.Y);
        int width = _monitors.Max(m => m.X + m.Width) - left, height = _monitors.Max(m => m.Y + m.Height) - top;
        double scale = Math.Min((canvas.ActualWidth - 12) / width, (canvas.Height - 12) / height);
        double offsetX = (canvas.ActualWidth - width * scale) / 2, offsetY = (canvas.Height - height * scale) / 2;
        for (int i = 0; i < _monitors.Count; i++)
        {
            var monitor = _monitors[i];
            bool selected = _draft.SelectedMonitors.Contains(monitor.Device);
            var toggle = new ToggleButton
            {
                Style = (Style)FindResource("DarkToggle"),
                Margin = new Thickness(0),
                Content = new TextBlock { Text = $"{i + 1}\n{monitor.Width} × {monitor.Height}", TextAlignment = TextAlignment.Center, FontSize = 10 },
                IsChecked = selected,
                IsEnabled = _draft.DisplayMode == "选定显示器全屏",
                Width = Math.Max(1, monitor.Width * scale), Height = Math.Max(1, monitor.Height * scale),
                Foreground = (Brush)FindResource("TextBrush"),
                Background = (Brush)FindResource(selected ? "AccentSoftBrush" : "CardHoverBrush"),
                BorderBrush = (Brush)FindResource(selected ? "AccentBrush" : "Stroke2Brush"), BorderThickness = new Thickness(2),
                ToolTip = $"显示器 {i + 1} · {monitor.Width} × {monitor.Height}",
            };
            AutomationProperties.SetName(toggle, (string)toggle.ToolTip);
            toggle.Click += (_, _) =>
            {
                if (toggle.IsChecked == true) _draft.SelectedMonitors.Add(monitor.Device); else _draft.SelectedMonitors.Remove(monitor.Device);
                DrawMonitors(canvas);
                SettingsNotice.Text = "显示器选择已保留在草稿，尚未应用到会话。";
            };
            Canvas.SetLeft(toggle, offsetX + (monitor.X - left) * scale);
            Canvas.SetTop(toggle, offsetY + (monitor.Y - top) * scale);
            canvas.Children.Add(toggle);
        }
    }

    private static readonly IReadOnlyDictionary<string, string> IconPaths = new Dictionary<string, string>
    {
        ["connect"] = "M5,12 H19 M14,7 L19,12 14,17",
        ["cancel"] = "M6,6 L18,18 M18,6 L6,18",
        ["history"] = "M3,10 A9,9 0 1 1 5,18 M3,4 V10 H9 M12,7 V12 L15,14",
        ["all"] = "M4,5 H20 M4,12 H20 M4,19 H20",
        ["settings"] = "M4,7 H20 M4,17 H20 M9,4 V10 M15,14 V20",
        ["info"] = "M12,11 V17 M12,7 V7.1 M21,12 A9,9 0 1 1 3,12 A9,9 0 1 1 21,12",
        ["file"] = "M14,3 H5 V21 H19 V8 L14,3 V8 H19 M8,13 H16 M8,17 H13",
        ["save"] = "M4,3 H17 L21,7 V21 H3 V3 H4 M7,3 V9 H17 V3 M7,21 V13 H17 V21",
        ["delete"] = "M3,6 H21 M9,6 V3 H15 V6 M6,6 L7,21 H17 L18,6 M10,10 V17 M14,10 V17",
        ["edit"] = "M4,16 L16,4 20,8 8,20 H4 V16 M13,7 L17,11",
        ["refresh"] = "M20,7 V12 H15 M4,17 V12 H9 M5,8 A8,8 0 0 1 18,5 L20,8 M4,16 L6,19 A8,8 0 0 0 19,16",
        ["check"] = "M5,12 L9,16 19,6",
    };
    private void SetButton(Button button, string label, string icon) => SetLabel(button, label, icon);
    private void SetLabel(ContentControl control, string label, string icon)
    {
        var panel = new StackPanel { Orientation = Orientation.Horizontal };
        var path = new ShapePath
        {
            Data = Geometry.Parse(IconPaths[icon]), Width = 14, Height = 14, Stretch = Stretch.Uniform,
            StrokeThickness = 1.6, StrokeStartLineCap = PenLineCap.Round, StrokeEndLineCap = PenLineCap.Round,
            StrokeLineJoin = PenLineJoin.Round, Margin = new Thickness(0, 0, 5, 0), VerticalAlignment = VerticalAlignment.Center,
            Focusable = false, IsHitTestVisible = false,
        };
        path.SetBinding(ShapePath.StrokeProperty, new System.Windows.Data.Binding("Foreground") { Source = control });
        panel.Children.Add(path);
        panel.Children.Add(new TextBlock { Text = label, VerticalAlignment = VerticalAlignment.Center });
        control.Content = panel;
        AutomationProperties.SetName(control, label);
    }
}
