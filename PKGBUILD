# Maintainer: yongabyte <yongabyte@users.noreply.github.com>
pkgname=wd-passport-kdialog-unlocker
pkgver=1.0.0
pkgrel=11
pkgdesc="Automated KDE graphical unlocker for hardware-encrypted WD My Passport drives"
arch=('x86_64')
url="https://github.com/yongabyte/wd-passport-plasma-unlocker"
license=('GPL-3.0-or-later')
depends=('kdialog' 'kde-cli-tools' 'bash' 'openssl')
makedepends=('gcc' 'libcap')
checkdepends=('criterion')

source=(
  "wd-unlocker.sh"
  "wd-passport-solid-action.desktop"
  "70-wd-passport.rules"
  "wd-passport-unlocker.service"
)
sha256sums=('SKIP' 'SKIP' 'SKIP' 'SKIP')

build() {
  # C backend lives in ./backend alongside this PKGBUILD (committed to AUR git).
  make -C "${startdir}/backend" all
}

check() {
  make -C "${startdir}/backend" test
}

package() {
  # 1. Install your dynamic frontend automation script
  install -Dm755 "${srcdir}/wd-unlocker.sh" "${pkgdir}/usr/bin/wd-passport-unlocker"

  # 2. Install the unlock-only C backend built above (stdin password, SG_IO)
  install -Dm755 "${startdir}/backend/wd-unlocker-backend" "${pkgdir}/usr/bin/wd-unlocker-backend"
  # Vendor SCSI opcodes (C0/C1/D8) are filtered by the kernel without
  # CAP_SYS_RAWIO. Grant it as a file capability: no setuid, no sudo at
  # runtime; the binary allowlists whole-disk /dev/sdX + WD serials only.
  setcap cap_sys_rawio+ep "${pkgdir}/usr/bin/wd-unlocker-backend"

  # 3. Install the Device Notifier solid action: "Unlock WD My Passport"
  # appears on the drive entry only while a matching USB disk is attached.
  install -Dm644 "${srcdir}/wd-passport-solid-action.desktop" "${pkgdir}/usr/share/solid/actions/wd-passport-solid-action.desktop"

  # 4. Install the hardware detection udev rule
  install -Dm644 "${srcdir}/70-wd-passport.rules" "${pkgdir}/usr/lib/udev/rules.d/70-wd-passport.rules"

  # 5. Install the systemd user unit (device-activated via
  # SYSTEMD_USER_WANTS; runs rootless in the user manager)
  install -Dm644 "${srcdir}/wd-passport-unlocker.service" "${pkgdir}/usr/lib/systemd/user/wd-passport-unlocker.service"
}
