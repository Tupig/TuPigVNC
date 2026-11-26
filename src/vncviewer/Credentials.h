/* Copyright (C) 2026 TuPigVNC Team
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

#ifndef __CREDENTIALS_H__
#define __CREDENTIALS_H__

#include <string>

/*
 * 跨会话保存 VNC 登录凭据。
 *
 * 凭据写在 VNC 配置目录下的 credentials.json，key 为 "host:port"。
 * Windows 上密码用 DPAPI 加密后存盘，其他平台退化为 base64 编码。
 */
namespace credentials
{

/*
 * 保存某台服务器的凭据（密码加密后存盘）。
 * host 格式为 "host:port"，作为 key。
 * 成功返回 true；配置目录不可用或写盘失败时返回 false。
 */
bool save(const std::string& host, const std::string& user,
          const std::string& password);

/*
 * 读取某台服务器的凭据。
 * 找到且解密成功时返回 true，并按需填充 user/password（可传 nullptr 忽略）。
 */
bool load(const std::string& host, std::string* user, std::string* password);

/* 删除某台服务器的凭据（认证失败时调用）。 */
void forget(const std::string& host);

} // namespace credentials

#endif // __CREDENTIALS_H__
