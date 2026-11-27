/*
 * TuPigVNC · Windows 原生文件对话框
 *
 * FLTK 自带的 Fl_File_Chooser 是 90 年代 Unix 风格自绘弹窗，
 * 和 Windows 11 完全不是一个样子。这里直接调 IFileDialog
 * （Vista 之后资源管理器同款「打开/另存为」对话框）。
 *
 * 非 Windows 平台：返回空串，调用方自行回退。
 */

#ifndef __NATIVEDIALOG_H__
#define __NATIVEDIALOG_H__

#include <string>

namespace nativeui {

/* 打开文件。owner 传窗口 HWND（可传 nullptr）。
 * 返回 UTF-8 路径；用户取消返回空串。 */
std::string openFile(void *ownerHwnd, const char *title,
                     const char *filterName, const char *filterSpec,
                     const char *initialDir);

/* 另存为。defaultName 是预填的文件名，可为 nullptr。 */
std::string saveFile(void *ownerHwnd, const char *title,
                     const char *filterName, const char *filterSpec,
                     const char *initialDir, const char *defaultName);

}  // namespace nativeui

#endif  // __NATIVEDIALOG_H__
