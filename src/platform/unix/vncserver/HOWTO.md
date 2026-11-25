# 近期 TuPigVNC 版本的变化
以往的 TuPigVNC 版本提供名为 `vncserver` 的包装脚本。用户可以手动运行该脚本以启动 *Xvnc* 进程，用法相当简单，只需运行：
```
$ vncserver :x [vncserver 选项] [Xvnc 选项]
```
如此即可。该方式在部分情况下运行良好，但远非全部情况。当用户希望将其与 *systemd* 结合使用时便会出现问题。因此，为符合 *SELinux* 与 *systemd* 的规则，实现方式必须更改。

# 如何启动 TuPigVNC 服务器
## 添加用户映射
通过此配置，可将用户映射到特定端口。映射应在 `vncserver.users` 配置文件中完成。操作应当十分直观：打开该文件后即可看到若干示例，其基本格式为：
```
:x=user
```
例如，可以这样配置：
```
:1=test
:2=vncuser
```

## 配置 Xvnc 选项
要配置 Xvnc 参数，请进入进行用户映射的同一目录，并打开 `vncserver-config-defaults` 配置文件。该文件用于默认的 Xvnc 配置，将应用于每位用户，除非存在以下任一情况：
* 用户在 `$XDG_CONFIG_HOME/tigervnc/config` 或 `$HOME/.config/tigervnc/config` 中拥有自己的配置。
* `vncserver-config-mandatory` 配置文件中以不同值配置了相同选项。该文件会替代默认配置，且优先级高于各用户自身的配置。系统管理员若希望强制指定特定的 *Xvnc* 选项，可使用此项。

配置文件的格式同样十分简单，其形式为：
```
option=value
option
```
例如：
```
session=gnome
securitytypes=vncauth,tlsvnc
geometry=2000x1200
localhost
alwaysshared
```
更多详情请参阅 Xvnc(1) 手册页。

### 注意：
建议设置用于指定所要启动会话的选项。例如，若要启动 GNOME 桌面，则需使用：
```
session=gnome
```
该值应与 `/usr/share/xsessions` 目录中的会话桌面文件名一致。若未指定会话，TuPigVNC 将尝试使用其找到的第一个会话，该会话可能正常工作，也可能无法正常工作。

## 设置 VNC 密码
要启动 TuPigVNC 服务器，需为每位用户设置密码。要创建密码，只需运行：
```
$ vncpasswd
```
请以将要运行服务器的用户身份运行该命令。

### 注意：
若你以往曾以某用户使用 TuPigVNC 并已创建密码，则必须确保（如曾使用）由 `vncpasswd` 创建的旧版 `$HOME/.vnc` 文件夹具有正确的 *SELinux* 上下文。你可以删除该文件夹并再次创建密码以重新生成，或者运行：
```
$ restorecon -RFv /home/<USER>/.vnc
```

## 启动 TuPigVNC 服务器
最后，可使用 systemd 服务启动服务器。只需运行：
```
$ systemctl start vncserver@:x
```
以 root 用户运行，或：
```
$ sudo systemctl start vncserver@:x
```
若该用户有权限运行 `sudo`，则以普通用户身份运行。请记得将 `:x` 替换为你在用户映射文件中配置的实际编号，例如：
```
$ systemctl start vncserver@:1
```
如此即为用户 `test` 启动了一个带 GNOME 会话的 TuPigVNC 服务器。

若希望服务器在开机时自动启动，可运行：
```
$ systemctl enable vncserver@:1
```

### 注意：
若你以往使用 TuPigVNC 并习惯通过 *systemd* 启动，则可能需要删除位于 `/etc/systemd/system/vncserver@.service` 的旧 *systemd* 配置文件，以免其优先级高于最新 TuPigVNC 提供的 systemd 服务文件。

# 限制
无法为已登录图形会话的用户启动 TuPigVNC 服务器。请避免以 `root` 用户运行服务器，因为这并不安全。虽然以 `root` 运行服务器通常可以工作，但不建议这样做，且可能存在部分功能无法正常工作的情况。
