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
 -Wno-unused-function -Wno-unused-variable
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

ct_dma_fence_array_create_4args() {
	CODE="
	#include <linux/dma-fence-array.h>
	static void conftest_dma_fence_array_create_4args(void)
	{
		dma_fence_array_create(0, NULL, 0, 0);
	}
	"

	compile_check "$CODE" "IDB_DMA_FENCE_ARRAY_CREATE_4ARGS" 1
}

ct_pci_resize_resource_4args() {
	CODE="
	#include <linux/pci.h>
	static void conftest_pci_resize_resource_4args(void)
	{
		pci_resize_resource(NULL, 0, 0, 0);
	}
	"

	compile_check "$CODE" "IDB_PCI_RESIZE_RESOURCE_4ARGS" 1
}

ct_drm_fb_helper_alloc_info() {
	CODE="
	#include <drm/drm_fb_helper.h>
	static void conftest_drm_fb_helper_alloc_info(void)
	{
		drm_fb_helper_alloc_info((struct drm_fb_helper *)NULL);
	}
	"

	compile_check "$CODE" "IDB_HAVE_DRM_FB_HELPER_ALLOC_INFO" 1
}

ct_drm_exec_for_each_locked_object_no_index() {
	CODE="
	#include <drm/drm_exec.h>
	static void conftest_drm_exec_for_each_locked_object_no_index(void)
	{
		struct drm_exec exec;
		struct drm_gem_object *obj;

		drm_exec_for_each_locked_object(&exec, obj) {
			(void)obj;
		}
	}
	"

	compile_check "$CODE" "IDB_DRM_EXEC_FOR_EACH_LOCKED_OBJECT_NO_INDEX" 1
}

ct_drm_pagemap_compound_folio() {
	CODE="
	#include <linux/migrate.h>
	#include <linux/memremap.h>
	#include <linux/mm.h>

	static void conftest_drm_pagemap_compound_folio(struct folio *folio,
							struct dev_pagemap *pgmap,
							void *data)
	{
		unsigned long flags = MIGRATE_VMA_SELECT_COMPOUND;
		unsigned long pfn = MIGRATE_PFN_COMPOUND;

		(void)flags;
		(void)pfn;
		zone_device_folio_init(folio, pgmap, 0);
		folio_set_zone_device_data(folio, data);
		(void)folio_zone_device_data(folio);
	}

	static const struct dev_pagemap_ops conftest_pagemap_ops = {
		.folio_free = NULL,
	};
	static const void *conftest_pagemap_ops_ptr = &conftest_pagemap_ops;
	"

	compile_check "$CODE" "IDB_DRM_PAGEMAP_COMPOUND_FOLIO" 1
}

ct_xe_pmt_telem_read_kernel_device() {
	CODE="
	#include <linux/pci.h>
	#include <linux/intel_vsec.h>
	static int xe_pmt_telem_read(struct device *dev, u32 guid, u64 *data, loff_t user_offset, u32 count)
	{
		(void)dev;
		(void)guid;
		(void)data;
		(void)user_offset;
		(void)count;
		return 0;
	}

	static struct pmt_callbacks xe_pmt_cb = {
		.read_telem = xe_pmt_telem_read,
	};
	"

	compile_check "$CODE" "IDB_XE_PMT_TELEM_READ_USE_KERNEL_DEV" 1
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
