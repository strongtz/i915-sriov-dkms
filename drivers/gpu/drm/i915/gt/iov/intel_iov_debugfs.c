// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "intel_iov.h"
#include "intel_iov_utils.h"
#include "intel_iov_debugfs.h"
#include "intel_iov_event.h"
#include "intel_iov_provisioning.h"
#include "intel_iov_query.h"
#include "intel_iov_relay.h"

static bool eval_is_pf(void *data)
{
	struct intel_iov *iov = data;

	return intel_iov_is_pf(iov);
}

static bool eval_is_vf(void *data)
{
	struct intel_iov *iov = data;

	return intel_iov_is_vf(iov);
}

static int ggtt_provisioning_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_ggtt(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(ggtt_provisioning);

static int ctxs_provisioning_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_ctxs(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(ctxs_provisioning);

static int dbs_provisioning_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_dbs(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(dbs_provisioning);

static int adverse_events_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_event_print_events(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(adverse_events);

static int vf_self_config_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = m->private;
	struct drm_printer p = drm_seq_file_printer(m);

	intel_iov_query_print_config(iov, &p);
	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(vf_self_config);

#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_IOV)

#define RELAY_MAX_LEN 60

static ssize_t relay_to_vf_write(struct file *file, const char __user *user,
				 size_t count, loff_t *ppos)
{
	struct intel_iov *iov = file->private_data;
	struct intel_runtime_pm *rpm = iov_to_gt(iov)->uncore->rpm;
	intel_wakeref_t wakeref;
	u32 message[1 + RELAY_MAX_LEN];	/* target + message */
	u32 reply[RELAY_MAX_LEN];
	int ret;

	if (*ppos)
		return 0;

	ret = from_user_to_u32array(user, count, message, ARRAY_SIZE(message));
	if (ret < 0)
		return ret;

	if (ret < 1 + GUC_HXG_MSG_MIN_LEN)
		return -EINVAL;

	if (message[0] == PFID)
		return -EINVAL;

	with_intel_runtime_pm(rpm, wakeref)
		ret = intel_iov_relay_send_to_vf(&iov->relay, message[0],
						 message + 1, ret - 1,
						 reply, ARRAY_SIZE(reply));
	if (ret < 0)
		return ret;

	return count;
}

static const struct file_operations relay_to_vf_fops = {
	.write =	relay_to_vf_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static ssize_t relay_to_pf_write(struct file *file, const char __user *user,
				 size_t count, loff_t *ppos)
{
	struct intel_iov *iov = file->private_data;
	struct intel_runtime_pm *rpm = iov_to_gt(iov)->uncore->rpm;
	intel_wakeref_t wakeref;
	u32 message[RELAY_MAX_LEN];
	u32 reply[RELAY_MAX_LEN];
	int ret;

	if (*ppos)
		return 0;

	ret = from_user_to_u32array(user, count, message, ARRAY_SIZE(message));
	if (ret < 0)
		return ret;

	if (ret < GUC_HXG_MSG_MIN_LEN)
		return -EINVAL;

	with_intel_runtime_pm(rpm, wakeref)
		ret = intel_iov_relay_send_to_pf(&iov->relay, message, ret,
						 reply, ARRAY_SIZE(reply));
	if (ret < 0)
		return ret;

	return count;
}

static const struct file_operations relay_to_pf_fops = {
	.write =	relay_to_pf_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

static ssize_t relocate_ggtt_write(struct file *file, const char __user *user,
				   size_t count, loff_t *ppos)
{
	struct intel_iov *iov = file->private_data;
	u32 vfid;
	int ret;

	if (*ppos)
		return 0;

	ret = kstrtou32_from_user(user, count, 0, &vfid);
	if (ret < 0)
		return ret;

	if (!vfid || vfid > pf_get_totalvfs(iov))
		return -EINVAL;

	ret = intel_iov_provisioning_move_ggtt(iov, vfid);
	if (ret < 0)
		return ret;

	return count;
}

static const struct file_operations relocate_ggtt_fops = {
	.write =	relocate_ggtt_write,
	.open =		simple_open,
	.llseek =	default_llseek,
};

#endif /* CONFIG_DRM_I915_DEBUG_IOV */

/**
 * intel_iov_debugfs_register - Register IOV specific entries in GT debugfs.
 * @iov: the IOV struct
 * @root: the GT debugfs root directory entry
 *
 * Some IOV entries are GT related so better to show them under GT debugfs.
 */
void intel_iov_debugfs_register(struct intel_iov *iov, struct dentry *root)
{
	static const struct intel_gt_debugfs_file files[] = {
		{ "ggtt_provisioning", &ggtt_provisioning_fops, eval_is_pf },
		{ "contexts_provisioning", &ctxs_provisioning_fops, eval_is_pf },
		{ "doorbells_provisioning", &dbs_provisioning_fops, eval_is_pf },
		{ "adverse_events", &adverse_events_fops, eval_is_pf },
		{ "self_config", &vf_self_config_fops, eval_is_vf },
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG_IOV)
		{ "relay_to_vf", &relay_to_vf_fops, eval_is_pf },
		{ "relay_to_pf", &relay_to_pf_fops, eval_is_vf },
		{ "relocate_ggtt", &relocate_ggtt_fops, eval_is_pf },
#endif
	};
	struct dentry *dir;

	if (unlikely(!root))
		return;

	if (!intel_iov_is_enabled(iov))
		return;

	dir = debugfs_create_dir("iov", root);
	if (IS_ERR(root))
		return;

	intel_gt_debugfs_register_files(dir, files, ARRAY_SIZE(files), iov);
}
