namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 「同时按左右键模拟中键」状态机。
///
/// 逐条移植 <c>src/vncviewer/EmulateMB.cxx</c> 的 11 态表：
/// 单按左键或右键会先等待 <see cref="TimeoutMs"/> 毫秒，期间不发送任何事件
/// （包括移动），超时后才把延迟的按下发出去；两键在窗口内都按下则改发中键。
/// 这样既保留了原版「快速左右同按 = 中键」的手感，又不会在普通单击时
/// 平白多出一次按下/抬起。
///
/// 这个类不做任何原生调用，只把结果交给 <see cref="SendPointer"/> 回调，
/// 因此可以独立测试。
/// </summary>
internal sealed class MiddleButtonEmulator
{
    /// <summary>原版 <c>timer.start(50)</c>。</summary>
    public const int TimeoutMs = 50;

    private const int Ground = 0;
    private const int DelayedLeft = 1;
    private const int DelayedRight = 2;
    private const int PressedMiddle = 3;
    private const int PressedLeft = 4;
    private const int PressedRight = 5;
    private const int ReleasedLeft = 6;
    private const int ReleasedRight = 7;
    private const int RepressedLeft = 8;
    private const int RepressedRight = 9;
    private const int PressedBoth = 10;

    /// <summary>
    /// 每行 5 个输入（无按键 / 左 / 右 / 左右同按 / 超时），
    /// 每个输入是 [action1, action2, 新状态]。
    /// action &gt; 0 按下该位，&lt; 0 抬起该位（位号 = |action| - 1）。
    /// 负数状态表示这个输入没有超时动作。
    /// </summary>
    private static readonly sbyte[][][] StateTable =
    [
        // 0 ground
        [
            [0, 0, Ground], [0, 0, DelayedLeft], [0, 0, DelayedRight],
            [2, 0, PressedMiddle], [0, 0, -1],
        ],
        // 1 delayed left
        [
            [1, -1, Ground], [0, 0, DelayedLeft], [1, -1, DelayedRight],
            [2, 0, PressedMiddle], [1, 0, PressedLeft],
        ],
        // 2 delayed right
        [
            [3, -3, Ground], [3, -3, DelayedLeft], [0, 0, DelayedRight],
            [2, 0, PressedMiddle], [3, 0, PressedRight],
        ],
        // 3 pressed middle
        [
            [-2, 0, Ground], [0, 0, ReleasedRight], [0, 0, ReleasedLeft],
            [0, 0, PressedMiddle], [0, 0, -1],
        ],
        // 4 pressed left
        [
            [-1, 0, Ground], [0, 0, PressedLeft], [-1, 0, DelayedRight],
            [3, 0, PressedBoth], [0, 0, -1],
        ],
        // 5 pressed right
        [
            [-3, 0, Ground], [-3, 0, DelayedLeft], [0, 0, PressedRight],
            [1, 0, PressedBoth], [0, 0, -1],
        ],
        // 6 released left
        [
            [-2, 0, Ground], [-2, 0, DelayedLeft], [0, 0, ReleasedLeft],
            [1, 0, RepressedLeft], [0, 0, -1],
        ],
        // 7 released right
        [
            [-2, 0, Ground], [0, 0, ReleasedRight], [-2, 0, DelayedRight],
            [3, 0, RepressedRight], [0, 0, -1],
        ],
        // 8 repressed left
        [
            [-2, -1, Ground], [-2, 0, PressedLeft], [-1, 0, ReleasedLeft],
            [0, 0, RepressedLeft], [0, 0, -1],
        ],
        // 9 repressed right
        [
            [-2, -3, Ground], [-3, 0, ReleasedRight], [-2, 0, PressedRight],
            [0, 0, RepressedRight], [0, 0, -1],
        ],
        // 10 pressed both
        [
            [-1, -3, Ground], [-3, 0, PressedLeft], [-1, 0, PressedRight],
            [0, 0, PressedBoth], [0, 0, -1],
        ],
    ];

    private readonly Action<int, int, ushort> _sendPointer;
    private readonly Action<int> _startTimer;
    private readonly Action _stopTimer;
    private int _state = Ground;
    private ushort _emulatedMask;
    private (int X, int Y) _origPos;
    private (int X, int Y) _lastPos;
    private ushort _lastMask;

    /// <param name="sendPointer">发送指针事件：x、y、按钮掩码。</param>
    /// <param name="startTimer">启动 50ms 延迟；到点调用 <see cref="HandleTimeout"/>。</param>
    /// <param name="stopTimer">取消尚未到点的延迟。</param>
    public MiddleButtonEmulator(Action<int, int, ushort> sendPointer,
        Action<int> startTimer, Action stopTimer)
    {
        _sendPointer = sendPointer;
        _startTimer = startTimer;
        _stopTimer = stopTimer;
    }

    /// <summary>状态机是否正在延迟或按住模拟出的键；为 true 时应继续捕获鼠标。</summary>
    public bool HasPendingButtons => _state != Ground || _emulatedMask != 0;

    /// <summary>当前真正发出去的按钮掩码（含模拟位），用于鼠标移动时保持一致。</summary>
    public ushort EmulatedMask => _emulatedMask;

    /// <summary>
    /// 鼠标移动。延迟期间照常发位置（掩码里不含尚未确认的左右键），
    /// 这样光标不会在两次按键之间停住；原版也是这么做的。
    /// </summary>
    public void Move(int x, int y, ushort buttonMask)
    {
        _lastPos = (x, y);
        _lastMask = buttonMask;
        _sendPointer(x, y, (ushort)((buttonMask & ~0x5) | _emulatedMask));
    }

    /// <summary>按键状态变化。buttonMask 是变化后的完整掩码，不是增量。</summary>
    public void Update(int x, int y, ushort buttonMask)
    {
        _lastPos = (x, y);
        _lastMask = buttonMask;

        int btstate = 0;
        if ((buttonMask & 0x1) != 0) btstate |= 0x1;
        if ((buttonMask & 0x4) != 0) btstate |= 0x2;

        var row = StateTable[_state];
        int action1 = row[btstate][0];
        int action2 = row[btstate][1];

        // 离开带超时的状态时，延迟的按下必须用最初按下的位置，
        // 这样拖拽才不会从光标当前位置开始。
        bool hasTimeout = row[4][2] >= 0;
        if (action1 != 0)
            SendAction(hasTimeout && action1 > 0 ? _origPos : (x, y), action1);
        if (action2 != 0)
            SendAction(hasTimeout && action2 > 0 ? _origPos : (x, y), action2);

        // 没有动作、也没有等待中的延迟时，仍要发一次移动，
        // 否则光标在两次按键之间会停住。
        if (action1 == 0 && action2 == 0 && !_timerRunning)
            _sendPointer(x, y, (ushort)((buttonMask & ~0x5) | _emulatedMask));

        int lastState = _state;
        _state = row[btstate][2];
        if (lastState != _state)
        {
            StopTimer();
            if (StateTable[_state][4][2] >= 0)
            {
                _origPos = (x, y);
                _timerRunning = true;
                _startTimer(TimeoutMs);
            }
        }
    }

    /// <summary>延迟到点。超时状态下没有超时动作是编程错误，这里直接忽略。</summary>
    public void HandleTimeout()
    {
        if (!_timerRunning) return;
        _timerRunning = false;

        var row = StateTable[_state];
        if (row[4][2] < 0) return;      // 不该有超时动作

        int action1 = row[4][0];
        int action2 = row[4][1];
        if (action1 != 0) SendAction(_origPos, action1);
        if (action2 != 0) SendAction(_origPos, action2);

        // 等待期间被压制的移动事件，在这里补一次最新位置。
        if (_origPos != _lastPos)
            _sendPointer(_lastPos.X, _lastPos.Y, (ushort)((_lastMask & ~0x5) | _emulatedMask));

        _state = row[4][2];
    }

    /// <summary>断线或失焦时复位，避免下次会话带着上一次的模拟状态。</summary>
    public void Reset()
    {
        StopTimer();
        _state = Ground;
        _emulatedMask = 0;
        _origPos = _lastPos = (0, 0);
        _lastMask = 0;
    }

    private void SendAction((int X, int Y) pos, int action)
    {
        if (action < 0) _emulatedMask &= (ushort)~(1 << ((-action) - 1));
        else _emulatedMask |= (ushort)(1 << (action - 1));

        // 左右键的真实状态由状态机接管：先清掉，再叠加模拟位。
        _sendPointer(pos.X, pos.Y, (ushort)((_lastMask & ~0x5) | _emulatedMask));
    }

    private bool _timerRunning;

    private void StopTimer()
    {
        if (!_timerRunning) return;
        _timerRunning = false;
        _stopTimer();
    }
}
