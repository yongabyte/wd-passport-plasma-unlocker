#include <criterion/criterion.h>
#include <string.h>
#include "unlock.h"

Test(device, allows_wd_passport) {
  cr_assert_eq(wd_device_allowed("/dev/sdb", "Western_Digital_My_Passport_1234"), 0);
  cr_assert_eq(wd_device_allowed("/dev/sdc", "Western_Digital_My_Book_25EE"), 0);
  // Real HW (1058:0820) reports short ID_SERIAL form:
  cr_assert_eq(wd_device_allowed("/dev/sda", "WD_My_Passport_0820_575831314143333139333132-0:0"), 0);
  cr_assert_eq(wd_device_allowed("/dev/sda", "usb-WD_My_Passport_0820_575831314143333139333132-0:0"), 0);
}

Test(device, rejects_system_and_partitions) {
  cr_assert_neq(wd_device_allowed("/dev/sda", "ATA_Samsung_SSD"), 0);
  cr_assert_neq(wd_device_allowed("/dev/sdb1", "Western_Digital_My_Passport_1234"), 0);
  cr_assert_neq(wd_device_allowed("/dev/nvme0n1", "Western_Digital_My_Passport_X"), 0);
  cr_assert_neq(wd_device_allowed("/dev/dm-0", "Western_Digital_My_Passport_X"), 0);
}

Test(device, rejects_bad_inputs) {
  cr_assert_neq(wd_device_allowed(NULL, "Western_Digital_My_Passport_X"), 0);
  cr_assert_neq(wd_device_allowed("/dev/sdb", NULL), 0);
  cr_assert_neq(wd_device_allowed("/dev/sdb", "SanDisk_Ultra"), 0);
  cr_assert_neq(wd_device_allowed("/dev/../etc/shadow", "Western_Digital_My_Passport_X"), 0);
  cr_assert_neq(wd_device_allowed("", "Western_Digital_My_Passport_X"), 0);
}
