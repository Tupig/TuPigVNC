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

  /* 服务器发来的剪贴板文本（UTF-8）。仅在「获得焦点」时才会真正同步。 */
  void (*on_clipboard)(void* ud, const char* text);

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

/* 请求停止事件循环并等待线程退出（会阻塞，最长 5 秒）。 */
VNC_API void vnc_stop(VncSession* s);

/* 销毁会话。内部会自动调用 vnc_stop。
   调用后句柄失效。 */
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

VNC_API int vnc_test_add(int a, int b);
VNC_API const char* vnc_version(void);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif  /* TUPIGVNC_VNCABI_H_ */
