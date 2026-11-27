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
