// wd-unlocker-backend: unlock-only, runs as the active user (no root, no sudo).
// Device open() granted via udev uaccess; vendor SG_IO opcodes require the
// file capability cap_sys_rawio (kernel filters them without it).
// Password on stdin, never argv.
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <scsi/sg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unlock.h"

static void wipe(void *p, size_t n) {
#if defined(__STDC_LIB_EXT1__) || defined(explicit_bzero)
  explicit_bzero(p, n);
#else
  volatile uint8_t *v = p;
  while (n--)
    *v++ = 0;
#endif
}

static int sg_xfer(int fd, const uint8_t cdb[10], uint8_t *data, size_t len,
                   int to_dev, uint8_t *sense, size_t sense_len,
                   int *io_err_out) {
  uint8_t sens[32];
  memset(sens, 0, sizeof(sens));
  sg_io_hdr_t h;
  memset(&h, 0, sizeof(h));
  h.interface_id = 'S';
  h.cmd_len = 10;
  h.mx_sb_len = sizeof(sens);
  h.dxfer_direction = to_dev ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV;
  h.dxfer_len = (unsigned)len;
  h.dxferp = data;
  h.cmdp = (uint8_t *)cdb;
  h.sbp = sens;
  h.timeout = 5000;
  if (ioctl(fd, SG_IO, &h) != 0) {
    if (io_err_out)
      *io_err_out = errno;
    return -1;
  }
  if (io_err_out)
    *io_err_out = 0;
  if (h.status != 0 || h.host_status != 0 || h.driver_status != 0)
    return -1;
  if (sense && sense_len) {
    size_t n = h.sb_len_wr > sense_len ? sense_len : h.sb_len_wr;
    memcpy(sense, sens, n);
  }
  return 0;
}

// Query one udev property (e.g. ID_SERIAL) for a device node.
// Returns 0 and fills out on success.
static int udev_prop(const char *devnode, const char *key, char *out,
                     size_t out_len) {
  char cmd[256];
  snprintf(cmd, sizeof(cmd), "udevadm info --query=property --name=%s 2>/dev/null",
           devnode);
  FILE *fp = popen(cmd, "r");
  if (!fp)
    return -1;
  size_t klen = strlen(key);
  char line[512];
  int rc = -1;
  while (fgets(line, sizeof(line), fp)) {
    if (strncmp(line, key, klen) == 0 && line[klen] == '=') {
      snprintf(out, out_len, "%s", line + klen + 1);
      out[strcspn(out, "\r\n")] = '\0';
      rc = 0;
      break;
    }
  }
  pclose(fp);
  return rc;
}

static int is_wd_candidate(const char *vendor_id, const char *serial,
                           const char *model) {
  if (!vendor_id || strcmp(vendor_id, "1058") != 0)
    return 0;
  if (serial) {
    char fake_path[] = "/dev/sda"; // allowlist checks serial only here
    (void)fake_path;
    if (strstr(serial, "My_Passport") != NULL)
      return 1;
    if (strncmp(serial, "Western_Digital_My_", 19) == 0)
      return 1;
  }
  if (model && strstr(model, "Passport") != NULL)
    return 1;
  return 0;
}

// Enumerate whole disks via /sys/block/sd* and match WD via udev properties.
// Fills found[][PATH_MAX] with /dev/sdX and serials[] with ID_SERIAL.
static int find_wd_devices(char found[][PATH_MAX], char serials[][256],
                           int max) {
  DIR *d = opendir("/sys/block");
  if (!d)
    return 0;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && n < max) {
    // whole disks only: sda..sdzz (letters, no digits)
    if (strncmp(e->d_name, "sd", 2) != 0)
      continue;
    const char *tail = e->d_name + 2;
    if (*tail == '\0')
      continue;
    int ok = 1;
    for (const char *p = tail; *p; p++)
      if (*p < 'a' || *p > 'z')
        ok = 0;
    if (!ok)
      continue;
    char devnode[300];
    if (snprintf(devnode, sizeof(devnode), "/dev/%s", e->d_name) >= (int)sizeof(devnode))
      continue;
    char vendor[64] = {0}, serial[256] = {0}, model[256] = {0};
    if (udev_prop(devnode, "ID_VENDOR_ID", vendor, sizeof(vendor)) != 0)
      continue;
    udev_prop(devnode, "ID_SERIAL", serial, sizeof(serial));
    udev_prop(devnode, "ID_MODEL", model, sizeof(model));
    if (!is_wd_candidate(vendor, serial, model))
      continue;
    snprintf(found[n], PATH_MAX, "%s", devnode);
    snprintf(serials[n], 256, "%s", serial);
    n++;
  }
  closedir(d);
  return n;
}

static const char *serial_for(const char *realpath, char found[][PATH_MAX],
                              char serials[][256], int n) {
  for (int i = 0; i < n; i++)
    if (strcmp(found[i], realpath) == 0)
      return serials[i];
  // Fall back: query udev directly for an explicit -d path.
  static char direct[256];
  if (udev_prop(realpath, "ID_SERIAL", direct, sizeof(direct)) == 0)
    return direct;
  return NULL;
}

static void usage(const char *argv0) {
  fprintf(stderr, "Usage: printf %%s \"$PASSWORD\" | %s [-d /dev/sdX]\n",
          argv0);
}

int main(int argc, char **argv) {
  const char *force_dev = NULL;
  for (int i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--device") == 0) &&
        i + 1 < argc) {
      force_dev = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 ||
               strcmp(argv[i], "--help") == 0) {
      usage(argv[0]);
      return 2;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  prctl(PR_SET_DUMPABLE, 0, 0, 0, 0);

  // Password from stdin (pipe from kdialog frontend).
  static char pw[WD_MAX_PASSWORD_BYTES + 2];
  if (mlock(pw, sizeof(pw)) != 0) {
    // non-fatal; continue (e.g. RLIMIT_MEMLOCK)
  }
  size_t total = 0;
  ssize_t r;
  // Read up to 129 to detect oversize.
  while ((r = read(STDIN_FILENO, pw + total, sizeof(pw) - total)) > 0) {
    total += (size_t)r;
    if (total >= sizeof(pw))
      break;
  }
  if (total == 0) {
    fprintf(stderr, "wd-unlocker-backend: empty password\n");
    return 2;
  }
  // Strip single trailing newline / CRLF (echo-friendly, printf %s unaffected).
  if (total > 0 && pw[total - 1] == '\n')
    total--;
  if (total > 0 && pw[total - 1] == '\r')
    total--;
  if (total == 0 || total > WD_MAX_PASSWORD_BYTES) {
    fprintf(stderr, "wd-unlocker-backend: bad password length\n");
    wipe(pw, sizeof(pw));
    return 2;
  }
  pw[total] = '\0';

  char devpath[PATH_MAX] = {0};
  char serial[256] = {0};
  if (force_dev) {
    char real[PATH_MAX];
    if (!realpath(force_dev, real)) {
      fprintf(stderr, "wd-unlocker-backend: cannot resolve %s: %s\n",
              force_dev, strerror(errno));
      wipe(pw, sizeof(pw));
      return 2;
    }
    // Look up by-id serial for allowlist; fall back to basename check.
    char found[8][PATH_MAX];
    char serials[8][256];
    memset(found, 0, sizeof(found));
    int n = find_wd_devices(found, serials, 8);
    const char *s = serial_for(real, found, serials, n);
    // serial_for matches by-id name which contains "Western_Digital_My_",
    // satisfying wd_device_allowed's prefix check.
    const char *id = s ? s : "";
    // Extra fallback: derive from sysfs model? Keep strict: require by-id hit.
    if (!s || wd_device_allowed(real, id) != 0) {
      // Allow explicit -d with raw ID_SERIAL style? Try basename-agnostic:
      // still require WD prefix, so reject here.
      fprintf(stderr, "wd-unlocker-backend: device not allowlisted: %s\n",
              real);
      wipe(pw, sizeof(pw));
      return 2;
    }
    snprintf(devpath, sizeof(devpath), "%s", real);
    snprintf(serial, sizeof(serial), "%s", id);
  } else {
    char found[8][PATH_MAX];
    char serials[8][256];
    memset(found, 0, sizeof(found));
    int n = find_wd_devices(found, serials, 8);
    if (n == 0) {
      fprintf(stderr,
              "wd-unlocker-backend: no Western Digital Passport device found\n");
      wipe(pw, sizeof(pw));
      return 2;
    }
    if (n > 1) {
      fprintf(stderr,
              "wd-unlocker-backend: multiple devices found, use -d /dev/sdX\n");
      wipe(pw, sizeof(pw));
      return 2;
    }
    if (wd_device_allowed(found[0], serials[0]) != 0) {
      fprintf(stderr, "wd-unlocker-backend: device not allowlisted\n");
      wipe(pw, sizeof(pw));
      return 2;
    }
    snprintf(devpath, sizeof(devpath), "%s", found[0]);
    snprintf(serial, sizeof(serial), "%s", serials[0]);
  }

  int fd = open(devpath, O_RDWR | O_NOFOLLOW);
  if (fd < 0) {
    fprintf(stderr, "wd-unlocker-backend: cannot open %s: %s\n", devpath,
            strerror(errno));
    wipe(pw, sizeof(pw));
    return 2;
  }

  uint8_t cdb[10], block[WD_BLOCK_SIZE];
  uint8_t locked = 0, cipher = 0;
  uint16_t pwlen = 0;
  int rc = 1;
  int xerr = 0;

  wd_build_status_cdb(cdb);
  if (sg_xfer(fd, cdb, block, sizeof(block), 0, NULL, 0, &xerr) != 0 ||
      wd_parse_status(block, &locked, &cipher, &pwlen) != 0) {
    fprintf(stderr, "wd-unlocker-backend: status read failed (%s)\n",
            xerr ? strerror(xerr) : "command rejected");
    goto out;
  }
  if (locked == 0x00 || locked == 0x02) {
    fprintf(stderr, "wd-unlocker-backend: already unlocked\n");
    rc = 0;
    goto out;
  }
  if (locked != 0x01) {
    fprintf(stderr, "wd-unlocker-backend: wrong device state 0x%02x\n",
            locked);
    goto out;
  }
  if (wd_validate_pwlen(pwlen) != 0) {
    fprintf(stderr, "wd-unlocker-backend: bad key length %u\n", pwlen);
    goto out;
  }

  wd_build_read_hsb_cdb(1, cdb);
  if (sg_xfer(fd, cdb, block, sizeof(block), 0, NULL, 0, &xerr) != 0) {
    fprintf(stderr, "wd-unlocker-backend: HSB1 read failed (%s)\n",
            xerr ? strerror(xerr) : "command rejected");
    goto out;
  }
  uint32_t iter = 0;
  uint8_t salt[WD_SALT_LEN];
  if (wd_parse_hsb1(block, &iter, salt) != 0 || iter == 0) {
    fprintf(stderr, "wd-unlocker-backend: bad HSB1\n");
    goto out;
  }

  uint8_t hash[WD_HASH_LEN];
  if (wd_mk_password_block(pw, iter, salt, hash) != 0) {
    fprintf(stderr, "wd-unlocker-backend: bad password input\n");
    goto out;
  }

  uint8_t ucdb[10];
  wd_build_unlock_cdb(pwlen, ucdb);
  uint8_t payload[8 + WD_HASH_LEN];
  payload[0] = 0x45;
  payload[1] = payload[2] = payload[3] = payload[4] = payload[5] = 0x00;
  payload[6] = (pwlen >> 8) & 0xFF;
  payload[7] = pwlen & 0xFF;
  memcpy(payload + 8, hash, WD_HASH_LEN);
  wipe(hash, sizeof(hash));
  if (sg_xfer(fd, ucdb, payload, sizeof(payload), 1, NULL, 0, &xerr) != 0) {
    if (xerr)
      fprintf(stderr, "wd-unlocker-backend: unlock failed (%s)\n",
              strerror(xerr));
    else
      fprintf(stderr, "wd-unlocker-backend: wrong password\n");
    wipe(payload, sizeof(payload));
    goto out;
  }
  wipe(payload, sizeof(payload));

  // Re-read status to confirm.
  wd_build_status_cdb(cdb);
  if (sg_xfer(fd, cdb, block, sizeof(block), 0, NULL, 0, &xerr) != 0 ||
      wd_parse_status(block, &locked, NULL, NULL) != 0) {
    fprintf(stderr, "wd-unlocker-backend: verify failed (%s)\n",
            xerr ? strerror(xerr) : "command rejected");
    goto out;
  }
  if (locked == 0x02 || locked == 0x00) {
    fprintf(stderr, "wd-unlocker-backend: unlocked\n");
    rc = 0;
  } else {
    fprintf(stderr, "wd-unlocker-backend: wrong password\n");
    rc = 1;
  }

out:
  wipe(pw, sizeof(pw));
  close(fd);
  return rc;
}
