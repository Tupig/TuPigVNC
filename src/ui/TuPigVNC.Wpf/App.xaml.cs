using System.IO;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;
using TuPigVNC.Wpf.Services;

namespace TuPigVNC.Wpf;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        // 保留当前软件渲染回退，避免界面迁移同时改变渲染后端。
        // 具体环境是否可用仍需实际启动验证，不保证所有驱动或系统均可显示。
        RenderOptions.ProcessRenderMode = System.Windows.Interop.RenderMode.SoftwareOnly;

        // UI 线程上的未处理异常不能静默吞掉——那会让「点了按钮没反应」
        // 变成无从排查的幽灵问题。写到用户临时目录下的日志里，
        // 同时在界面上给出可见提示后退出。
        DispatcherUnhandledException += OnDispatcherUnhandledException;
        AppDomain.CurrentDomain.UnhandledException += OnDomainUnhandledException;

        base.OnStartup(e);

        // 命令行：-h / -v 直接输出到控制台并退出，不建窗口。
        var options = CommandLineOptions.Parse(e.Args);
        if (options.ShowHelp || options.ShowVersion)
        {
            ShowConsoleText(options.ShowHelp
                ? CommandLineOptions.HelpText(GetProgramName())
                : $"TuPigVNC {typeof(App).Assembly.GetName().Version}");
            Shutdown(options.ShowHelp ? 1 : 0);
            return;
        }

        if (options.Error is { } error)
        {
            ShowConsoleText($"参数错误：{error}\n\n{CommandLineOptions.HelpText(GetProgramName())}");
            Shutdown(2);
            return;
        }

        MainWindow = new MainWindow(options);
        MainWindow.Show();
    }

    /// <summary>
    /// UI 线程未处理异常：记日志 + 提示用户，然后继续运行而不是静默崩溃。
    /// 崩溃现场比「窗口突然没了」有用得多。
    /// </summary>
    private void OnDispatcherUnhandledException(
        object sender, System.Windows.Threading.DispatcherUnhandledExceptionEventArgs e)
    {
        string log = WriteCrashLog("UI", e.Exception);
        MessageBox.Show(
            $"发生未处理的界面错误：\n\n{e.Exception.Message}\n\n详细信息已写入：\n{log}",
            "TuPigVNC · 错误", MessageBoxButton.OK, MessageBoxImage.Error);
        e.Handled = true;
    }

    private static void OnDomainUnhandledException(object sender, UnhandledExceptionEventArgs e)
    {
        if (e.ExceptionObject is Exception ex)
            WriteCrashLog("Domain", ex);
    }

    /// <summary>把异常写到 %TEMP%\TuPigVNC\crash-*.log，返回路径。</summary>
    private static string WriteCrashLog(string source, Exception ex)
    {
        try
        {
            string dir = Path.Combine(Path.GetTempPath(), "TuPigVNC");
            Directory.CreateDirectory(dir);
            string file = Path.Combine(dir, $"crash-{DateTime.Now:yyyyMMdd-HHmmss}-{source}.log");
            File.WriteAllText(file,
                $"[{DateTime.Now:O}] 来源={source}\n{ex}\n");
            return file;
        }
        catch (Exception)
        {
            // 连日志都写不了（磁盘满/权限）时不能再抛——否则递归崩溃。
            return "(无法写入日志)";
        }
    }

    private static string GetProgramName() =>
        Path.GetFileNameWithoutExtension(Environment.ProcessPath) ?? "TuPigVNC";

    /// <summary>
    /// 帮助和版本号走控制台。WPF 是 GUI 子系统，没有控制台时
    /// 写到 stderr 会丢；用 MessageBox 保证用户看得见。
    /// </summary>
    private static void ShowConsoleText(string text)
    {
        if (TryWriteConsole(text)) return;
        MessageBox.Show(text, "TuPigVNC", MessageBoxButton.OK, MessageBoxImage.Information);
    }

    private static bool TryWriteConsole(string text)
    {
        try
        {
            if (GetConsoleWindow() == IntPtr.Zero) return false;
            Console.Out.Write(text + "\n");
            return true;
        }
        catch (Exception) { return false; }
    }

    [DllImport("kernel32.dll")]
    private static extern IntPtr GetConsoleWindow();

    /// <summary>
    /// 把窗口标题栏切成暗色，并套用 Windows 11 的 20px 圆角，
    /// 与旧版 ServerDialog::show() 的 DWM 处理保持一致。
    /// </summary>
    public static void ApplyDarkTitleBar(Window window) => ApplyDarkTitleBar(window, true);

    /// <param name="systemRoundCorners">
    /// 自绘边框和圆角的无边框窗口传 false：DWM 再切一次会把 1px 边框切成缺口。
    /// </param>
    public static void ApplyDarkTitleBar(Window window, bool systemRoundCorners)
    {
        IntPtr hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero)
            return;

        int dark = 1;
        // DWMWA_USE_IMMERSIVE_DARK_MODE = 20（Win10 1809+ 用 19）
        DwmSetWindowAttribute(hwnd, 20, ref dark, sizeof(int));
        DwmSetWindowAttribute(hwnd, 19, ref dark, sizeof(int));

        // DWMWA_WINDOW_CORNER_PREFERENCE：1 = DONOTROUND，2 = ROUND。
        int round = systemRoundCorners ? 2 : 1; // DWMWCP_ROUND / DWMWCP_DONOTROUND
        DwmSetWindowAttribute(hwnd, 33, ref round, sizeof(int));
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute,
        ref int value, int size);
}
