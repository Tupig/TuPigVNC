/*
 * TuPigVNC · 原生核心 C ABI（阶段 1 完整版）
 *
 * C# WPF 前端通过 P/Invoke 调用本 DLL。
 *
 * 设计原则：
 *   1. 只暴露 C 类型（const char* / uint8_t* / int / void*），STL 类型绝不出 DLL 边界
 *      —— 避免不同 CRT 版本的堆/字符串 ABI 冲突
 *   2. 所有异常在 DLL 内部 catch，转成错误码（0 = 成功，负数 = 错误）
 *   3. 内存所有权：
 *      - DLL 创建的对象（VncSession*）必须调 vnc_destroy 释放
 *      - 像素缓冲由 C# 分配（Marshal.AllocHGlobal），DLL 只写不改大小
 *      - 回调里传出的 const char* / const uint8_t* 只在回调期间有效，需要保留请自行拷贝
 *   4. 线程模型：
 *      - vnc_start_loop 起一个内部线程跑 select() + processMsg()
 *      - 所有回调都在这个内部线程上触发
 *      - C# 侧必须 Dispatcher.Invoke/BeginInvoke 切回 UI 线程
 *   5. 像素格式固定 32bpp BGRX（小端下内存布局 B,G,R,X），stride = width * 4
 *
 * 错误码约定（vnc_connect 返回值）：
 *    0  = 成功
 *   -1  = 参数非法
 *   -2  = 网络连接失败（DNS / 拒绝 / 超时）
 *   -3  = 协议握手失败
 *   -4  = 认证失败
 *   -5  = 内部异常（详见 err 缓冲）
 */

#ifndef TUPIGVNC_VNCABI_H_
#define TUPIGVNC_VNCABI_H_

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#  ifdef TUPIGVNC_CORE_EXPORTS
#    define VNC_API __declspec(dllexport)
#  else
#    define VNC_API __declspec(dllimport)
#  endif
#else
#  define VNC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * 不透明句柄
 * ====================================================================== */

typedef struct VncSession VncSession;

/* ========================================================================
 * 回调表
 *
 * 所有函数指针都允许为 NULL（表示前端不关心该事件）。
 * ud 是创建时传入的用户数据指针，原样回传。
 * ====================================================================== */

typedef struct VncCallbacks {
  void* ud;

  /* ---- 连接生命周期 ---- */

  /* 服务器初始化完成（连接建立的最后一步）。
     name      : 桌面名称（UTF-8，可能为空字符串）
     width/height: 初始帧缓冲尺寸
     bpp/depth : 像素位数 / 颜色深度（目前固定 32/24）
     rs/gs/bs  : 每个颜色分量的高位偏移（目前固定 16/8/0） */
  void (*on_server_init)(void* ud, int width, int height, const char* name,
                         int bpp, int depth, int rs, int gs, int bs);

  /* 一帧更新完成（framebufferUpdateEnd）。
     前端应在此把像素缓冲刷到屏幕上（x,y,w,h 是本次更新的脏矩形，
     目前实现里总是整个帧缓冲，后续可优化成真正的脏矩形）。 */
  void (*on_frame_update)(void* ud, int x, int y, int w, int h);

  /* 连接断开或发生致命错误。
     msg   : UTF-8 错误描述
     fatal : 非 0 表示连接已不可用，前端应关闭会话窗口 */
  void (*on_error)(void* ud, const char* msg, int fatal);

  /* ---- 需要前端同步响应的回调（返回 0 = 取消/失败，非 0 = 成功） ---- */

  /* 需要用户名/密码。
     user/userLen, pass/passLen : 输出缓冲（UTF-8，含结尾 \0）。
     前端应在 UI 线程弹对话框，把结果写进缓冲后返回。
     本回调在内部网络线程上调用，前端必须自行切线程并等结果。 */
  int (*on_credentials)(void* ud, const char* host, int secure,
                        char* user, size_t userLen,
                        char* pass, size_t passLen);

  /* 证书校验（TLS）。status 见 rfb::Security 的 CertStatus 枚举。 */
  int (*on_verify_cert)(void* ud, unsigned int status,
                        const uint8_t* cert, size_t len);

  /* SSH 主机密钥校验。fp 是 UTF-8 指纹字符串。 */
  int (*on_verify_hostkey)(void* ud, const uint8_t* key, size_t len,
                           const char* fp);

  /* ---- 状态通知 ---- */

  /* 服务器发来的剪贴板文本（UTF-8）。仅在「获得焦点」时才会真正同步。
     两条协议路径都会走到这里：旧版 ServerCutText，以及扩展剪贴板的
     ClipboardProvide。 */
  void (*on_clipboard)(void* ud, const char* text);

  /* 服务器通知「远端剪贴板有变化」。available=0 表示远端剪贴板被清空。
     托管侧收到 available=1 后应调用 vnc_request_clipboard() 拉取内容；
     这正是原版 Viewport::handleClipboardAnnounce 的语义。 */
  void (*on_clipboard_announce)(void* ud, int available);

  /* 服务器请求我们发送本地剪贴板（扩展剪贴板的 ClipboardRequest 或
     announceClipboard 的隐式请求）。托管侧应读取本机剪贴板后调用
     vnc_send_clipboard_data()。 */
  void (*on_clipboard_request)(void* ud);

  /* 光标形状改变。rgba 是 width*height*4 字节的预乘 RGBA 位图。 */
  void (*on_cursor)(void* ud, int width, int height, int hotX, int hotY,
                    const uint8_t* rgba);

  /* 光标位置改变（服务器端光标本地渲染的场合）。 */
  void (*on_cursor_pos)(void* ud, int x, int y);

  /* 桌面名称改变。 */
  void (*on_name)(void* ud, const char* name);

  /* 键盘 LED 状态（bit0=CapsLock, bit1=NumLock, bit2=ScrollLock）。 */
  void (*on_led)(void* ud, unsigned int state);

  /* 远程帧缓冲尺寸改变（服务器分辨率变化 / 多显示器切换）。 */
  void (*on_resize)(void* ud, int width, int height);

  /* 响铃。 */
  void (*on_bell)(void* ud);

} VncCallbacks;

/* ========================================================================
 * 会话生命周期
 * ====================================================================== */

/* 创建会话。cb 会被拷贝一份，调用后 cb 指针即可释放。
   失败返回 NULL。 */
VNC_API VncSession* vnc_create(const VncCallbacks* cb);

/* 建立到 host:port 的连接（同步，会阻塞）。
   成功后会话处于「已连接但未开始事件循环」状态。
   err/errLen : 失败时写入 UTF-8 错误描述（可为 NULL）。
   返回 0 成功，负数见文件头错误码约定。 */
VNC_API int vnc_connect(VncSession* s, const char* host, int port,
                        char* err, size_t errLen);

/* 启动内部事件循环线程（非阻塞，立即返回）。
   所有回调都在该线程上触发。重复调用无副作用。 */
VNC_API void vnc_start_loop(VncSession* s);

/* 请求停止事件循环并等待线程退出（会阻塞）。
   实现会先 shutdown socket，让阻塞中的 recv/send 立刻返回，正常情况下
   会在几十毫秒内退出；只有对端长时间占着发送缓冲这类极端情况才会更久。
   在事件循环线程上调用时只置停止标志、立即返回，不会 join 自己。 */
VNC_API void vnc_stop(VncSession* s);

/* 销毁会话。内部会自动请求停止。
   若在事件循环线程（例如回调里）调用，不会当场删除——那会析构 joinable
   的线程并触发 std::terminate。此时改为「标记待销毁」，由事件循环线程
   退出前自行释放。调用方在回调返回后必须视为句柄已失效。 */
VNC_API void vnc_destroy(VncSession* s);

/* ========================================================================
 * 帧缓冲
 * ====================================================================== */

/* 设置像素缓冲。data 由 C# 分配（Marshal.AllocHGlobal），
   必须至少 width*height*4 字节。
   DLL 只写不读，且不会释放 data。
   在 on_server_init 或 on_resize 之后调用。 */
VNC_API void vnc_set_framebuffer(VncSession* s, uint8_t* data,
                                 int width, int height, int stride);

/* ========================================================================
 * 输入
 * ====================================================================== */

/* 按下键。systemKeyCode 是 Windows 虚拟键码（VK_*），
   keyCode 是扫描码，keySym 是 X11 keysym（DLL 内部会自行处理映射）。 */
VNC_API void vnc_send_key(VncSession* s, int systemKeyCode,
                          uint32_t keyCode, uint32_t keySym);

/* 松开键。 */
VNC_API void vnc_send_key_release(VncSession* s, int systemKeyCode);

/* 鼠标事件。pos 是远程帧缓冲坐标；buttonmask 是 X11 风格位掩码
   （bit0=左, bit1=中, bit2=右, bit3/4=滚轮上/下）。 */
VNC_API void vnc_send_pointer(VncSession* s, int x, int y,
                              uint16_t buttonmask);

/* 请求服务器重发整个帧缓冲。 */
VNC_API void vnc_refresh(VncSession* s);

/* ========================================================================
 * 会话选项
 * ====================================================================== */

/* 是否共享会话（不断开其他查看器）。
   必须在 vnc_connect 之前调用：这个标志在协议初始化时随 ClientInit 一起发出，
   连上之后再改对本次会话无效。默认值为不共享。 */
VNC_API void vnc_set_shared(VncSession* s, int shared);

/* 监听模式：绑定本地端口，阻塞等待服务器反向连入（原版 -listen）。
   acceptTimeoutMs 为 0 表示一直等。成功返回 0；参数错误 -1；
   超时 / 取消 / 建连失败 -2；未预期的异常 -5。
   注意：这个函数会阻塞调用线程，必须放到后台线程。 */
VNC_API int vnc_listen(VncSession* s, int port, int acceptTimeoutMs,
                       char* err, size_t errLen);

/* ========================================================================
 * 安全类型
 * ====================================================================== */

/* 设置协商用的安全类型列表（逗号分隔的 TigerVNC 名字）。
   任何一个名字认不出来就整份拒绝（返回 -1），绝不静默丢弃——
   用户以为关掉了加密、实际还在用，是安全问题。 */
VNC_API int vnc_set_security_types(VncSession* s, const char* types);

/* 本核心实际编译进去的安全类型（逗号分隔）。写进调用方缓冲，
   返回写入的字节数（不含结尾 '\0'）；缓冲不够时返回 -1。
   用来把界面上不支持的选项置灰。 */
VNC_API int vnc_security_types(char* buffer, size_t capacity);

/* X.509 证书校验用的 CA 证书 / CRL 文件路径（PEM）。 */
VNC_API void vnc_set_x509_ca(VncSession* s, const char* path);
VNC_API void vnc_set_x509_crl(VncSession* s, const char* path);

/* ========================================================================
 * 音频
 * ====================================================================== */

/* 启用 / 关闭音频（QEMU 音频扩展）。必须在 vnc_connect 之前调用：
   它决定 SetEncodings 里要不要带音频伪编码。 */
VNC_API void vnc_set_audio_enabled(VncSession* s, int enabled);

/* 音频后端是否可用（有播放设备且已启用）。 */
VNC_API int vnc_audio_available(VncSession* s);

/* ========================================================================
 * 网络统计
 * ====================================================================== */

/* 取累计统计：帧数、像素数、字节数和当前带宽估算（bit/s）。
   任一输出指针可为 NULL。 */
VNC_API void vnc_get_stats(VncSession* s, unsigned* updates,
                           unsigned* pixels, unsigned* bytes,
                           unsigned long long* bitsPerSecond);

/* 自动按带宽调整 JPEG 质量（12~16 Mbps 滞回带）。 */
VNC_API void vnc_set_auto_select_quality(VncSession* s, int enabled);

/* 把远程桌面设成单个指定尺寸（窗口变化时同步远程分辨率）。
   服务器不支持 SetDesktopSize 时核心会忽略——调用方不能假装成功。 */
VNC_API void vnc_request_remote_size(VncSession* s, int width, int height);

/* ========================================================================
 * 剪贴板
 * ====================================================================== */

VNC_API void vnc_request_clipboard(VncSession* s);
VNC_API void vnc_announce_clipboard(VncSession* s, int available);
VNC_API void vnc_send_clipboard(VncSession* s, const char* text);

/* ========================================================================
 * 编码 / 画质
 * ====================================================================== */

VNC_API void vnc_set_encoding(VncSession* s, int encoding);
VNC_API void vnc_set_compress_level(VncSession* s, int level);
VNC_API void vnc_set_quality_level(VncSession* s, int level);

/* ========================================================================
 * 多显示器
 *
 * screens 是 size 个 int 的数组，每 4 个一组：
 *   [x, y, width, height]
 * ====================================================================== */

VNC_API void vnc_request_screen_layout(VncSession* s, int width, int height,
                                       const int* screens, int count);

/* ========================================================================
 * 测试 / 元信息
 * ====================================================================== */

/* 非阻塞停止请求；最终销毁仍须在非回调线程等待退出。 */
VNC_API void vnc_request_stop(VncSession* s);
/* 复制已发布 BGRX 快照；返回 0 成功、1 无帧、-1 容量不足/参数错误。
   width/height 始终返回当前快照尺寸，可用 NULL data 查询尺寸。 */
VNC_API int vnc_copy_frame(VncSession* s, uint8_t* data, size_t capacity,
                            int stride, int* width, int* height);
VNC_API int vnc_test_add(int a, int b);
VNC_API const char* vnc_version(void);

/* ========================================================================
 * 自动更新
 *
 * 复用 src/common/core/UpdateChecker：GitHub Releases API 查询 +
 * 域名白名单 + 强制 SHA-256 校验 + 静默安装。
 * 所有函数都是阻塞的（内部走 WinHTTP），调用方必须放到后台线程。
 * 字符串一律 UTF-8，输出缓冲由调用方提供。
 * ====================================================================== */

/* 查询最新发行版。成功返回 0，并把字段写进调用方缓冲；
   失败返回负数并把原因写进 err。字段用 NULL 表示"不关心"。
   版本比较由调用方自己做（见 vnc_compare_versions）。 */
typedef struct VncReleaseInfo {
  char version[64];
  char name[256];
  char publishedAt[64];
  char htmlUrl[1024];
  char downloadUrl[1024];
  char checksumUrl[1024];
  long long size;
  /* 发行说明可能很长，单独由调用方给缓冲 */
  char* notes;
  size_t notesCapacity;
} VncReleaseInfo;

VNC_API int vnc_check_update(VncReleaseInfo* info, char* err, size_t errLen);

/* 语义化版本比较：a<b 返回 -1，相等 0，a>b 返回 1。 */
VNC_API int vnc_compare_versions(const char* a, const char* b);

/* 下载 → 校验 → 静默安装更新版本。成功返回 0。
   checksumUrl 为空时直接拒绝（安全要求，不提供免校验安装）。 */
VNC_API int vnc_download_and_install(const char* url, const char* checksumUrl,
                                     const char* version,
                                     char* err, size_t errLen);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* TUPIGVNC_VNCABI_H_ */
