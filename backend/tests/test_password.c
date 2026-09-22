#include <criterion/criterion.h>
#include <string.h>
#include "unlock.h"

static void hex_of(const uint8_t h[32], char out[65]) {
  static const char *d = "0123456789abcdef";
  for (int i = 0; i < 32; i++) {
    out[2 * i] = d[h[i] >> 4];
    out[2 * i + 1] = d[h[i] & 0xF];
  }
  out[64] = 0;
}

Test(salt, plain) {
  const uint8_t s[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  char out[8];
  // Python skips odd bytes: pairs (A,B)->A, (C,D)->C, (E,F)->E, (G,H)->G
  cr_assert_eq(wd_clean_salt(s, out, sizeof(out)), 4);
  cr_assert_str_eq(out, "ACEG");
}

Test(salt, early_nul_pair_stops) {
  const uint8_t s[8] = {'A', 'B', 0, 0, 'C', 'D', 'E', 'F'};
  char out[8];
  cr_assert_eq(wd_clean_salt(s, out, sizeof(out)), 1);
  cr_assert_str_eq(out, "A");
}

Test(password, v1_single_iter) {
  const uint8_t s[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  uint8_t h[32];
  char hex[65];
  cr_assert_eq(wd_mk_password_block("test", 1, s, h), 0);
  hex_of(h, hex);
  cr_assert_str_eq(hex, "d33ac82da6640c57ad72c26d287a8a598a018497c5d09118e3bb885b460ae134");
}

Test(password, v2_1000_iter) {
  const uint8_t s[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  uint8_t h[32];
  char hex[65];
  cr_assert_eq(wd_mk_password_block("test", 1000, s, h), 0);
  hex_of(h, hex);
  cr_assert_str_eq(hex, "07afd560b05241696a3186785156a8803513ed9ed4315b5bdc01c4dad8dc7705");
}

Test(password, v4_utf8) {
  const uint8_t s[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
  uint8_t h[32];
  char hex[65];
  cr_assert_eq(wd_mk_password_block("p@ss w\xc3\xb6rld", 2, s, h), 0);
  hex_of(h, hex);
  cr_assert_str_eq(hex, "abf81c5ab19418b7550cc9d13a5c93535c6b73841ee231afe403bb020cbea72a");
}

Test(password, rejects_empty_and_oversize) {
  const uint8_t s[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
  uint8_t h[32];
  cr_assert_neq(wd_mk_password_block("", 1, s, h), 0);
  cr_assert_neq(wd_mk_password_block(NULL, 1, s, h), 0);
  char big[200];
  memset(big, 'x', sizeof(big) - 1);
  big[sizeof(big) - 1] = 0;
  cr_assert_neq(wd_mk_password_block(big, 1, s, h), 0);
}
