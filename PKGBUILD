# Maintainer: Xilin Wu <strongtz@yeah.net>

pkgname=i915-sriov-dkms
pkgver=2025.10.09
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
source=("$pkgname::git+file://$(pwd)/.git" "i915-set-sriov-numvfs.conf")
sha256sums=('SKIP'
            'e85e4d4c97cb1f6e825c47ea5e3a9c18f10761714307985f67b58c8e55a1e2c2')

package() {
  cd "$srcdir/$pkgname"

  echo "* Copying module into /usr/src..."
  install -dm755 "${pkgdir}/usr/src/${pkgname}-${pkgver}"
  cp -r ${srcdir}/$pkgname/{compat,dkms.conf,drivers,include,Makefile} "${pkgdir}/usr/src/${pkgname}-${pkgver}"
  cd "$srcdir"
  install -Dm644 i915-set-sriov-numvfs.conf "${pkgdir}/etc/tmpfiles.d/i915-set-sriov-numvfs.conf"
}
