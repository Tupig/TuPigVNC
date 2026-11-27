using System.IO;
using Microsoft.Win32;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 原生文件对话框。WPF 自带的 OpenFileDialog / SaveFileDialog 就是
/// Windows 资源管理器同款界面，不需要像旧版那样自己 P/Invoke。
/// </summary>
internal static class NativeFileDialog
{
    private const string ConfigFilter =
        "TuPigVNC 配置文件 (*.tigervnc)|*.tigervnc|所有文件 (*.*)|*.*";

    public static string? OpenConfig(string initialDir)
    {
        var dialog = new OpenFileDialog
        {
            Title = "载入 TuPigVNC 配置",
            Filter = ConfigFilter,
            DefaultExt = ".tigervnc",
            CheckFileExists = true,
        };
        if (!string.IsNullOrEmpty(initialDir) && Directory.Exists(initialDir))
            dialog.InitialDirectory = initialDir;

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }

    public static string? SaveConfig(string initialDir, string suggestedName)
    {
        var dialog = new SaveFileDialog
        {
            Title = "保存 TuPigVNC 配置",
            Filter = ConfigFilter,
            DefaultExt = ".tigervnc",
            FileName = suggestedName,
            OverwritePrompt = true, // 系统自己弹「是否替换」
        };
        if (!string.IsNullOrEmpty(initialDir) && Directory.Exists(initialDir))
            dialog.InitialDirectory = initialDir;

        return dialog.ShowDialog() == true ? dialog.FileName : null;
    }
}
