#!/bin/sh
#
# conftest.sh — Intel DRM Backport kernel compatibility probe
#
# References:
#   - https://github.com/NVIDIA/open-gpu-kernel-modules/blob/main/kernel-open/conftest.sh
set -e

if [ -z "${KBUILD_SRC}${srctree}" ] && [ -z "${LINUXINCLUDE}" ]; then
	echo "conftest.sh: ERROR: must be run from within a KBUILD recipe." >&2
	echo "  (LINUXINCLUDE / srctree are not set)" >&2
	exit 1
fi

: "${CC:?conftest.sh: CC not set}"
: "${LINUXINCLUDE:?conftest.sh: LINUXINCLUDE not set}"
: "${KBUILD_CFLAGS:?conftest.sh: KBUILD_CFLAGS not set}"

CFLAGS="$NOSTDINC_FLAGS $LINUXINCLUDE $KBUILD_CPPFLAGS $KBUILD_CFLAGS $KBUILD_MODFLAGS \
 -DKBUILD_BASENAME=\"conftest\" -DKBUILD_MODNAME=\"conftest\" \
 -Werror=implicit-function-declaration -Wno-missing-prototypes 
 -Wno-unused-function 
"

TMPDIR="${KBUILD_EXTMOD:-.}/conftest_tmp"
mkdir -p "${TMPDIR}"
TMP="${TMPDIR}/ct_$$"

cleanup() {
	rm -f "${TMP}.c" "${TMP}.o"
	rm -d "${TMPDIR}" 2>/dev/null || true
}
trap cleanup EXIT

PREAMBLE='
#include <linux/version.h>
'

idb_define() {
	echo "#define ${1} ${2}"
}

idb_undef() {
	echo "#undef ${1}"
}

compile_check() {
	_code="$1"
	_def="$2"
	_val="${3:-1}"

	printf '%s\n%s\n' "${PREAMBLE}" "${_code}" >"${TMP}.c"

	if ${CC} ${CFLAGS} -c -o "${TMP}.o" "${TMP}.c" >/dev/null 2>&1; then
		idb_define "${_def}" "${_val}"
	else
		idb_undef "${_def}"
	fi

	rm -f "${TMP}.c" "${TMP}.o"
}

ACTION="$1"
shift

ct_copy_from_user_inatomic_nontemporal() {
	CODE="
	#include <linux/uaccess.h>
	static void conftest_copy_from_user_inatomic_nontemporal(void)
	{
		copy_from_user_inatomic_nontemporal((void __force *)NULL, NULL, 0);
	}
	"

	compile_check "$CODE" "IDB_COPY_FROM_USER_INATOMIC_NONTEMPORAL" 1
}

case "${ACTION}" in
compile_test)
	for TEST in "$@"; do
		_fn="ct_${TEST}"
		if type "${_fn}" >/dev/null 2>&1; then
			"${_fn}"
		else
			echo "conftest.sh: WARNING: unknown compile test '${TEST}'" >&2
		fi
	done
	;;

*)
	echo "conftest.sh: ERROR: unknown action '${ACTION}'" >&2
	echo "  usage: conftest.sh compile_test [names...]" >&2
	exit 1
	;;
esac

exit 0
