namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 会话快捷键的修饰键状态机。逐条移植自 <c>src/vncviewer/ShortcutHandler.cxx</c>。
///
/// 为什么需要它：全屏时用户按 <c>Ctrl+Alt+Enter</c> 想切回窗口，但如果直接把
/// 这三个键发给远端，远端会收到 Ctrl+Alt+Enter。状态机负责识别出「这是一组
/// 快捷键组合」并把它们从远程输入里吃掉，本地执行对应动作。
///
/// 状态流转（对照原版 6 个状态）：
/// <code>
///   Idle ──修饰键按下──> Arming ──全部修饰键按下──> Armed
///                          │                        │
///                          └──不相关键按下──> Wedged └──普通键按下──> Firing
///                                              (彻底放弃)              (发快捷键)
/// </code>
/// 关键语义：<c>KeyIgnore</c> 表示这个键已被吃掉、绝不能再发给远端；
/// <c>KeyShortcut</c> 表示识别出了快捷键、要执行本地动作；
/// <c>KeyUnarm</c> 表示修饰键全部松开、要解除键盘抓取。
/// </summary>
internal sealed class ShortcutHandler
{
    [Flags]
    public enum Modifier
    {
        None = 0,
        Control = 1 << 0,
        Shift = 1 << 1,
        Alt = 1 << 2,
        Super = 1 << 3,
    }

    public enum KeyAction
    {
        Normal,
        Unarm,
        Shortcut,
        Ignore,
    }

    private enum State
    {
        Idle,
        Arming,
        Armed,
        Rearming,
        Firing,
        Wedged,
    }

    /// <summary>按下的物理键 → 它的 KeySym。必须用物理键码做键，不能用 KeySym：
    /// 左右 Ctrl 的 KeySym 不同，但都是同一个修饰键位。</summary>
    private readonly Dictionary<int, uint> _pressedKeys = new();
    private readonly HashSet<int> _firedKeys = new();
    private State _state = State.Idle;
    private Modifier _modifierMask;

    /// <summary>设置触发修饰键组合。传空表示不启用快捷键，所有键直接透传。</summary>
    public void SetModifiers(Modifier mask)
    {
        _modifierMask = mask;
        Reset();
    }

    /// <summary>
    /// 处理一次按键按下。
    /// <paramref name="keyCode"/> 是物理键码（Windows 的扫描码），
    /// <paramref name="keySym"/> 是解析后的 X11 KeySym。
    /// </summary>
    public KeyAction HandleKeyPress(int keyCode, uint keySym)
    {
        _pressedKeys[keyCode] = keySym;

        if (_modifierMask == Modifier.None)
            return KeyAction.Normal;

        Modifier modifier = KeySymToModifier(keySym);

        Modifier pressedMask = Modifier.None;
        foreach (uint sym in _pressedKeys.Values)
            pressedMask |= KeySymToModifier(sym);

        switch (_state)
        {
            case State.Idle:
            case State.Arming:
            case State.Rearming:
                // 原版是三个**独立** if（不是 if/else），必须逐条照搬：
                // 「组合凑齐」和「新键属于组合」可以同时成立，写法不同
                // 会让「所有修饰键同时按下」的边界走到不同状态。
                if (pressedMask == _modifierMask)
                {
                    // 所有触发用的修饰键都已按下：进入待命，下一个普通键就是快捷键。
                    _state = State.Armed;
                }
                if (modifier != Modifier.None && (modifier & _modifierMask) == modifier)
                {
                    // 新按键属于触发组合的一部分，继续等其余修饰键。
                    if (_state == State.Idle) _state = State.Arming;
                }
                else
                {
                    // 新按键是别的东西：这组按键不是快捷键，彻底放弃。
                    _state = State.Wedged;
                }
                return KeyAction.Normal;

            case State.Armed:
                if (modifier != Modifier.None && (modifier & _modifierMask) == modifier)
                {
                    // 又按了一个同组修饰键（比如 Ctrl 按了两次），继续等。
                    return KeyAction.Normal;
                }
                if (modifier != Modifier.None)
                {
                    // 按的是另一种修饰键，组合已不成立。
                    _state = State.Wedged;
                    return KeyAction.Normal;
                }
                // 普通键：这就是快捷键，记住它以便松手时识别。
                _state = State.Firing;
                _firedKeys.Add(keyCode);
                return KeyAction.Shortcut;

            case State.Firing:
                // 快捷键已经触发，之后的按键全部吃掉（修饰键可能还在按着）。
                if (modifier != Modifier.None) return KeyAction.Ignore;
                _firedKeys.Add(keyCode);
                return KeyAction.Shortcut;

            default:
                return KeyAction.Normal;
        }
    }

    /// <summary>处理一次按键松开。</summary>
    public KeyAction HandleKeyRelease(int keyCode)
    {
        bool firedKey = _firedKeys.Contains(keyCode);
        _firedKeys.Remove(keyCode);
        _pressedKeys.Remove(keyCode);

        Modifier pressedMask = Modifier.None;
        foreach (uint sym in _pressedKeys.Values)
            pressedMask |= KeySymToModifier(sym);

        KeyAction action;
        switch (_state)
        {
            case State.Arming:
                action = KeyAction.Normal;
                break;

            case State.Armed:
                if (_pressedKeys.Count == 0)
                    action = KeyAction.Unarm;       // 修饰键全松开：解除抓取
                else if (pressedMask == _modifierMask)
                    action = KeyAction.Normal;      // 还按着完整组合
                else
                {
                    action = KeyAction.Normal;
                    _state = State.Rearming;        // 组合拆了一部分，重新等
                }
                break;

            case State.Rearming:
                action = _pressedKeys.Count == 0 ? KeyAction.Unarm : KeyAction.Normal;
                break;

            case State.Firing:
                // 按下时识别为快捷键的键，松开也要吃掉，不能漏给远端。
                action = firedKey ? KeyAction.Shortcut : KeyAction.Ignore;
                break;

            default:
                action = KeyAction.Normal;
                break;
        }

        if (_pressedKeys.Count == 0) _state = State.Idle;
        return action;
    }

    public void Reset()
    {
        _state = State.Idle;
        _firedKeys.Clear();
        _pressedKeys.Clear();
    }

    /// <summary>KeySym → 修饰键位。对应原版 keySymToModifier。</summary>
    private static Modifier KeySymToModifier(uint keySym) => keySym switch
    {
        0xffe3 or 0xffe4 => Modifier.Control,   // XK_Control_L / _R
        0xffe1 or 0xffe2 => Modifier.Shift,     // XK_Shift_L / _R
        0xffe9 or 0xffea => Modifier.Alt,       // XK_Alt_L / _R
        0xffeb or 0xffec => Modifier.Super,     // XK_Super_L / _R
        0xffed or 0xffee => Modifier.Super,     // XK_Hyper_L / _R
        _ => Modifier.None,
    };

    /// <summary>
    /// 解析配置里的修饰键名字（大小写不敏感）。逐条对照原版 parseModifier。
    /// 认不出的名字返回 None，调用方应视为「未配置」而不是报错。
    ///
    /// 注意：原版只认 <c>"Ctrl"</c>，**不认 <c>"Control"</c> 全称**
    /// （<c>strcasecmp</c> 是整串比较，长度不同就不相等）。这里必须保持一致——
    /// 否则同一份配置在 FLTK 版和 WPF 版行为不同：WPF 触发、FLTK 不触发。
    /// </summary>
    public static Modifier ParseModifier(string? key)
    {
        if (string.IsNullOrWhiteSpace(key)) return Modifier.None;
        return key.Trim().ToUpperInvariant() switch
        {
            "CTRL" => Modifier.Control,
            "SHIFT" => Modifier.Shift,
            "ALT" or "OPTION" => Modifier.Alt,
            "WIN" or "SUPER" or "CMD" => Modifier.Super,
            _ => Modifier.None,
        };
    }

    /// <summary>把修饰键位翻回人类可读的名字（用于提示文字）。</summary>
    public static string ModifierString(Modifier mask)
    {
        var parts = new List<string>();
        if ((mask & Modifier.Control) != 0) parts.Add("Ctrl");
        if ((mask & Modifier.Shift) != 0) parts.Add("Shift");
        if ((mask & Modifier.Alt) != 0) parts.Add("Alt");
        if ((mask & Modifier.Super) != 0) parts.Add("Win");
        return string.Join("+", parts);
    }

    /// <summary>
    /// 从配置的 "Ctrl,Shift,Alt,Win" 这种字符串解析出组合掩码。
    /// 逗号分隔，无法识别的项直接忽略。
    /// </summary>
    public static Modifier ParseModifiers(string? value)
    {
        if (string.IsNullOrWhiteSpace(value)) return Modifier.None;
        Modifier mask = Modifier.None;
        foreach (string part in value.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            mask |= ParseModifier(part);
        return mask;
    }
}
