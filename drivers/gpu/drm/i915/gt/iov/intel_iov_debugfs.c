// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include <drm/drm_print.h>

#include "gt/intel_gt_debugfs.h"
#include "gt/intel_gt.h"
#include "intel_iov.h"
#include "intel_iov_utils.h"
#include "intel_iov_debugfs.h"
#include "intel_iov_event.h"
#include "intel_iov_provisioning.h"
#include "intel_iov_query.h"

static bool eval_is_pf(void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)data)->iov;

	return intel_iov_is_pf(iov);
}

static bool eval_is_vf(void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)data)->iov;

	return intel_iov_is_vf(iov);
}

static int ggtt_provisioning_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)m->private)->iov;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_ggtt(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(ggtt_provisioning);

static int ggtt_available_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)m->private)->iov;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_available_ggtt(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(ggtt_available);

static int ctxs_provisioning_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)m->private)->iov;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_ctxs(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(ctxs_provisioning);

static int dbs_provisioning_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)m->private)->iov;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_provisioning_print_dbs(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(dbs_provisioning);

static int adverse_events_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)m->private)->iov;
	struct drm_printer p = drm_seq_file_printer(m);

	return intel_iov_event_print_events(iov, &p);
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(adverse_events);

static int vf_self_config_show(struct seq_file *m, void *data)
{
	struct intel_iov *iov = &((struct intel_gt *)m->private)->iov;
	struct drm_printer p = drm_seq_file_printer(m);

	intel_iov_query_print_config(iov, &p);
	return 0;
}
DEFINE_INTEL_GT_DEBUGFS_ATTRIBUTE(vf_self_config);

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
		{ "ggtt_available", &ggtt_available_fops, eval_is_pf },
		{ "contexts_provisioning", &ctxs_provisioning_fops, eval_is_pf },
		{ "doorbells_provisioning", &dbs_provisioning_fops, eval_is_pf },
		{ "adverse_events", &adverse_events_fops, eval_is_pf },
		{ "self_config", &vf_self_config_fops, eval_is_vf },
	};
	struct dentry *dir;

	if (unlikely(!root))
		return;

	if (!intel_iov_is_enabled(iov))
		return;

	dir = debugfs_create_dir("iov", root);
	if (IS_ERR(root))
		return;

	intel_gt_debugfs_register_files(dir, files, ARRAY_SIZE(files), iov_to_gt(iov));
}
