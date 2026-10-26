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
		_compiled=1
	else
		_compiled=0
	fi
	rm -f "${TMP}.c" "${TMP}.o"

	if [ "${_compiled}" = "1" ]; then
		idb_define "${_def}" "${_val}"
	else
		idb_undef "${_def}"
	fi
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

ct_drm_client_dev_suspend_one_arg() {
	CODE="
	#include <drm/drm_client_event.h>
	static void conftest_drm_client_dev_suspend_one_arg(void)
	{
		drm_client_dev_suspend((struct drm_device *)NULL);
		drm_client_dev_resume((struct drm_device *)NULL);
	}
	"

	compile_check "$CODE" "IDB_DRM_CLIENT_DEV_SUSPEND_ONE_ARG" 1
}

ct_drm_client_funcs_restore_one_arg() {
	CODE="
	#include <drm/drm_client.h>
	static int conftest_restore(struct drm_client_dev *client)
	{
		(void)client;
		return 0;
	}
	static const struct drm_client_funcs conftest_client_funcs = {
		.restore = conftest_restore,
	};
	"

	compile_check "$CODE" "IDB_DRM_CLIENT_FUNCS_RESTORE_ONE_ARG" 1
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

ct_drm_fb_helper_restore_fbdev_mode_unlocked_one_arg() {
	CODE="
	#include <drm/drm_fb_helper.h>
	static int conftest_restore_fbdev_mode(void)
	{
		return drm_fb_helper_restore_fbdev_mode_unlocked(
			(struct drm_fb_helper *)NULL);
	}
	"

	compile_check "$CODE" "IDB_DRM_FB_HELPER_RESTORE_FBDEV_MODE_ONE_ARG" 1
}

ct_drm_sched_init_args() {
	CODE="
	#include <drm/gpu_scheduler.h>
	static int conftest_drm_sched_init_args(struct drm_gpu_scheduler *sched,
						const struct drm_sched_backend_ops *ops,
						struct workqueue_struct *submit_wq,
						struct workqueue_struct *timeout_wq,
						atomic_t *score, struct device *dev)
	{
		const struct drm_sched_init_args args = {
			.ops = ops,
			.submit_wq = submit_wq,
			.num_rqs = 1,
			.credit_limit = 1,
			.hang_limit = 0,
			.timeout = 0,
			.timeout_wq = timeout_wq,
			.score = score,
			.name = NULL,
			.dev = dev,
		};

		return drm_sched_init(sched, &args);
	}
	"

	compile_check "$CODE" "IDB_DRM_SCHED_INIT_ARGS" 1
}

ct_drm_sched_job_init_5args() {
	CODE="
	#include <drm/gpu_scheduler.h>
	static void conftest_drm_sched_job_init_5args(void)
	{
		struct drm_sched_job job;
		struct drm_sched_entity entity;
		drm_sched_job_init(&job, &entity, 0, NULL, 0);
	}
	"

	compile_check "$CODE" "IDB_DRM_SCHED_JOB_INIT_5ARGS" 1
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

ct_xe_pmt_telem_read_4args() {
	CODE="
	#include <linux/pci.h>
	#include <linux/intel_vsec.h>
	static int conftest_pmt_read_telem(struct pci_dev *pdev, u32 guid,
					   u64 *data, u32 count)
	{
		(void)pdev;
		(void)guid;
		(void)data;
		(void)count;
		return 0;
	}

	static struct pmt_callbacks conftest_pmt_cb = {
		.read_telem = conftest_pmt_read_telem,
	};
	"

	compile_check "$CODE" "IDB_XE_PMT_TELEM_READ_4ARGS" 1
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
