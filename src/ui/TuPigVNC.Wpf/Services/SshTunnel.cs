using System.Diagnostics;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;

namespace TuPigVNC.Wpf.Services;

/// <summary>
/// 经 SSH 网关建立到 VNC 服务器的本地隧道（<c>-via</c>）。
/// 逐条对照 <c>src/vncviewer/vncviewer.cxx</c> 的 <c>createTunnel</c> /
/// <c>waitForTunnel</c> / <c>findSshClient</c>：
/// <list type="bullet">
/// <item>优先用 Windows 10+ 自带的 OpenSSH（<c>%SystemRoot%\System32\OpenSSH\ssh.exe</c>），找不到退回 PATH。</item>
/// <item><c>-N</c> 不执行远程命令；<c>ExitOnForwardFailure</c> 端口绑定失败立即退出；
///       <c>BatchMode</c> 禁止交互式密码提示——认证必须靠密钥或 ssh-agent。</item>
/// <item>轮询本地端口直到就绪；ssh 进程提前退出立即失败，不用等满超时。</item>
/// </list>
/// 不允许 shell 拼接：参数走 <see cref="ProcessStartInfo.ArgumentList"/>，
/// 主机名和端口都是独立参数，不经过命令行字符串解析。
/// </summary>
internal sealed class SshTunnel : IDisposable
{
    /// <summary>隧道建立的等待上限，与原版一致。</summary>
    private static readonly TimeSpan StartupTimeout = TimeSpan.FromSeconds(10);

    private Process? _process;
    private int _localPort;

    /// <summary>隧道就绪后，应该连接的本机地址（<c>localhost:端口</c>）。</summary>
    public string LocalAddress => $"localhost::{_localPort}";

    /// <summary>
    /// 建立隧道。成功返回 null；失败返回中文错误说明。
    /// 调用方负责把错误显示出来，不能假装连上了。
    /// </summary>
    public string? Start(string gatewayHost, string remoteHost, int remotePort)
    {
        Dispose();

        string sshClient = FindSshClient();
        _localPort = FindFreeTcpPort();

        var info = new ProcessStartInfo(sshClient)
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
        };
        // ArgumentList 逐个传参，不拼命令行字符串——避免注入。
        info.ArgumentList.Add("-N");
        info.ArgumentList.Add("-o");
        info.ArgumentList.Add("ExitOnForwardFailure=yes");
        info.ArgumentList.Add("-o");
        info.ArgumentList.Add("BatchMode=yes");
        info.ArgumentList.Add("-L");
        info.ArgumentList.Add($"{_localPort}:{remoteHost}:{remotePort}");
        info.ArgumentList.Add(gatewayHost);

        try
        {
            _process = Process.Start(info);
        }
        catch (Exception ex)
        {
            return $"无法启动 SSH 客户端「{sshClient}」：{ex.Message}。" +
                   "请确认已安装 OpenSSH 客户端。";
        }

        if (_process is null)
            return $"无法启动 SSH 客户端「{sshClient}」。";

        // 把 ssh 的报错读出来，失败时能告诉用户真正的原因。
        // 回调在 ThreadPool 线程执行，和下面的读取有竞态，必须加锁；
        // 否则错误信息可能只写了一半就被读走。
        var stderr = new StringBuilder();
        _process.ErrorDataReceived += (_, e) =>
        {
            if (e.Data is null) return;
            lock (stderr) stderr.Append(e.Data).Append('\n');
        };
        _process.BeginErrorReadLine();

        if (WaitForPort(_process, _localPort, StartupTimeout, out bool exited))
        {
            return null;
        }

        string reason = exited
            ? "SSH 进程提前退出"
            : "SSH 隧道在 10 秒内未能建立";
        Dispose();
        string detail;
        lock (stderr) detail = stderr.ToString().Trim();
        return $"{reason}（本地端口 {_localPort}）。请确认网关「{gatewayHost}」可访问，" +
               "并先手动执行一次 ssh 登录网关以接受主机密钥。" +
               (detail.Length > 0 ? $"\nssh 输出：{detail}" : "");
    }

    /// <summary>轮询本地端口；ssh 进程提前退出时立即返回失败。</summary>
    private static bool WaitForPort(Process process, int port, TimeSpan timeout, out bool exited)
    {
        var deadline = Stopwatch.StartNew();
        exited = false;
        while (deadline.Elapsed < timeout)
        {
            if (IsLocalPortOpen(port))
                return true;
            if (process.HasExited)
            {
                exited = true;
                return false;
            }
            Thread.Sleep(100);
        }
        return false;
    }

    private static bool IsLocalPortOpen(int port)
    {
        using var client = new TcpClient();
        try
        {
            // 只连回环，不碰外部地址。
            // 不用 .Wait(timeout)：超时后连接任务还在跑，而 using 已经把 client 释放了，
            // 那是个「在已释放对象上完成」的定时炸弹。改成受取消令牌控制的异步。
            using var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(200));
            client.ConnectAsync(IPAddress.Loopback, port, cts.Token).GetAwaiter().GetResult();
            return client.Connected;
        }
        catch (Exception) { return false; }
    }

    /// <summary>优先用系统自带的 OpenSSH，找不到退回 PATH 里的 ssh.exe。</summary>
    private static string FindSshClient()
    {
        string systemDir = Environment.GetFolderPath(Environment.SpecialFolder.System);
        string bundled = Path.Combine(systemDir, "OpenSSH", "ssh.exe");
        return File.Exists(bundled) ? bundled : "ssh.exe";
    }

    /// <summary>让系统分配一个空闲的本地 TCP 端口。</summary>
    private static int FindFreeTcpPort()
    {
        var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        try { return ((IPEndPoint)listener.LocalEndpoint).Port; }
        finally { listener.Stop(); }
    }

    public void Dispose()
    {
        Process? process = _process;
        _process = null;
        if (process is null) return;
        try
        {
            if (!process.HasExited)
            {
                process.Kill(entireProcessTree: true);
                process.WaitForExit(2000);
            }
        }
        catch (Exception) { /* 进程已经退出，或没权限；不影响后续清理。 */ }
        finally { process.Dispose(); }
    }
}
