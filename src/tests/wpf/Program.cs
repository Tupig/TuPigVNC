using System.IO;
using System.Reflection;
using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Threading;
using TuPigVNC.Wpf;
using TuPigVNC.Wpf.Services;

namespace WpfFunctionalTests;

/// <summary>
/// WPF 功能行为测试：每个可见功能至少一条断言。
/// 只验证行为（控件状态、配置持久化、历史记录、参数解析），
/// 外观对比度由 tmp/verification/ui-test 单独覆盖。
///
/// 用法：dotnet run -c Release --project src/tests/wpf
/// 退出码 0 = 全过，非 0 = 有用例失败。
/// </summary>
internal static class Program
{
    private static int _pass, _fail;
    private static readonly List<string> _failures = new();
    private static readonly StringBuilder _log = new();

    [STAThread]
    private static int Main()
    {
        var app = new Application { ShutdownMode = ShutdownMode.OnExplicitShutdown };
        app.Resources.MergedDictionaries.Add(new ResourceDictionary
        {
            Source = new Uri("pack://application:,,,/TuPigVNC;component/Themes/Material3.xaml")
        });

        try
        {
            Run("命令行参数解析", TestCommandLine);
            Run("历史记录增删与去重", TestHistory);
            Run("主窗口导航切页", TestNavigation);
            Run("地址栏占位符联动", TestAddressBar);
            Run("设置页六个分类", TestSettingsTabs);
            Run("关于页版本与更新入口", TestAboutPage);
            Run("记录操作面板", TestRecordPanels);
            Run("信任指纹面板", TestTrustPanel);
            Run("窗口自适应高度", TestAdaptiveHeight);
        }
        catch (Exception ex)
        {
            Fail("测试进程异常", $"{ex.GetType().Name}: {ex.Message}");
        }
        finally
        {
            app.Shutdown();
        }

        Console.WriteLine();
        if (_log.Length > 0) Console.WriteLine(_log.ToString());
        Console.WriteLine($"=== {_pass} 通过 / {_fail} 失败 ===");
        foreach (var f in _failures) Console.WriteLine("  ✗ " + f);
        return _fail == 0 ? 0 : 1;
    }

    // ============================================================
    // 用例
    // ============================================================

    private static void TestCommandLine()
    {
        Expect("-h 触发帮助", CommandLineOptions.Parse(new[] { "-h" }).ShowHelp);
        Expect("--help 触发帮助", CommandLineOptions.Parse(new[] { "--help" }).ShowHelp);
        Expect("-v 触发版本", CommandLineOptions.Parse(new[] { "-v" }).ShowVersion);

        var unknown = CommandLineOptions.Parse(new[] { "--no-such-flag" });
        Expect("未知参数报错", unknown.Error is not null, unknown.Error);

        var badPort = CommandLineOptions.Parse(new[] { "host:99999" });
        Expect("端口越界被拒", badPort.Error is not null, badPort.Error);

        var ok = CommandLineOptions.Parse(new[] { "192.0.2.10:5901" });
        Expect("合法地址被接受", ok.Error is null, ok.Error);
        Expect("地址原样保留", ok.Server == "192.0.2.10:5901", ok.Server);

        var listen = CommandLineOptions.Parse(new[] { "-listen", "5501" });
        Expect("-listen 进入监听模式", listen.Listen);
        Expect("监听端口解析", listen.ListenPort == 5501, listen.ListenPort.ToString());

        var via = CommandLineOptions.Parse(new[] { "-via", "gateway.example" });
        Expect("-via 解析网关", via.Via == "gateway.example", via.Via);
        Expect("-via 缺参数报错",
               CommandLineOptions.Parse(new[] { "-via" }).Error is not null);
    }

    private static void TestHistory()
    {
        using var temp = new TempDir();
        var store = new HistoryStore(temp.FilePath);

        store.Load();
        Expect("空文件载入不抛", true);

        long now = DateTimeOffset.UtcNow.ToUnixTimeSeconds();
        store.AddOrTouch("192.0.2.1:5900");
        store.AddOrTouch("192.0.2.2:5900");
        store.Save();

        var reloaded = new HistoryStore(temp.FilePath);
        reloaded.Load();
        Expect("两条记录落盘", reloaded.Entries.Count == 2, $"{reloaded.Entries.Count}");

        // 同地址再次 touch：不新增条目，次数 +1
        int countBefore = reloaded.Entries.Count;
        int useBefore = reloaded.Entries.First(e => e.Host.Contains("192.0.2.1")).UseCount;
        reloaded.AddOrTouch("192.0.2.1:5900");
        Expect("同地址不重复添加", reloaded.Entries.Count == countBefore,
               $"{countBefore} -> {reloaded.Entries.Count}");
        Expect("连接次数累加",
               reloaded.Entries.First(e => e.Host.Contains("192.0.2.1")).UseCount > useBefore);

        // 删除
        var victim = reloaded.Entries.First();
        bool removed = reloaded.Remove(victim);
        Expect("删除成功", removed);
        Expect("删除后少一条", reloaded.Entries.Count == countBefore - 1);
    }

    private static void TestNavigation()
    {
        var win = NewMainWindow();
        try
        {
            foreach (var name in new[] { "NavRecent", "NavAll", "NavSettings", "NavAbout" })
                Expect($"导航项 {name} 存在", win.FindName(name) is RadioButton);

            var recent = (RadioButton)win.FindName("NavRecent");
            var about = (RadioButton)win.FindName("NavAbout");
            var records = (FrameworkElement)win.FindName("RecordsPage");
            var settings = (FrameworkElement)win.FindName("SettingsPage");
            var aboutPage = (FrameworkElement)win.FindName("AboutPage");

            recent.IsChecked = true;
            win.UpdateLayout();
            Expect("记录页可见", records.Visibility == Visibility.Visible);
            Expect("设置页收起", settings.Visibility == Visibility.Collapsed);

            about.IsChecked = true;
            win.UpdateLayout();
            Expect("关于页可见", aboutPage.Visibility == Visibility.Visible);
            Expect("记录页收起", records.Visibility == Visibility.Collapsed);
        }
        finally { win.Close(); }
    }

    private static void TestAddressBar()
    {
        var win = NewMainWindow();
        try
        {
            var input = (TextBox)win.FindName("ServerInput");
            var placeholder = (TextBlock)win.FindName("ServerPlaceholder");
            Expect("地址栏存在", input is not null);
            Expect("初始占位符可见", placeholder.Visibility == Visibility.Visible);

            input.Text = "192.0.2.5";
            win.UpdateLayout();
            Expect("有输入后占位符隐藏",
                   placeholder.Visibility == Visibility.Collapsed,
                   placeholder.Visibility.ToString());

            input.Text = "";
            win.UpdateLayout();
            Expect("清空后占位符恢复", placeholder.Visibility == Visibility.Visible);
        }
        finally { win.Close(); }
    }

    private static void TestSettingsTabs()
    {
        var win = NewMainWindow();
        try
        {
            var tabs = (Panel)win.FindName("SettingsTabs");
            var fields = (Panel)win.FindName("SettingFields");
            Expect("分类标签容器存在", tabs is not null);
            Expect("字段容器存在", fields is not null);
            if (tabs is null || fields is null) return;

            Expect("六个分类", tabs.Children.Count == 6, $"{tabs.Children.Count}");

            int empty = 0;
            foreach (RadioButton tab in tabs.Children)
            {
                tab.IsChecked = true;
                win.UpdateLayout();
                if (fields.Children.Count == 0) empty++;
            }
            Expect("每个分类都有字段", empty == 0, $"{empty} 个分类空白");
        }
        finally { win.Close(); }
    }

    private static void TestAboutPage()
    {
        var win = NewMainWindow();
        try
        {
            var version = (TextBlock)win.FindName("AboutVersionText");
            var current = (TextBlock)win.FindName("CurrentVersionText");
            Expect("版本文本非空", version?.Text?.Length > 0, version?.Text);
            Expect("当前版本文本非空", current?.Text?.Length > 0, current?.Text);
            Expect("版本号不是 1.0.0 占位", version is not null && !version.Text.Contains("1.0.0"),
                   version?.Text);
            Expect("检查更新按钮存在", win.FindName("CheckUpdate") is Button);
        }
        finally { win.Close(); }
    }

    private static void TestRecordPanels()
    {
        var win = NewMainWindow();
        try
        {
            var rename = (FrameworkElement)win.FindName("RenamePanel");
            var delete = (FrameworkElement)win.FindName("DeletePanel");
            Expect("重命名面板存在", rename is not null);
            Expect("删除面板存在", delete is not null);
            Expect("初始都收起",
                   rename.Visibility == Visibility.Collapsed &&
                   delete.Visibility == Visibility.Collapsed);
        }
        finally { win.Close(); }
    }

    private static void TestTrustPanel()
    {
        var win = NewMainWindow();
        try
        {
            var panel = (FrameworkElement)win.FindName("TrustPanel");
            var check = (CheckBox)win.FindName("TrustCheck");
            var accept = (Button)win.FindName("TrustAccept");
            Expect("信任面板存在", panel is not null);
            Expect("初始隐藏", panel.Visibility == Visibility.Collapsed);
            Expect("默认不可接受（必须先核对指纹）", !accept.IsEnabled);

            check.IsChecked = true;
            win.UpdateLayout();
            Expect("勾选后可接受", accept.IsEnabled);
        }
        finally { win.Close(); }
    }

    private static void TestAdaptiveHeight()
    {
        var win = NewMainWindow();
        try
        {
            double baseHeight = win.Height;

            // 切到设置页应长高（内容比记录页多）
            ((RadioButton)win.FindName("NavSettings")).IsChecked = true;
            win.UpdateLayout();
            Dispatcher.CurrentDispatcher.Invoke(DispatcherPriority.Loaded, new Action(() => { }));
            double settingsHeight = win.Height;

            Expect("设置页窗口变高", settingsHeight >= baseHeight,
                   $"{baseHeight:F0} -> {settingsHeight:F0}");

            // 切回记录页恢复
            ((RadioButton)win.FindName("NavRecent")).IsChecked = true;
            win.UpdateLayout();
            Dispatcher.CurrentDispatcher.Invoke(DispatcherPriority.Loaded, new Action(() => { }));
            Expect("切回记录页恢复高度", Math.Abs(win.Height - baseHeight) < 1,
                   $"{win.Height:F0} vs {baseHeight:F0}");
        }
        finally { win.Close(); }
    }

    // ============================================================
    // 工具
    // ============================================================

    private static MainWindow NewMainWindow()
    {
        var win = (MainWindow)Activator.CreateInstance(
            typeof(MainWindow), new object[] { null });
        win.Show();
        win.UpdateLayout();
        Dispatcher.CurrentDispatcher.Invoke(DispatcherPriority.Loaded, new Action(() => { }));
        return win;
    }

    private static void Run(string name, Action test)
    {
        int before = _fail;
        try { test(); }
        catch (Exception ex) { Fail(name + " 抛异常", $"{ex.GetType().Name}: {ex.Message}"); }
        Console.WriteLine($"[{(_fail == before ? "PASS" : "FAIL")}] {name}");
    }

    private static void Expect(string what, bool ok, string detail = null)
    {
        if (ok) { _pass++; return; }
        _fail++;
        string msg = what + (detail is null ? "" : $" -> {detail}");
        _failures.Add(msg);
        _log.AppendLine("  FAIL " + msg);
    }

    private static void Fail(string what, string detail)
    {
        _fail++;
        _failures.Add($"{what} -> {detail}");
        _log.AppendLine($"  FAIL {what} -> {detail}");
    }

    /// <summary>临时目录里的 history.json，用完即删。</summary>
    private sealed class TempDir : IDisposable
    {
        private readonly string _dir = Path.Combine(
            Path.GetTempPath(), "TuPigVNC-tests", Guid.NewGuid().ToString("N"));

        public string FilePath => Path.Combine(_dir, "history.json");

        public TempDir() => Directory.CreateDirectory(_dir);

        public void Dispose()
        {
            try { Directory.Delete(_dir, true); } catch { }
        }
    }
}
