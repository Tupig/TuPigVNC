namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 断线自动重连的退避策略。
///
/// 逐条对照原版 <c>src/vncviewer/vncviewer.cxx</c> 第 182-184、225-232 行：
/// 最多 3 次，基准 2 秒，指数增长（2/4/8…），封顶 30 秒，
/// 每次再乘 0.8~1.2 的随机抖动，避免服务器重启时所有客户端同时冲击。
///
/// 纯计算，不睡眠、不联网，方便独立测试。
/// </summary>
internal sealed class ReconnectPolicy
{
    public const int MaxAttempts = 3;
    private const int BaseDelaySeconds = 2;
    private const int MaxDelaySeconds = 30;

    private readonly Random _random;
    private int _attempts;

    /// <param name="seed">测试用固定种子；生产传 null 拿默认随机源。</param>
    public ReconnectPolicy(int? seed = null) =>
        _random = seed is { } value ? new Random(value) : new Random();

    /// <summary>已经用掉的次数。</summary>
    public int Attempts => _attempts;

    /// <summary>还能不能再试。</summary>
    public bool CanRetry => _attempts < MaxAttempts;

    /// <summary>
    /// 取下一次的等待时间。调用一次算一次尝试，返回 null 表示次数用尽。
    /// 结果落在 [delay×0.8, delay×1.2]。
    /// </summary>
    public TimeSpan? NextDelay()
    {
        if (!CanRetry) return null;
        _attempts++;

        int delay = BaseDelaySeconds << (_attempts - 1);
        if (delay > MaxDelaySeconds) delay = MaxDelaySeconds;

        // 原版是 0.8 + (rand() % 401) / 1000.0，即 [0.8, 1.2]。
        double jitter = 0.8 + _random.Next(0, 401) / 1000.0;
        return TimeSpan.FromSeconds(delay * jitter);
    }

    /// <summary>用户手动重连或连接成功后复位，下一次断线重新从 2 秒开始。</summary>
    public void Reset() => _attempts = 0;
}
