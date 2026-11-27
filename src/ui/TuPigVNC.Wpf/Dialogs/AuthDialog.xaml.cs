using System.Windows;
using System.Windows.Input;
using System.Windows.Media;

namespace TuPigVNC.Wpf.Dialogs;

/// <summary>
/// VNC 认证对话框（对应旧版 AuthDialog）：
/// 顶部安全状态横幅 + 品牌紫问号图标 + 用户名/密码 + 记住密码 + CapsLock 提示。
/// </summary>
public partial class AuthDialog : Window
{
    public string Username { get; private set; } = "";
    public string Password { get; private set; } = "";
    public bool KeepPassword { get; private set; }

    public AuthDialog(bool secure, bool needsUser, bool needsPassword,
        bool showKeepPassword = false)
    {
        InitializeComponent();

        if (secure)
        {
            SecureBanner.Background = (Brush)FindResource("OkSoftBrush");
            SecureText.Foreground = (Brush)FindResource("OkBrush");
            SecureIcon.Foreground = (Brush)FindResource("OkBrush");
            SecureIcon.Text = ""; // 锁
            SecureText.Text = "此连接是安全的";
        }
        else
        {
            SecureBanner.Background = (Brush)FindResource("ErrSoftBrush");
            SecureText.Foreground = (Brush)FindResource("ErrBrush");
            SecureIcon.Foreground = (Brush)FindResource("ErrBrush");
            SecureIcon.Text = ""; // 开锁
            SecureText.Text = "此连接不安全";
        }

        UsernamePanel.Visibility = needsUser ? Visibility.Visible : Visibility.Collapsed;
        PasswordPanel.Visibility = needsPassword ? Visibility.Visible : Visibility.Collapsed;
        KeepPasswordCheck.Visibility = showKeepPassword && needsPassword
            ? Visibility.Visible
            : Visibility.Collapsed;

        PreviewKeyDown += (_, _) => UpdateCapsWarning();
        PreviewKeyUp += (_, _) => UpdateCapsWarning();

        Loaded += (_, _) =>
        {
            if (needsUser)
                UsernameBox.Focus();
            else if (needsPassword)
                PasswordBox.Focus();

            UpdateCapsWarning();
        };
    }

    private void Window_SourceInitialized(object? sender, EventArgs e)
    {
        App.ApplyDarkTitleBar(this);
    }

    private void UpdateCapsWarning()
    {
        if (CapsWarn is null)
            return;

        CapsWarn.Visibility = Keyboard.IsKeyToggled(Key.CapsLock)
            ? Visibility.Visible
            : Visibility.Collapsed;
    }

    private void Ok_Click(object sender, RoutedEventArgs e)
    {
        Username = UsernameBox.Text;
        Password = PasswordBox.Password;
        KeepPassword = KeepPasswordCheck.IsChecked == true;
        DialogResult = true;
    }
}
