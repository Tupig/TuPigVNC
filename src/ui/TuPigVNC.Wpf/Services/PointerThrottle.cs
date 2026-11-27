using System.Windows.Threading;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 鼠标事件限速。逐条对照 <c>src/vncviewer/Viewport::handlePointerEvent</c>：
/// <list type="bullet">
/// <item>按键掩码变化（按下 / 松开）**立即**发送，不能延迟——否则点击会感觉到卡。</item>
/// <item>纯移动按 <c>PointerEventInterval</c>（默认 17ms）合并：这段时间内
///       只记住最后一次位置，到点再发，避免高回报率鼠标把网络刷爆。</item>
/// </list>
/// 高延迟网络下这个限速能显著减少带宽占用，本地局域网没有副作用。
/// </summary>
internal sealed class PointerThrottle : IDisposable
{
    private readonly Action<int, int, ushort> _send;
    private readonly DispatcherTimer _timer;
    private ushort _lastButtonMask;
    private bool _hasPending;
    private (int X, int Y, ushort Mask) _pending;
    private bool _disposed;

    /// <param name="send">真正发出去的回调。</param>
    /// <param name="intervalMs">合并间隔；0 表示不限速（每个事件都发）。</param>
    public PointerThrottle(Action<int, int, ushort> send, int intervalMs, Dispatcher dispatcher)
    {
        _send = send;
        // 用局部变量避免「可能为空」的警告：定时器回调只在自己活着时触发。
        DispatcherTimer? timer = null;
        timer = new DispatcherTimer(
            TimeSpan.FromMilliseconds(Math.Max(1, intervalMs)),
            DispatcherPriority.Input,
            (_, _) =>
            {
                timer?.Stop();
                if (!_hasPending) return;
                _hasPending = false;
                _send(_pending.X, _pending.Y, _pending.Mask);
            },
            dispatcher);
        _timer = timer;
    }

    /// <summary>
    /// 发送一次指针事件。按键掩码变化时立即发送，否则合并到间隔末尾。
    /// </summary>
    public void Move(int x, int y, ushort buttonMask)
    {
        if (_disposed) return;

        // 按下/松开必须立刻生效，不能等定时器。
        if (buttonMask != _lastButtonMask)
        {
            _hasPending = false;
            _timer.Stop();
            _lastButtonMask = buttonMask;
            _send(x, y, buttonMask);
            return;
        }

        // 纯移动：记住最后一次位置，等间隔到点再发。
        _pending = (x, y, buttonMask);
        _hasPending = true;
        if (!_timer.IsEnabled) _timer.Start();
    }

    /// <summary>把还没发出去的位置立刻发掉（比如松手时）。</summary>
    public void Flush()
    {
        if (_disposed || !_hasPending) return;
        _hasPending = false;
        _timer.Stop();
        _send(_pending.X, _pending.Y, _pending.Mask);
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        _hasPending = false;
        _timer.Stop();
    }
}
