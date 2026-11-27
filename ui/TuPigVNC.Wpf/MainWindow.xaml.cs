using System.Diagnostics;
using System.IO;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using TuPigVNC.Wpf.Dialogs;
using TuPigVNC.Wpf.Services;

namespace TuPigVNC.Wpf;

public partial class MainWindow : Window
{
    private const string ConfigIdentifier = "TuPigVNC Configuration file Version 1.0";

    private readonly HistoryStore _history = new();
    private string _usedDir = "";
    private bool _connecting;
    private int _activePanel = -1;

    public MainWindow()
    {
        InitializeComponent();

        // 高度自适应内容，但最多占屏幕 92%（照搬旧版 applyWindowHeight）
        MaxHeight = SystemParameters.WorkArea.Height * 0.92;

        _history.Load();
        RefreshHistory();
        RefreshRecentChips();

        Loaded += (_, _) =>
        {
            ServerInput.Focus();
            UpdatePlaceholder();
        };
    }

    private void Window_SourceInitialized(object? sender, EventArgs e)
    {
        App.ApplyDarkTitleBar(this);
    }

    // ==================================================================
    // 地址输入
    // ==================================================================

    private void ServerInput_TextChanged(object sender, TextChangedEventArgs e)
    {
        UpdatePlaceholder();
    }

    private void ServerInput_FocusChanged(object sender, KeyboardFocusChangedEventArgs e)
    {
        UpdatePlaceholder();
    }

    private void UpdatePlaceholder()
    {
        if (ServerPlaceholder is null || ServerInput is null)
            return;

        ServerPlaceholder.Visibility =
            ServerInput.Text.Length == 0 && !ServerInput.IsKeyboardFocusWithin
                ? Visibility.Visible
                : Visibility.Collapsed;
    }

    private void ServerInput_KeyDown(object sender, KeyEventArgs e)
    {
        if (e.Key == Key.Enter)
        {
            e.Handled = true;
            _ = ConnectAsync();
        }
    }

    private void Connect_Click(object sender, RoutedEventArgs e)
    {
        _ = ConnectAsync();
    }

    private async Task ConnectAsync()
    {
        if (_connecting)
            return;

        string server = ServerInput.Text.Trim();

        // 空地址直接返回，否则会把 host="" 写进历史
        if (server.Length == 0)
        {
            ShowError("请输入服务器地址，例如 192.168.1.100:5900");
            ServerInput.Focus();
            return;
        }

        if (!HistoryStore.TryParseHostPort(server, out string host, out int port))
        {
            ShowError("地址格式不正确，例如 192.168.1.100:5900");
            return;
        }

        ClearError();

        // 更新历史：已存在则累加次数并置顶，否则插入新记录
        _history.AddOrTouch(server);
        SaveHistorySafe();
        RefreshRecentChips();
        RefreshHistory();

        _connecting = true;
        ConnectButton.IsEnabled = false;
        ConnectButton.Content = "连接中…";

        // 先记下 UI 上下文：CredentialProvider 会在核心网络线程上被调用，
        // 必须切回 UI 线程弹对话框。
        var dispatcher = Dispatcher;

        try
        {
            var (result, error) = await Task.Run(() =>
            {
                using var session = new VncSession();

                session.CredentialProvider = (credHost, secure, needsUser, needsPassword) =>
                {
                    return dispatcher.Invoke(() =>
                    {
                        var dialog = new AuthDialog(secure, needsUser, needsPassword, showKeepPassword: true)
                        {
                            Owner = this,
                        };
                        if (dialog.ShowDialog() != true)
                            return ((string User, string Password)?)null;
                        return (dialog.Username, dialog.Password);
                    });
                };

                var rc = session.Connect(host, port, out string err);
                if (rc == VncConnectResult.Ok)
                {
                    // 阶段 3 才接会话窗口；这里先记录日志并主动释放连接。
                    Debug.WriteLine($"[vnc] 连接成功：{credHostSafe(host)}:{port}");
                }
                return (rc, err);
            });

            if (result == VncConnectResult.Ok)
            {
                ShowInfo($"连接成功：{host}:{port}（会话窗口将在阶段 3 接入）");
            }
            else
            {
                ShowError($"连接失败（{result}）：{error}");
            }
        }
        catch (DllNotFoundException)
        {
            ShowError("核心 DLL 未找到：TuPigVNC.Core.dll 需要与程序放在同一目录。");
        }
        catch (EntryPointNotFoundException)
        {
            ShowError("核心 DLL 尚未实现连接接口（阶段 1 正在进行中）。");
        }
        catch (Exception ex)
        {
            ShowError("连接失败：" + ex.Message);
        }
        finally
        {
            _connecting = false;
            ConnectButton.IsEnabled = true;
            ConnectButton.Content = "连接";
        }
    }

    private static string credHostSafe(string host) => string.IsNullOrEmpty(host) ? "?" : host;

    // ==================================================================
    // 历史 / 最近连接
    // ==================================================================

    private void RefreshHistory()
    {
        HistoryList.ItemsSource = _history.Entries.ToList();
        HistoryEntrySubtitle.Text = $"{_history.Entries.Count} 台 · 上限 250";
    }

    private void RefreshRecentChips()
    {
        RecentChips.Children.Clear();

        int i = 0;
        foreach (var entry in _history.Entries)
        {
            if (i++ >= 3)
                break;

            string label = !string.IsNullOrEmpty(entry.Name) && entry.Name != entry.Host
                ? $"{entry.Host} · {entry.Name}"
                : entry.Host;

            var chip = new Button
            {
                Content = label,
                Style = (Style)FindResource("ChipButton"),
                MaxWidth = 200,
                Tag = entry,
            };
            chip.Click += RecentChip_Click;
            RecentChips.Children.Add(chip);
        }
    }

    private void RecentChip_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: HistoryEntry entry })
            return;

        ServerInput.Text = entry.Host;
        ServerInput.CaretIndex = ServerInput.Text.Length;
        _ = ConnectAsync();
    }

    private void HistoryRow_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (e.ClickCount >= 2 && sender is FrameworkElement { DataContext: HistoryEntry entry })
        {
            ServerInput.Text = entry.Host;
            _ = ConnectAsync();
        }
    }

    private void HistoryRow_MouseRightButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: HistoryEntry entry })
        {
            _history.Remove(entry);
            SaveHistorySafe();
            RefreshRecentChips();
            RefreshHistory();
            e.Handled = true;
        }
    }

    private void HistoryDelete_Click(object sender, RoutedEventArgs e)
    {
        if (sender is FrameworkElement { DataContext: HistoryEntry entry })
        {
            _history.Remove(entry);
            SaveHistorySafe();
            RefreshRecentChips();
            RefreshHistory();
        }
    }

    private void SaveHistorySafe()
    {
        try
        {
            _history.Save();
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[history] 保存失败：{ex.Message}");
        }
    }

    // ==================================================================
    // 展开面板
    // ==================================================================

    private void Entry_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: string tag } || !int.TryParse(tag, out int idx))
            return;

        ShowPanel(_activePanel == idx ? -1 : idx);
    }

    private void Collapse_Click(object sender, RoutedEventArgs e)
    {
        ShowPanel(-1);
    }

    private void ShowPanel(int idx)
    {
        _activePanel = idx;

        PanelHistory.Visibility = idx == 0 ? Visibility.Visible : Visibility.Collapsed;
        PanelOptions.Visibility = idx == 1 ? Visibility.Visible : Visibility.Collapsed;
        PanelUpdate.Visibility = idx == 2 ? Visibility.Visible : Visibility.Collapsed;
        PanelAbout.Visibility = idx == 3 ? Visibility.Visible : Visibility.Collapsed;
    }

    private void OptionTab_Checked(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: string tag } || !int.TryParse(tag, out int idx))
            return;

        var panes = new[] { OptPane0, OptPane1, OptPane2, OptPane3, OptPane4, OptPane5 };
        for (int i = 0; i < panes.Length; i++)
        {
            // InitializeComponent 期间 Checked 可能先于后面的元素创建，判空
            if (panes[i] is null)
                continue;
            panes[i].Visibility = i == idx ? Visibility.Visible : Visibility.Collapsed;
        }
    }

    // ==================================================================
    // 载入 / 另存为配置
    // ==================================================================

    private void LoadConfig_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_usedDir))
            _usedDir = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

        string? file = NativeFileDialog.OpenConfig(_usedDir);
        if (string.IsNullOrEmpty(file))
            return;

        _usedDir = Path.GetDirectoryName(file) ?? _usedDir;

        try
        {
            string? server = ReadServerNameFromConfig(file);
            if (server is null)
            {
                ShowError("载入配置失败：文件里没有 ServerName 项。");
                return;
            }

            ServerInput.Text = server;
            ClearError();
            ServerInput.Focus();
            ServerInput.CaretIndex = ServerInput.Text.Length;
        }
        catch (Exception ex)
        {
            ShowError("载入配置失败：" + ex.Message);
        }
    }

    private void SaveAs_Click(object sender, RoutedEventArgs e)
    {
        if (string.IsNullOrEmpty(_usedDir))
            _usedDir = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);

        string? file = NativeFileDialog.SaveConfig(_usedDir, "vnc.tigervnc");
        if (string.IsNullOrEmpty(file))
            return;

        _usedDir = Path.GetDirectoryName(file) ?? _usedDir;

        try
        {
            WriteServerNameToConfig(file, ServerInput.Text);
            ShowInfo($"已保存配置：{Path.GetFileName(file)}");
        }
        catch (Exception ex)
        {
            ShowError("保存配置失败：" + ex.Message);
        }
    }

    private static string? ReadServerNameFromConfig(string file)
    {
        foreach (string line in File.ReadAllLines(file, Encoding.UTF8))
        {
            const string prefix = "ServerName=";
            if (line.StartsWith(prefix, StringComparison.Ordinal))
                return DecodeConfigValue(line[prefix.Length..]);
        }
        return null;
    }

    private static void WriteServerNameToConfig(string file, string server)
    {
        var sb = new StringBuilder();
        sb.Append(ConfigIdentifier).Append('\n');
        sb.Append('\n');
        sb.Append("ServerName=").Append(EncodeConfigValue(server)).Append('\n');
        File.WriteAllText(file, sb.ToString(), new UTF8Encoding(false));
    }

    // 与旧版 parameters.cxx 的 replaceMap 一致：\n \r \\ 三种转义
    private static string EncodeConfigValue(string value) =>
        value.Replace("\\", "\\\\").Replace("\n", "\\n").Replace("\r", "\\r");

    private static string DecodeConfigValue(string value)
    {
        var sb = new StringBuilder(value.Length);
        for (int i = 0; i < value.Length; i++)
        {
            if (value[i] == '\\' && i + 1 < value.Length)
            {
                i++;
                sb.Append(value[i] switch
                {
                    'n' => '\n',
                    'r' => '\r',
                    '\\' => '\\',
                    _ => value[i],
                });
            }
            else
            {
                sb.Append(value[i]);
            }
        }
        return sb.ToString();
    }

    // ==================================================================
    // 更新 / 关于
    // ==================================================================

    private void CheckUpdate_Click(object sender, RoutedEventArgs e)
    {
        // 自动更新依赖 core::UpdateChecker，阶段 1 尚未通过 C ABI 暴露
        UpdateStage.Text = "当前阶段：自动更新模块尚未接入核心（阶段 1）";
        UpdateChangelog.Text = "更新检查将在核心 DLL 提供接口后接入。";
    }

    private void OpenLink_Click(object sender, RoutedEventArgs e)
    {
        if (sender is not FrameworkElement { Tag: string url })
            return;

        try
        {
            Process.Start(new ProcessStartInfo(url) { UseShellExecute = true });
        }
        catch (Exception ex)
        {
            ShowError("无法打开链接：" + ex.Message);
        }
    }

    // ==================================================================
    // 行内提示
    // ==================================================================

    private void ShowError(string message)
    {
        ErrorText.Foreground = (Brush)FindResource("ErrBrush");
        ErrorText.Text = message;
        ServerInput.Foreground = (Brush)FindResource("ErrBrush");
    }

    private void ShowInfo(string message)
    {
        ErrorText.Foreground = (Brush)FindResource("OkBrush");
        ErrorText.Text = message;
        ServerInput.Foreground = (Brush)FindResource("TextBrush");
    }

    private void ClearError()
    {
        ErrorText.Text = "";
        ServerInput.Foreground = (Brush)FindResource("TextBrush");
    }
}
