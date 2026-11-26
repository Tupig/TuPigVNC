/* Copyright (C) 2002-2005 RealVNC Ltd.  All Rights Reserved.
 *
 * 本软件为自由软件；你可以依据自由软件基金会发布的 GNU 通用公共许可证
 * （GNU General Public License）的条款（第 2 版，或你选择的任何更高版本）
 * 重新分发和/或修改本软件。
 *
 * 本软件的发布是希望它有用，但不附带任何担保；甚至不附带适销性或
 * 特定用途适用性的默示担保。详见 GNU 通用公共许可证。
 *
 * 你应已收到随本软件一同提供的 GNU 通用公共许可证副本；如果没有，
 * 请致信自由软件基金会：Inc., 59 Temple Place - Suite 330, Boston,
 * MA  02111-1307, USA。
 */
// -=- Registry.h

// Win32 Registry API 的 C++ 封装

#ifndef __RFB_WIN32_REGISTRY_H__
#define __RFB_WIN32_REGISTRY_H__

#include <string>
#include <vector>

#include <windows.h>
#include <rfb_win32/Security.h>

namespace rfb {

  namespace win32 {

    class RegKey {
    public:
      // 未打开任何键
      RegKey();

      // 复制指定的已存在的键
      RegKey(const HKEY k);
      RegKey(const RegKey& k);

      // 内部会调用 close()
      ~RegKey();

      void setHKEY(HKEY key, bool freeKey);
    private:
      RegKey& operator=(const RegKey& k);
      HKEY& operator=(const HKEY& k);
    public:

      // 如果键是新建的返回 true，若已存在则返回 false
      bool createKey(const RegKey& root, const char* name);

      // 键存在则打开，不存在则抛出异常
      void openKey(const RegKey& root, const char* name, bool readOnly=false);

      // 设置该键的（自主）访问控制列表
      void setDACL(const PACL acl, bool inheritFromParent=true);

      // 按需关闭当前键
      void close();

      // 删除子键/值
      void deleteKey(const char* name) const;
      void deleteValue(const char* name) const;


      // 阻塞等待注册表变化；若指定了 event，则立即返回并在发生变化时通知该事件
      void awaitChange(bool watchSubTree, DWORD filter, HANDLE event=nullptr) const;

      void setExpandString(const char* valname, const char* s) const;
      void setString(const char* valname, const char* s) const;
      void setBinary(const char* valname, const void* data, size_t length) const;
      void setInt(const char* valname, int i) const;
      void setBool(const char* valname, bool b) const;

      std::string getString(const char* valname) const;
      std::string getString(const char* valname, const char* def) const;

      std::vector<uint8_t> getBinary(const char* valname) const;
      std::vector<uint8_t> getBinary(const char* valname, const uint8_t* def, size_t deflength) const;

      int getInt(const char* valname) const;
      int getInt(const char* valname, int def) const;

      bool getBool(const char* valname) const;
      bool getBool(const char* valname, bool def) const;

      std::string getRepresentation(const char* valname) const;

      bool isValue(const char* valname) const;

      // 获取第 "i" 个值/键的名称
      // 如果值的数量少于 "i"，则返回 0
      // 名称的所有权归 RegKey 对象所有！
      const char* getValueName(int i);
      const char* getKeyName(int i);

      operator HKEY() const;
    protected:
      HKEY key;
      bool freeKey;
      // 枚举值/键名时复用的缓冲区，由 std::vector 自动管理
      // （替代裸 char* + 长度，避免手工 new[]/delete[] 的生命周期问题）
      std::vector<char> valueName;
    };

  };

};

#endif // __RFB_WIN32_REG_CONFIG_H__
