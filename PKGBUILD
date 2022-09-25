# Maintainer: Xilin Wu <strongtz@yeah.net>

_pkgbase=i915-sriov
pkgname=i915-sriov-dkms-git
pkgver=5.15.49
pkgrel=1
pkgdesc="Linux i915 module patched with SR-IOV support"
arch=('x86_64')
url="https://github.com/strongtz/i915-sriov-dkms"
license=('GPL2')
depends=('dkms')
conflicts=("${_pkgbase}")
install=${pkgname}.install
source=("git+https://github.com/strongtz/i915-sriov-dkms.git")
md5sums=('SKIP')

package() {
  # Install
  make DESTDIR="${pkgdir}" install

  # Copy dkms.conf
  install -Dm644 dkms.conf "${pkgdir}"/usr/src/${_pkgbase}-${pkgver}/dkms.conf

  # Set name and version
  sed -e "s/@_PKGBASE@/${_pkgbase}/" \
      -e "s/@PKGVER@/${pkgver}/" \
      -i "${pkgdir}"/usr/src/${_pkgbase}-${pkgver}/dkms.conf

  # Copy sources (including Makefile)
  cp -r ${_pkgbase}/* "${pkgdir}"/usr/src/${_pkgbase}-${pkgver}/
}
