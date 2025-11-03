# Maintainer: Xilin Wu <strongtz@yeah.net>

pkgname=i915-sriov-dkms
pkgver=2025.11.04
pkgrel=1
pkgdesc="Linux i915 module patched with SR-IOV support"
arch=('x86_64')
url="https://github.com/strongtz/i915-sriov-dkms"
license=('GPL-2.0-only')
makedepends=('git')
depends=('dkms')
conflicts=("${pkgname}-git")
backup=("etc/tmpfiles.d/i915-set-sriov-numvfs.conf")
install=${pkgname}.install
source=("$pkgname::git+file://$(pwd)/.git")
sha256sums=('SKIP')

package() {
  cd "$srcdir/$pkgname"

  echo "* Copying module into /usr/src..."
  install -dm755 "${pkgdir}/usr/src/${pkgname}-${pkgver}"
  cp -r {compat,dkms.conf,drivers,include,Makefile} "${pkgdir}/usr/src/${pkgname}-${pkgver}"
  install -Dm644 i915-set-sriov-numvfs.conf "${pkgdir}/etc/tmpfiles.d/i915-set-sriov-numvfs.conf"
}
