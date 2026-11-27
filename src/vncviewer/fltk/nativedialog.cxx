/*
 * TuPigVNC · Windows 原生文件对话框实现（IFileDialog）
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "nativedialog.h"

#ifdef WIN32

#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shlwapi.h>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#pragma comment(lib, "shell32.lib")

namespace {

std::wstring toWide(const char *s)
{
  if (s == nullptr || s[0] == '\0')
    return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
  if (n <= 1)
    return std::wstring();
  std::wstring w((size_t)(n - 1), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
  return w;
}

std::string toUtf8(const wchar_t *w)
{
  if (w == nullptr)
    return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr,
                              nullptr);
  if (n <= 1)
    return std::string();
  std::string s((size_t)(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
  return s;
}

/* 从 "*.tigervnc" 里抠出 "tigervnc" */
std::wstring defaultExtFromSpec(const std::wstring &spec)
{
  size_t dot = spec.rfind(L'.');
  if (dot == std::wstring::npos)
    return std::wstring();
  std::wstring ext = spec.substr(dot + 1);
  size_t semi = ext.find(L';');
  if (semi != std::wstring::npos)
    ext = ext.substr(0, semi);
  return ext;
}

std::string showDialog(bool save, void *ownerHwnd, const char *title,
                       const char *filterName, const char *filterSpec,
                       const char *initialDir, const char *defaultName)
{
  // FLTK 主线程可能已经初始化过 COM（OLE 拖放），重复初始化会返回
  // S_FALSE / RPC_E_CHANGED_MODE，都说明 COM 可用；只有失败才需要收尾。
  HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED |
                                          COINIT_DISABLE_OLE1DDE);
  const bool needUninit = SUCCEEDED(hrCom);

  std::string result;

  IFileDialog *dlg = nullptr;
  HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog
                                     : CLSID_FileOpenDialog,
                                nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dlg));
  if (FAILED(hr) || dlg == nullptr) {
    if (needUninit)
      CoUninitialize();
    return result;
  }

  DWORD opts = 0;
  dlg->GetOptions(&opts);
  opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
  opts |= save ? FOS_OVERWRITEPROMPT : FOS_FILEMUSTEXIST;
  dlg->SetOptions(opts);

  std::wstring wTitle = toWide(title);
  if (!wTitle.empty())
    dlg->SetTitle(wTitle.c_str());

  std::wstring wFilterName = toWide(filterName);
  std::wstring wFilterSpec = toWide(filterSpec);
  if (!wFilterSpec.empty()) {
    COMDLG_FILTERSPEC spec[1];
    spec[0].pszName = wFilterName.c_str();
    spec[0].pszSpec = wFilterSpec.c_str();
    dlg->SetFileTypes(1, spec);
    dlg->SetFileTypeIndex(1);

    std::wstring ext = defaultExtFromSpec(wFilterSpec);
    if (!ext.empty())
      dlg->SetDefaultExtension(ext.c_str());
  }

  if (initialDir != nullptr && initialDir[0] != '\0') {
    std::wstring wDir = toWide(initialDir);
    IShellItem *folder = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(wDir.c_str(), nullptr,
                                              IID_PPV_ARGS(&folder))) &&
        folder != nullptr) {
      dlg->SetFolder(folder);
      folder->Release();
    }
  }

  if (save && defaultName != nullptr && defaultName[0] != '\0') {
    std::wstring wName = toWide(defaultName);
    dlg->SetFileName(wName.c_str());
  }

  hr = dlg->Show((HWND)ownerHwnd);
  if (SUCCEEDED(hr)) {
    IShellItem *item = nullptr;
    if (SUCCEEDED(dlg->GetResult(&item)) && item != nullptr) {
      PWSTR path = nullptr;
      if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) &&
          path != nullptr) {
        result = toUtf8(path);
        CoTaskMemFree(path);
      }
      item->Release();
    }
  }

  dlg->Release();
  if (needUninit)
    CoUninitialize();
  return result;
}

}  // namespace

namespace nativeui {

std::string openFile(void *ownerHwnd, const char *title,
                     const char *filterName, const char *filterSpec,
                     const char *initialDir)
{
  return showDialog(false, ownerHwnd, title, filterName, filterSpec,
                    initialDir, nullptr);
}

std::string saveFile(void *ownerHwnd, const char *title,
                     const char *filterName, const char *filterSpec,
                     const char *initialDir, const char *defaultName)
{
  return showDialog(true, ownerHwnd, title, filterName, filterSpec,
                    initialDir, defaultName);
}

}  // namespace nativeui

#else  // !WIN32

namespace nativeui {

std::string openFile(void *, const char *, const char *, const char *,
                     const char *)
{
  return std::string();
}

std::string saveFile(void *, const char *, const char *, const char *,
                     const char *, const char *)
{
  return std::string();
}

}  // namespace nativeui

#endif
