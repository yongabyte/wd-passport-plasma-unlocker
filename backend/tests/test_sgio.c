#include <criterion/criterion.h>
#include <string.h>
#include "unlock.h"

Test(sgio, status_cdb_shape) {
  uint8_t cdb[10];
  cr_assert_eq(wd_build_status_cdb(cdb), 0);
  const uint8_t exp[10] = {0xC0, 0x45, 0, 0, 0, 0, 0, 0, 0x30, 0};
  cr_assert_arr_eq(cdb, exp, 10);
}

Test(sgio, read_hsb_page1) {
  uint8_t cdb[10];
  cr_assert_eq(wd_build_read_hsb_cdb(1, cdb), 0);
  // D8 + be32(1) at bytes 2..5, fixed tail
  const uint8_t exp[10] = {0xD8, 0, 0, 0, 0, 0x01, 0, 0, 0x01, 0};
  cr_assert_arr_eq(cdb, exp, 10);
}

Test(sgio, unlock_cdb_len) {
  uint8_t cdb[10];
  cr_assert_eq(wd_build_unlock_cdb(32, cdb), 0);
  cr_assert_eq(cdb[0], 0xC1);
  cr_assert_eq(cdb[1], 0xE1);
  cr_assert_eq(cdb[8], 40); // 32 + 8
}

Test(sgio, rejects_oversize_pwlen) {
  uint8_t cdb[10];
  cr_assert_neq(wd_build_unlock_cdb(200, cdb), 0);
  cr_assert_eq(wd_validate_pwlen(32), 0);
  cr_assert_neq(wd_validate_pwlen(200), 0);
}

Test(status, parse_ok_and_bad_sig) {
  uint8_t b[WD_BLOCK_SIZE];
  memset(b, 0, sizeof(b));
  b[0] = 0x45;
  b[3] = 0x01;
  b[4] = 0x30;
  b[6] = 0x00;
  b[7] = 0x20; // pwlen 32
  uint8_t locked = 0, cipher = 0;
  uint16_t pwlen = 0;
  cr_assert_eq(wd_parse_status(b, &locked, &cipher, &pwlen), 0);
  cr_assert_eq(locked, 0x01);
  cr_assert_eq(pwlen, 32);
  b[0] = 0x00;
  cr_assert_neq(wd_parse_status(b, &locked, &cipher, &pwlen), 0);
}
