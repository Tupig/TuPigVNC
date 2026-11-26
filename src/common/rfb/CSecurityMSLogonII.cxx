/*
 * Copyright (C) 2022 Dinglan Peng
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifndef HAVE_NETTLE
#error "This header should not be compiled without HAVE_NETTLE defined"
#endif

#include <stdlib.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include <assert.h>

#include <nettle/des.h>
#include <nettle/cbc.h>
#include <nettle/bignum.h>

#include <core/i18n.h>

#include <rfb/CSecurityMSLogonII.h>
#include <rfb/CConnection.h>
#include <rfb/Exception.h>

#include <rdr/InStream.h>
#include <rdr/OutStream.h>
#include <rdr/RandomStream.h>

using namespace rfb;

CSecurityMSLogonII::CSecurityMSLogonII(CConnection* cc_)
  : CSecurity(cc_)
{
  mpz_init(g);
  mpz_init(p);
  mpz_init(A);
  mpz_init(b);
  mpz_init(B);
  mpz_init(k);
}

CSecurityMSLogonII::~CSecurityMSLogonII()
{
  mpz_clear(g);
  mpz_clear(p);
  mpz_clear(A);
  mpz_clear(b);
  mpz_clear(B);
  mpz_clear(k);
}

bool CSecurityMSLogonII::processMsg()
{
  if (readKey()) {
    writeCredentials();
    return true;
  }
  return false;
}

bool CSecurityMSLogonII::readKey()
{
  rdr::InStream* is = cc->getInStream();
  if (!is->hasData(24))
    return false;
  uint8_t gBytes[8];
  uint8_t pBytes[8];
  uint8_t ABytes[8];
  is->readBytes(gBytes, 8);
  is->readBytes(pBytes, 8);
  is->readBytes(ABytes, 8);
  nettle_mpz_set_str_256_u(g, 8, gBytes);
  nettle_mpz_set_str_256_u(p, 8, pBytes);
  if (mpz_sgn(p) <= 0)
    throw protocol_error(_("Received invalid Diffie-Hellman modulus"));
  nettle_mpz_set_str_256_u(A, 8, ABytes);
  return true;
}

void CSecurityMSLogonII::writeCredentials()
{
  std::string username;
  std::string password;
  rdr::RandomStream rs;

  cc->getUserPasswd(&username, &password);

  std::vector<uint8_t> bBytes(8);
  if (!rs.hasData(8))
    throw std::runtime_error(_("Failed to generate random data"));
  rs.readBytes(bBytes.data(), bBytes.size());
  nettle_mpz_set_str_256_u(b, bBytes.size(), bBytes.data());
  mpz_powm(k, A, b, p);
  mpz_powm(B, g, b, p);

  uint8_t key[8];
  uint8_t reversedKey[8];
  uint8_t BBytes[8];
  uint8_t user[256];
  uint8_t pass[64];
  nettle_mpz_get_str_256(8, key, k);
  nettle_mpz_get_str_256(8, BBytes, B);
  for (int i = 0; i < 8; ++i) {
    uint8_t x = 0;
    for (int j = 0; j < 8; ++j) {
      x |= ((key[i] >> j) & 1) << (7 - j);
    }
    reversedKey[i] = x;
  }

  if (!rs.hasData(256 + 64))
    throw std::runtime_error(_("Failed to generate random data"));
  rs.readBytes(user, 256);
  rs.readBytes(pass, 64);
  if (username.size() >= 256)
    throw std::length_error("Username is too long");
  memcpy(user, username.c_str(), username.size() + 1);
  if (password.size() >= 64)
    throw std::length_error("Password is too long");
  memcpy(pass, password.c_str(), password.size() + 1);

  // 使用原始密钥作为 IV、反转后的密钥作为 DES 密钥的 DES-CBC 加密。
  // MSLogonII 是 UltraVNC 的 Windows 域登录扩展，协议强制使用 DES-CBC
  // （服务端兼容性要求，客户端无法更换算法），因此这里的 DES 属于协议
  // 实现，而非本客户端可选的弱加密。
  // codeql[cpp/weak-cryptographic-algorithm]
  struct CBC_CTX(struct des_ctx, DES_BLOCK_SIZE) ctx;
  des_fix_parity(8, reversedKey, reversedKey);
  des_set_key(&ctx.ctx, reversedKey);
  CBC_SET_IV(&ctx, key);
  CBC_ENCRYPT(&ctx, des_encrypt, 256, user, user);
  CBC_SET_IV(&ctx, key);
  CBC_ENCRYPT(&ctx, des_encrypt, 64, pass, pass);

  rdr::OutStream* os = cc->getOutStream();
  os->writeBytes(BBytes, 8);
  os->writeBytes(user, 256);
  os->writeBytes(pass, 64);
  os->flush();
}
