#pragma once
#include <stdint.h>
#include <stddef.h>

#define WD_BLOCK_SIZE 512
#define WD_SALT_LEN 8
#define WD_HASH_LEN 32
#define WD_MAX_PASSWORD_BYTES 128
#define WD_MAX_KEYLEN 64
#define WD_HSB1_SIG0 0x00
#define WD_HSB1_SIG1 0x01
#define WD_HSB1_SIG2 0x44
#define WD_HSB1_SIG3 0x57

/* Checksum over first 510 bytes (data[0] counted twice, matching WD quirk). */
uint8_t wd_hsb_checksum(const uint8_t *block510);

/* 0 ok, -1 bad signature, -2 bad checksum. block must be 512 bytes. */
int wd_verify_hsb1(const uint8_t *block);

/* 0 ok, -1 on invalid block. iteration/salt valid only on 0. */
int wd_parse_hsb1(const uint8_t *block, uint32_t *iteration_out,
                  uint8_t salt_out[WD_SALT_LEN]);

/* Strip NUL-pairs: returns clean len. out must hold at least 5 bytes
 * (8-byte salt yields at most 4 chars + NUL). */
size_t wd_clean_salt(const uint8_t salt[WD_SALT_LEN], char *out, size_t out_len);

/* Hash password: passwd is NUL-terminated UTF-8, 1..128 bytes.
 * 0 ok, -1 bad args, -2 empty after validation. */
int wd_mk_password_block(const char *passwd, uint32_t iteration,
                         const uint8_t salt[WD_SALT_LEN],
                         uint8_t out32[WD_HASH_LEN]);

/* Status block (512 bytes from C0/45 CDB). 0 ok, -1 bad signature. */
int wd_parse_status(const uint8_t *data, uint8_t *locked_out,
                    uint8_t *cipher_out, uint16_t *pwlen_out);

/* Validate PasswordLength from drive. 0 ok, -1 too large. */
int wd_validate_pwlen(uint16_t pwlen);

/* Build CDBs. Returns 0 ok, -1 on bad pwlen. */
int wd_build_status_cdb(uint8_t cdb_out[10]);
int wd_build_read_hsb_cdb(uint32_t page, uint8_t cdb_out[10]);
int wd_build_unlock_cdb(uint16_t pwlen, uint8_t cdb_out[10]);

/* Device allowlist: realpath must be /dev/sd[a-z]+ (no digits),
 * id_serial must start with "Western_Digital_My_". 0 allowed, -1 denied. */
int wd_device_allowed(const char *realpath, const char *id_serial);
