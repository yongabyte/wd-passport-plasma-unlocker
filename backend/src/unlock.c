#include "unlock.h"
#include <openssl/evp.h>
#include <string.h>

// One-shot SHA-256 via system libcrypto. Zeros out on failure so a
// backend error degrades to "wrong password", never to a wild hash.
static void sha256_once(const uint8_t *data, size_t len,
                        uint8_t hash[WD_HASH_LEN]) {
  size_t outl = 0;
  if (data && EVP_Q_digest(NULL, "SHA256", NULL, data, len, hash, &outl) &&
      outl == WD_HASH_LEN)
    return;
  memset(hash, 0, WD_HASH_LEN);
}

uint8_t wd_hsb_checksum(const uint8_t *block510) {
  if (!block510)
    return 0;
  unsigned c = 0;
  for (int i = 0; i < 510; i++)
    c += block510[i];
  c += block510[0]; // WD quirk: data[0] counted twice
  return (uint8_t)((c * (unsigned)-1) & 0xFF);
}

int wd_verify_hsb1(const uint8_t *block) {
  if (!block)
    return -1;
  if (block[0] != WD_HSB1_SIG0 || block[1] != WD_HSB1_SIG1 ||
      block[2] != WD_HSB1_SIG2 || block[3] != WD_HSB1_SIG3)
    return -1;
  if (wd_hsb_checksum(block) != block[511])
    return -2;
  return 0;
}

int wd_parse_hsb1(const uint8_t *block, uint32_t *iteration_out,
                  uint8_t salt_out[WD_SALT_LEN]) {
  if (!block || !iteration_out || !salt_out)
    return -1;
  if (wd_verify_hsb1(block) != 0)
    return -1;
  *iteration_out = (uint32_t)block[8] | ((uint32_t)block[9] << 8) |
                   ((uint32_t)block[10] << 16) | ((uint32_t)block[11] << 24);
  memcpy(salt_out, block + 12, WD_SALT_LEN);
  return 0;
}

size_t wd_clean_salt(const uint8_t salt[WD_SALT_LEN], char *out,
                     size_t out_len) {
  if (!salt || !out || out_len == 0)
    return 0;
  size_t pos = 0;
  for (size_t i = 0; i < WD_SALT_LEN / 2; i++) {
    if (salt[2 * i] == 0x00 && salt[2 * i + 1] == 0x00)
      break;
    if (pos + 1 >= out_len)
      break;
    out[pos++] = (char)salt[2 * i];
  }
  out[pos] = '\0';
  return pos;
}

/* Decode one UTF-8 sequence. Returns bytes consumed (1..4) or 0 on error. */
static size_t utf8_decode(const char *s, size_t avail, uint32_t *cp) {
  const uint8_t *u = (const uint8_t *)s;
  if (avail == 0)
    return 0;
  if (u[0] < 0x80) {
    *cp = u[0];
    return 1;
  }
  if ((u[0] & 0xE0) == 0xC0) {
    if (avail < 2 || (u[1] & 0xC0) != 0x80)
      return 0;
    *cp = ((uint32_t)(u[0] & 0x1F) << 6) | (u[1] & 0x3F);
    if (*cp < 0x80)
      return 0; // overlong
    return 2;
  }
  if ((u[0] & 0xF0) == 0xE0) {
    if (avail < 3 || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80)
      return 0;
    *cp = ((uint32_t)(u[0] & 0x0F) << 12) | ((uint32_t)(u[1] & 0x3F) << 6) |
          (u[2] & 0x3F);
    if (*cp < 0x800)
      return 0;
    return 3;
  }
  if ((u[0] & 0xF8) == 0xF0) {
    if (avail < 4 || (u[1] & 0xC0) != 0x80 || (u[2] & 0xC0) != 0x80 ||
        (u[3] & 0xC0) != 0x80)
      return 0;
    *cp = ((uint32_t)(u[0] & 0x07) << 18) | ((uint32_t)(u[1] & 0x3F) << 12) |
          ((uint32_t)(u[2] & 0x3F) << 6) | (u[3] & 0x3F);
    if (*cp < 0x10000 || *cp > 0x10FFFF)
      return 0;
    return 4;
  }
  return 0;
}

int wd_mk_password_block(const char *passwd, uint32_t iteration,
                         const uint8_t salt[WD_SALT_LEN],
                         uint8_t out32[WD_HASH_LEN]) {
  if (!passwd || !salt || !out32)
    return -1;
  size_t pwlen = strlen(passwd);
  if (pwlen == 0 || pwlen > WD_MAX_PASSWORD_BYTES)
    return -2;
  if (iteration == 0)
    return -1;

  char clean[5];
  wd_clean_salt(salt, clean, sizeof(clean));

  // Build UTF-16LE of clean + passwd into fixed buffer.
  uint8_t utf16[512];
  size_t pos = 0;
  const char *parts[2] = {clean, passwd};
  for (int p = 0; p < 2; p++) {
    size_t len = strlen(parts[p]);
    size_t i = 0;
    while (i < len) {
      uint32_t cp = 0;
      size_t n = utf8_decode(parts[p] + i, len - i, &cp);
      if (n == 0)
        return -1;
      i += n;
      if (cp < 0x10000) {
        if (pos + 2 > sizeof(utf16))
          return -1;
        utf16[pos++] = cp & 0xFF;
        utf16[pos++] = (cp >> 8) & 0xFF;
      } else {
        if (pos + 4 > sizeof(utf16))
          return -1;
        cp -= 0x10000;
        uint16_t hi = 0xD800 + (cp >> 10), lo = 0xDC00 + (cp & 0x3FF);
        utf16[pos++] = hi & 0xFF;
        utf16[pos++] = (hi >> 8) & 0xFF;
        utf16[pos++] = lo & 0xFF;
        utf16[pos++] = (lo >> 8) & 0xFF;
      }
    }
  }

  uint8_t digest[32];
  sha256_once(utf16, pos, digest);
  for (uint32_t k = 1; k < iteration; k++)
    sha256_once(digest, sizeof(digest), digest);
  memcpy(out32, digest, WD_HASH_LEN);
  return 0;
}

int wd_parse_status(const uint8_t *data, uint8_t *locked_out,
                    uint8_t *cipher_out, uint16_t *pwlen_out) {
  if (!data)
    return -1;
  if (data[0] != 0x45)
    return -1;
  if (locked_out)
    *locked_out = data[3];
  if (cipher_out)
    *cipher_out = data[4];
  if (pwlen_out)
    *pwlen_out = (uint16_t)(((uint16_t)data[6] << 8) | data[7]);
  return 0;
}

int wd_validate_pwlen(uint16_t pwlen) {
  if (pwlen == 0 || pwlen > WD_MAX_KEYLEN)
    return -1;
  if ((unsigned)pwlen + 8u > 255u)
    return -1;
  return 0;
}

int wd_build_status_cdb(uint8_t cdb_out[10]) {
  if (!cdb_out)
    return -1;
  const uint8_t exp[10] = {0xC0, 0x45, 0, 0, 0, 0, 0, 0, 0x30, 0};
  memcpy(cdb_out, exp, 10);
  return 0;
}

int wd_build_read_hsb_cdb(uint32_t page, uint8_t cdb_out[10]) {
  if (!cdb_out)
    return -1;
  cdb_out[0] = 0xD8;
  cdb_out[1] = 0x00;
  cdb_out[2] = (page >> 24) & 0xFF;
  cdb_out[3] = (page >> 16) & 0xFF;
  cdb_out[4] = (page >> 8) & 0xFF;
  cdb_out[5] = page & 0xFF;
  cdb_out[6] = 0x00;
  cdb_out[7] = 0x00;
  cdb_out[8] = 0x01;
  cdb_out[9] = 0x00;
  return 0;
}

int wd_build_unlock_cdb(uint16_t pwlen, uint8_t cdb_out[10]) {
  if (!cdb_out || wd_validate_pwlen(pwlen) != 0)
    return -1;
  cdb_out[0] = 0xC1;
  cdb_out[1] = 0xE1;
  cdb_out[2] = cdb_out[3] = cdb_out[4] = cdb_out[5] = cdb_out[6] = cdb_out[7] =
      0x00;
  cdb_out[8] = (uint8_t)(pwlen + 8u);
  cdb_out[9] = 0x00;
  return 0;
}

int wd_device_allowed(const char *realpath, const char *id_serial) {
  if (!realpath || !id_serial)
    return -1;
  if (realpath[0] == '\0' || strstr(realpath, "..") != NULL)
    return -1;
  const char *dev = "/dev/sd";
  size_t n = strlen(dev);
  if (strncmp(realpath, dev, n) != 0)
    return -1;
  const char *tail = realpath + n;
  if (*tail == '\0')
    return -1;
  for (const char *p = tail; *p; p++) {
    if (*p < 'a' || *p > 'z')
      return -1; // rejects partitions (digits), nvme, dm
  }
  // Accept legacy "Western_Digital_My_*" (upstream format) and real-HW
  // short forms "WD_My_Passport_*", "usb-WD_My_Passport_*".
  static const char *kLegacy = "Western_Digital_My_";
  if (strncmp(id_serial, kLegacy, strlen(kLegacy)) == 0)
    return 0;
  if (strstr(id_serial, "My_Passport") != NULL)
    return 0;
  return -1;
}
