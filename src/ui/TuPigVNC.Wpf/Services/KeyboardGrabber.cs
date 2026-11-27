using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows.Input;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 全屏时接管系统组合键（Alt+Tab、Win 键等），让它们发到远端而不是切走本机窗口。
///
/// 等价于原版 <c>DesktopWindow::grabKeyboard()</c> 的 FLTK 键盘抓取。
/// Windows 上没有"只给某个窗口"的抓取 API，只能用 WH_KEYBOARD_LL 低级钩子
/// 拦下按键再转给会话窗口。
///
/// 安全边界：钩子只在本进程内安装，不写注册表、不注入其他进程；
/// 只要会话窗口不在前台就立即放行所有按键，不会影响本机其他程序。
/// 钩子回调必须尽快返回，所以这里只做转发，不做任何耗时操作。
/// </summary>
internal sealed class KeyboardGrabber : IDisposable
{
    private const int WH_KEYBOARD_LL = 13;
    private const int WM_KEYDOWN = 0x0100;
    private const int WM_KEYUP = 0x0101;
    private const int WM_SYSKEYDOWN = 0x0104;
    private const int WM_SYSKEYUP = 0x0105;

    private readonly Action<Key, bool> _forward;
    private readonly Func<bool> _shouldGrab;
    private readonly LowLevelKeyboardProc _proc;
    private IntPtr _hook;
    private bool _disposed;

    /// <param name="forward">把按键转发给远端：键、是否按下。</param>
    /// <param name="shouldGrab">当前是否应该接管（前台是本会话窗口且在全屏）。</param>
    public KeyboardGrabber(Action<Key, bool> forward, Func<bool> shouldGrab)
    {
        _forward = forward;
        _shouldGrab = shouldGrab;
        // 必须用字段钉住委托，否则 GC 回收后就是野指针。
        _proc = Hook;
    }

    public bool IsActive => _hook != IntPtr.Zero;

    /// <summary>开始抓取。必须在 UI 线程上调用（需要消息循环）。</summary>
    public bool Start()
    {
        if (_disposed || _hook != IntPtr.Zero) return _hook != IntPtr.Zero;
        try
        {
            _hook = SetWindowsHookEx(WH_KEYBOARD_LL, _proc, GetModuleHandle(null), 0);
            return _hook != IntPtr.Zero;
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"[keyboard] 安装钩子失败：{ex.GetType().Name}");
            _hook = IntPtr.Zero;
            return false;
        }
    }

    public void Stop()
    {
        if (_hook == IntPtr.Zero) return;
        try { UnhookWindowsHookEx(_hook); }
        catch (Exception ex) { Debug.WriteLine($"[keyboard] 卸载钩子失败：{ex.GetType().Name}"); }
        finally { _hook = IntPtr.Zero; }
    }

    private IntPtr Hook(int code, IntPtr wParam, IntPtr lParam)
    {
        // code < 0 表示必须原样放行，任何情况下都不能吞掉。
        if (code >= 0 && !_disposed && _shouldGrab())
        {
            try
            {
                int message = (int)wParam;
                bool down = message is WM_KEYDOWN or WM_SYSKEYDOWN;
                bool up = message is WM_KEYUP or WM_SYSKEYUP;
                if (down || up)
                {
                    var info = Marshal.PtrToStructure<KbdLlHookStruct>(lParam);
                    Key key = KeyInterop.KeyFromVirtualKey(unchecked((int)info.VkCode));
                    if (key != Key.None)
                    {
                        _forward(key, down);
                        // 返回 1 表示已处理，系统不再把这次按键送给本机窗口。
                        return 1;
                    }
                }
            }
            catch (Exception ex)
            {
                // 钩子回调里绝不能抛异常穿透到系统；出错就放行。
                Debug.WriteLine($"[keyboard] 转发失败：{ex.GetType().Name}");
            }
        }
        return CallNextHookEx(_hook, code, wParam, lParam);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Stop();
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct KbdLlHookStruct
    {
        public uint VkCode;
        public uint ScanCode;
        public uint Flags;
        public uint Time;
        public IntPtr ExtraInfo;
    }

    private delegate IntPtr LowLevelKeyboardProc(int code, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern IntPtr SetWindowsHookEx(int idHook, LowLevelKeyboardProc proc,
        IntPtr module, uint threadId);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool UnhookWindowsHookEx(IntPtr hook);

    [DllImport("user32.dll")]
    private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr wParam, IntPtr lParam);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
    private static extern IntPtr GetModuleHandle(string? moduleName);
}
