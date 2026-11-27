using System.Runtime.InteropServices;
using System.Windows.Input;

namespace TuPigVNC.Wpf.Services;

/// <summary>把 Windows 按键转换为 RFB keysym；保留左右修饰键的独立身份。</summary>
internal static class RemoteKeyMapper
{
    public static uint SpecialKey(Key key) => key switch
    {
        Key.Back => 0xff08, Key.Tab => 0xff09, Key.Return => 0xff0d,
        Key.Escape => 0xff1b, Key.Insert => 0xff63, Key.Delete => 0xffff,
        Key.Home => 0xff50, Key.End => 0xff57, Key.PageUp => 0xff55,
        Key.PageDown => 0xff56, Key.Left => 0xff51, Key.Up => 0xff52,
        Key.Right => 0xff53, Key.Down => 0xff54,
        Key.LeftShift => 0xffe1, Key.RightShift => 0xffe2,
        Key.LeftCtrl => 0xffe3, Key.RightCtrl => 0xffe4,
        Key.LeftAlt => 0xffe9, Key.RightAlt => 0xffea,
        Key.LWin => 0xffeb, Key.RWin => 0xffec, Key.Apps => 0xff67,
        Key.CapsLock => 0xffe5, Key.NumLock => 0xff7f, Key.Scroll => 0xff14,
        Key.PrintScreen => 0xff61, Key.Pause => 0xff13,
        Key.F1 or Key.F2 or Key.F3 or Key.F4 or Key.F5 or Key.F6 or
        Key.F7 or Key.F8 or Key.F9 or Key.F10 or Key.F11 or Key.F12 or
        Key.F13 or Key.F14 or Key.F15 or Key.F16 or Key.F17 or Key.F18 or
        Key.F19 or Key.F20 or Key.F21 or Key.F22 or Key.F23 or Key.F24
            => 0xffbeu + (uint)(key - Key.F1),
        _ => 0,
    };

    public static uint UnicodeKeysym(int codepoint) =>
        codepoint is > 0 and <= 0xff ? (uint)codepoint : 0x01000000u | (uint)codepoint;

    public static uint ScanCode(Key key)
    {
        uint scan = MapVirtualKey((uint)KeyInterop.VirtualKeyFromKey(key), 4);
        // QEMU 扩展键事件使用 XT 扫描码的扩展位。
        return (scan & 0xff00) != 0 ? (scan & 0xff) | 0x80u : scan;
    }

    public static uint CharacterKey(Key key)
    {
        uint virtualKey = (uint)KeyInterop.VirtualKeyFromKey(key);
        var state = new byte[256];
        if (!GetKeyboardState(state)) return 0;
        // Ctrl/Alt 不应把字符转换成控制字符；远端会收到独立修饰键事件。
        state[0x11] = state[0xa2] = state[0xa3] = 0;
        state[0x12] = state[0xa4] = state[0xa5] = 0;
        var text = new char[8];
        // 不改变系统死键状态，避免翻译器吞掉后续输入。
        int count = ToUnicodeEx(virtualKey, MapVirtualKey(virtualKey, 0), state,
            text, text.Length, 4, GetKeyboardLayout(0));
        if (count <= 0) return 0;
        return UnicodeKeysym(char.IsSurrogatePair(text[0], text[1])
            ? char.ConvertToUtf32(text[0], text[1]) : text[0]);
    }

    [DllImport("user32.dll")]
    private static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool GetKeyboardState([Out] byte[] state);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern int ToUnicodeEx(uint key, uint scan, byte[] state,
        [Out] char[] buffer, int capacity, uint flags, IntPtr layout);
    [DllImport("user32.dll")]
    private static extern IntPtr GetKeyboardLayout(uint threadId);
}
