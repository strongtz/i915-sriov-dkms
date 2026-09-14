# Maintainer: Xilin Wu <strongtz@yeah.net>

pkgname=i915-sriov-dkms
pkgver=2026.08.12.1
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
_source_repo="$(cd "$(git rev-parse --git-common-dir)" && pwd -P)"
_source_commit="$(git rev-parse HEAD)"
source=("$pkgname::git+file://${_source_repo}#commit=${_source_commit}")
sha256sums=('SKIP')

package() {
  cd "$srcdir/$pkgname"

  echo "* Copying module into /usr/src..."
  install -dm755 "${pkgdir}/usr/src/${pkgname}-${pkgver}"
  cp -r {compat,dkms.conf,drivers,include,Makefile} "${pkgdir}/usr/src/${pkgname}-${pkgver}"
  install -Dm755 conftest.sh "${pkgdir}/usr/src/${pkgname}-${pkgver}/conftest.sh"
  install -Dm644 i915-set-sriov-numvfs.conf "${pkgdir}/etc/tmpfiles.d/i915-set-sriov-numvfs.conf"
}
