#include <criterion/criterion.h>
#include <string.h>
#include "unlock.h"

static void make_hsb1(uint8_t *b, uint32_t iter, const uint8_t salt[8]) {
  memset(b, 0, WD_BLOCK_SIZE);
  b[0] = 0x00;
  b[1] = 0x01;
  b[2] = 0x44;
  b[3] = 0x57;
  b[8] = iter & 0xFF;
  b[9] = (iter >> 8) & 0xFF;
  b[10] = (iter >> 16) & 0xFF;
  b[11] = (iter >> 24) & 0xFF;
  memcpy(b + 12, salt, 8);
  b[511] = wd_hsb_checksum(b);
}

Test(checksum, zero_block_with_sig) {
  uint8_t b[WD_BLOCK_SIZE];
  memset(b, 0, sizeof(b));
  b[0] = 0x00;
  b[1] = 0x01;
  b[2] = 0x44;
  b[3] = 0x57;
  // 0+1+0x44+0x57 = 156; (156 + data[0]=0) * -1 & 0xFF = 100
  cr_assert_eq(wd_hsb_checksum(b), 100);
}

Test(checksum, counts_data0_twice) {
  uint8_t b[WD_BLOCK_SIZE];
  memset(b, 0, sizeof(b));
  b[0] = 0x05;
  // sum(510) = 5, + data[0]=5 -> 10; -10 & 0xFF = 246
  cr_assert_eq(wd_hsb_checksum(b), 246);
}

Test(hsb1, verify_ok) {
  uint8_t b[WD_BLOCK_SIZE];
  const uint8_t salt[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  make_hsb1(b, 1000, salt);
  cr_assert_eq(wd_verify_hsb1(b), 0);
}

Test(hsb1, bad_signature) {
  uint8_t b[WD_BLOCK_SIZE];
  const uint8_t salt[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  make_hsb1(b, 1000, salt);
  b[2] = 0x00;
  b[511] = wd_hsb_checksum(b); // fix checksum so only sig fails
  cr_assert_eq(wd_verify_hsb1(b), -1);
}

Test(hsb1, bad_checksum) {
  uint8_t b[WD_BLOCK_SIZE];
  const uint8_t salt[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  make_hsb1(b, 1000, salt);
  b[511] ^= 0xFF;
  cr_assert_eq(wd_verify_hsb1(b), -2);
}

Test(hsb1, parse_iteration_and_salt) {
  uint8_t b[WD_BLOCK_SIZE];
  const uint8_t salt[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
  make_hsb1(b, 1000, salt);
  uint32_t iter = 0;
  uint8_t out[8];
  cr_assert_eq(wd_parse_hsb1(b, &iter, out), 0);
  cr_assert_eq(iter, 1000);
  cr_assert_arr_eq(out, salt, 8);
}
