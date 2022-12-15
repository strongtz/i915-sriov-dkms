// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/intel_gt.h"
#include "gt/intel_gt_debugfs.h"
#include "gt/uc/intel_guc_ads.h"
#include "gt/uc/intel_guc_ct.h"
#include "gt/uc/intel_guc_slpc.h"
#include "gt/uc/intel_guc_submission.h"
#include "intel_guc.h"
#include "intel_guc_debugfs.h"
#include "intel_guc_log_debugfs.h"
#include "intel_runtime_pm.h"

static int guc_info_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_is_supported(guc))
		return -ENODEV;

	intel_guc_load_status(guc, &p);
	drm_puts(&p, "\n");
	intel_guc_log_info(&guc->log, &p);

	if (!intel_guc_submission_is_used(guc))
		return 0;

	intel_guc_ct_print_info(&guc->ct, &p);
	intel_guc_submission_print_info(guc, &p);
	intel_guc_ads_print_policy_info(guc, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(guc_info);

static int guc_registered_contexts_show(struct seq_file *m, void *data)
{
	struct intel_guc *guc = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_submission_is_used(guc))
		return -ENODEV;

	intel_guc_submission_print_context_info(guc, &p);

	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(guc_registered_contexts);

static int guc_slpc_info_show(struct seq_file *m, void *unused)
{
	struct intel_guc *guc = m->private;
	struct intel_guc_slpc *slpc = &guc->slpc;
	struct drm_printer p = drm_seq_file_printer(m);

	if (!intel_guc_slpc_is_used(guc))
		return -ENODEV;

	return intel_guc_slpc_print_info(slpc, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(guc_slpc_info);

static bool intel_eval_slpc_support(void *data)
{
	struct intel_guc *guc = (struct intel_guc *)data;

	return intel_guc_slpc_is_used(guc);
}

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
static ssize_t guc_send_mmio_write(struct file *file, const char __user *user,
				   size_t count, loff_t *ppos)
{
	struct intel_guc *guc = file->private_data;
	struct intel_runtime_pm *rpm = guc_to_gt(guc)->uncore->rpm;
	u32 request[GUC_MAX_MMIO_MSG_LEN];
	u32 response[GUC_MAX_MMIO_MSG_LEN];
	intel_wakeref_t wakeref;
	int ret;

	if (*ppos)
		return 0;

	ret = from_user_to_u32array(user, count, request, ARRAY_SIZE(request));
	if (ret < 0)
		return ret;

	with_intel_runtime_pm(rpm, wakeref)
		ret = intel_guc_send_mmio(guc, request, ret, response, ARRAY_SIZE(response));
	if (ret < 0)
		return ret;

	return count;
}

static const struct file_operations guc_send_mmio_fops = {
	.write =	guc_send_mmio_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static ssize_t guc_send_ctb_write(struct file *file, const char __user *user,
				  size_t count, loff_t *ppos)
{
	struct intel_guc *guc = file->private_data;
	struct intel_runtime_pm *rpm = guc_to_gt(guc)->uncore->rpm;
	u32 request[32], response[8];	/* reasonable limits */
	intel_wakeref_t wakeref;
	int ret;

	if (*ppos)
		return 0;

	ret = from_user_to_u32array(user, count, request, ARRAY_SIZE(request));
	if (ret < 0)
		return ret;

	with_intel_runtime_pm(rpm, wakeref)
		ret = intel_guc_send_and_receive(guc, request, ret, response, ARRAY_SIZE(response));
	if (ret < 0)
		return ret;

	return count;
}

static const struct file_operations guc_send_ctb_fops = {
	.write =	guc_send_ctb_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};
#endif

void intel_guc_debugfs_register(struct intel_guc *guc, struct dentry *root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "guc_info", &guc_info_fops, NULL },
		{ "guc_registered_contexts", &guc_registered_contexts_fops, NULL },
		{ "guc_slpc_info", &guc_slpc_info_fops, &intel_eval_slpc_support},
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_GUC)
		{ "guc_send_mmio", &guc_send_mmio_fops, NULL },
		{ "guc_send_ctb", &guc_send_ctb_fops, NULL },
#endif
	};

	if (!intel_guc_is_supported(guc))
		return;

	intel_gt_debugfs_register_files(root, files, ARRAY_SIZE(files), guc);
	intel_guc_log_debugfs_register(&guc->log, root);
}
