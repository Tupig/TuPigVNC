using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Interop;
using System.Windows.Media;

namespace TuPigVNC.Wpf;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        // 强制软件渲染：任何环境（无 GPU / 远程桌面 / 虚拟显示 / 图形驱动异常）都能显示。
        // 这是 WPF 相对 WinUI 3 的核心优势——WinUI 3 没有软件回退，本机实测全空白。
        RenderOptions.ProcessRenderMode = System.Windows.Interop.RenderMode.SoftwareOnly;

        base.OnStartup(e);
    }

    /// <summary>
    /// 把窗口标题栏切成暗色，并套用 Windows 11 的 20px 圆角，
    /// 与旧版 ServerDialog::show() 的 DWM 处理保持一致。
    /// </summary>
    public static void ApplyDarkTitleBar(Window window)
    {
        IntPtr hwnd = new WindowInteropHelper(window).Handle;
        if (hwnd == IntPtr.Zero)
            return;

        int dark = 1;
        // DWMWA_USE_IMMERSIVE_DARK_MODE = 20（Win10 1809+ 用 19）
        DwmSetWindowAttribute(hwnd, 20, ref dark, sizeof(int));
        DwmSetWindowAttribute(hwnd, 19, ref dark, sizeof(int));

        int round = 2; // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd, 33, ref round, sizeof(int));
    }

    [DllImport("dwmapi.dll")]
    private static extern int DwmSetWindowAttribute(IntPtr hwnd, int attribute,
        ref int value, int size);
}
