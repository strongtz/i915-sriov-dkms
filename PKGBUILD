# Maintainer: Xilin Wu <strongtz@yeah.net>

_pkgbase=i915-sriov-dkms
pkgname=i915-sriov-dkms-git
pkgver=2024.07.19
pkgrel=1
pkgdesc="Linux i915 module patched with SR-IOV support"
arch=('x86_64')
url="https://github.com/strongtz/i915-sriov-dkms"
license=('GPL2')
depends=('dkms')
makedepends=('git')
conflicts=("${_pkgbase}")
install=${pkgname}.install
source=("git+https://github.com/strongtz/i915-sriov-dkms.git")
md5sums=('SKIP')

package() {
  cd "$srcdir/$_pkgbase"
  # Copy dkms.conf
  install -Dm644 dkms.conf "${pkgdir}"/usr/src/${_pkgbase}-${pkgver}/dkms.conf

  echo "* Copying module into /usr/src..."
  install -dm755 "${pkgdir}/usr/src/${_pkgbase}-${pkgver}"
  cp -r ${srcdir}/$_pkgbase/* "${pkgdir}/usr/src/${_pkgbase}-${pkgver}"
}
