// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "intel_iov_provisioning.h"
#include "intel_iov_state.h"
#include "intel_iov_sysfs.h"
#include "intel_iov_types.h"
#include "intel_iov_utils.h"

/*
 * /sys/class/drm/card*
 * └── iov
 *     ├── pf/
 *     │   └── gt/
 *     │       └── ...
 *     ├── vf1/
 *     │   └── gt/
 *     │       └── ...
 */

#define IOV_KOBJ_GT_NAME "gt"

struct iov_kobj {
	struct kobject base;
	struct intel_iov *iov;
};
#define to_iov_kobj(x) container_of(x, struct iov_kobj, base)

static struct intel_iov *kobj_to_iov(struct kobject *kobj)
{
	return to_iov_kobj(kobj)->iov;
}

static unsigned int kobj_to_id(struct kobject *kobj)
{
	return to_sriov_ext_kobj(kobj->parent)->id;
}

struct iov_attr {
	struct attribute attr;
	ssize_t (*show)(struct intel_iov *iov, unsigned int id, char *buf);
	ssize_t (*store)(struct intel_iov *iov, unsigned int id,
			 const char *buf, size_t count);
};
#define to_iov_attr(x) container_of(x, struct iov_attr, attr)

#define IOV_ATTR(name) \
static struct iov_attr name##_iov_attr = \
	__ATTR(name, 0644, name##_iov_attr_show, name##_iov_attr_store)

#define IOV_ATTR_RO(name) \
static struct iov_attr name##_iov_attr = \
	__ATTR(name, 0444, name##_iov_attr_show, NULL)

/* common attributes */

static ssize_t contexts_quota_iov_attr_show(struct intel_iov *iov,
					    unsigned int id, char *buf)
{
	u16 num_ctxs = intel_iov_provisioning_get_ctxs(iov, id);

	return sysfs_emit(buf, "%hu\n", num_ctxs);
}

static ssize_t contexts_quota_iov_attr_store(struct intel_iov *iov,
					     unsigned int id,
					     const char *buf, size_t count)
{
	u16 num_ctxs;
	int err;

	err = kstrtou16(buf, 0, &num_ctxs);
	if (err)
		return err;

	err = intel_iov_provisioning_set_ctxs(iov, id, num_ctxs);
	return err ?: count;
}

static ssize_t doorbells_quota_iov_attr_show(struct intel_iov *iov,
					     unsigned int id, char *buf)
{
	u16 num_dbs = intel_iov_provisioning_get_dbs(iov, id);

	return sysfs_emit(buf, "%hu\n", num_dbs);
}

static ssize_t doorbells_quota_iov_attr_store(struct intel_iov *iov,
					      unsigned int id,
					      const char *buf, size_t count)
{
	u16 num_dbs;
	int err;

	err = kstrtou16(buf, 0, &num_dbs);
	if (err)
		return err;

	err = intel_iov_provisioning_set_dbs(iov, id, num_dbs);
	return err ?: count;
}

static ssize_t exec_quantum_ms_iov_attr_show(struct intel_iov *iov,
					     unsigned int id, char *buf)
{
	u32 exec_quantum = intel_iov_provisioning_get_exec_quantum(iov, id);

	return sysfs_emit(buf, "%u\n", exec_quantum);
}

static ssize_t exec_quantum_ms_iov_attr_store(struct intel_iov *iov,
					      unsigned int id,
					      const char *buf, size_t count)
{
	u32 exec_quantum;
	int err;

	err = kstrtou32(buf, 0, &exec_quantum);
	if (err)
		return err;

	err = intel_iov_provisioning_set_exec_quantum(iov, id, exec_quantum);
	return err ?: count;
}

static ssize_t preempt_timeout_us_iov_attr_show(struct intel_iov *iov,
						unsigned int id, char *buf)
{
	u32 preempt_timeout = intel_iov_provisioning_get_preempt_timeout(iov, id);

	return sysfs_emit(buf, "%u\n", preempt_timeout);
}

static ssize_t preempt_timeout_us_iov_attr_store(struct intel_iov *iov,
						 unsigned int id,
						 const char *buf, size_t count)
{
	u32 preempt_timeout;
	int err;

	err = kstrtou32(buf, 0, &preempt_timeout);
	if (err)
		return err;

	err = intel_iov_provisioning_set_preempt_timeout(iov, id, preempt_timeout);
	return err ?: count;
}

IOV_ATTR(contexts_quota);
IOV_ATTR(doorbells_quota);
IOV_ATTR(exec_quantum_ms);
IOV_ATTR(preempt_timeout_us);

static struct attribute *iov_attrs[] = {
	&contexts_quota_iov_attr.attr,
	&doorbells_quota_iov_attr.attr,
	&exec_quantum_ms_iov_attr.attr,
	&preempt_timeout_us_iov_attr.attr,
	NULL
};

static const struct attribute_group iov_attr_group = {
	.attrs = iov_attrs,
};

static const struct attribute_group *default_iov_attr_groups[] = {
	&iov_attr_group,
	NULL
};

/* PF only attributes */

static ssize_t ggtt_free_iov_attr_show(struct intel_iov *iov,
				       unsigned int id, char *buf)
{
	GEM_WARN_ON(id);
	return sysfs_emit(buf, "%llu\n", intel_iov_provisioning_query_free_ggtt(iov));
}

static ssize_t ggtt_max_quota_iov_attr_show(struct intel_iov *iov,
					    unsigned int id, char *buf)
{
	GEM_WARN_ON(id);
	return sysfs_emit(buf, "%llu\n", intel_iov_provisioning_query_max_ggtt(iov));
}

static ssize_t contexts_free_iov_attr_show(struct intel_iov *iov, unsigned int id, char *buf)
{
	GEM_WARN_ON(id);
	return sysfs_emit(buf, "%hu\n", intel_iov_provisioning_query_free_ctxs(iov));
}

static ssize_t contexts_max_quota_iov_attr_show(struct intel_iov *iov, unsigned int id, char *buf)
{
	GEM_WARN_ON(id);
	return sysfs_emit(buf, "%hu\n", intel_iov_provisioning_query_max_ctxs(iov));
}

static ssize_t doorbells_free_iov_attr_show(struct intel_iov *iov,
					    unsigned int id, char *buf)
{
	GEM_WARN_ON(id);
	return sysfs_emit(buf, "%hu\n", intel_iov_provisioning_query_free_dbs(iov));
}

static ssize_t doorbells_max_quota_iov_attr_show(struct intel_iov *iov,
						 unsigned int id, char *buf)
{
	GEM_WARN_ON(id);
	return sysfs_emit(buf, "%hu\n", intel_iov_provisioning_query_max_dbs(iov));
}

static ssize_t sched_if_idle_iov_attr_show(struct intel_iov *iov,
					   unsigned int id, char *buf)
{
	u32 value = intel_iov_provisioning_get_sched_if_idle(iov);

	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t sched_if_idle_iov_attr_store(struct intel_iov *iov,
					    unsigned int id,
					    const char *buf, size_t count)
{
	bool value;
	int err;

	err = kstrtobool(buf, &value);
	if (err)
		return err;

	err = intel_iov_provisioning_set_sched_if_idle(iov, value);
	return err ?: count;
}

static ssize_t engine_reset_iov_attr_show(struct intel_iov *iov,
					  unsigned int id, char *buf)
{
	u32 value = intel_iov_provisioning_get_reset_engine(iov);

	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t engine_reset_iov_attr_store(struct intel_iov *iov,
					   unsigned int id,
					   const char *buf, size_t count)
{
	bool value;
	int err;

	err = kstrtobool(buf, &value);
	if (err)
		return err;

	err = intel_iov_provisioning_set_reset_engine(iov, value);
	return err ?: count;
}

static ssize_t sample_period_ms_iov_attr_show(struct intel_iov *iov,
					      unsigned int id, char *buf)
{
	u32 value = intel_iov_provisioning_get_sample_period(iov);

	return sysfs_emit(buf, "%u\n", value);
}

static ssize_t sample_period_ms_iov_attr_store(struct intel_iov *iov,
					       unsigned int id,
					       const char *buf, size_t count)
{
	u32 value;
	int err;

	err = kstrtou32(buf, 0, &value);
	if (err)
		return err;

	err = intel_iov_provisioning_set_sample_period(iov, value);
	return err ?: count;
}

IOV_ATTR_RO(ggtt_free);
IOV_ATTR_RO(ggtt_max_quota);
IOV_ATTR_RO(contexts_free);
IOV_ATTR_RO(contexts_max_quota);
IOV_ATTR_RO(doorbells_free);
IOV_ATTR_RO(doorbells_max_quota);

IOV_ATTR(sched_if_idle);
IOV_ATTR(engine_reset);
IOV_ATTR(sample_period_ms);

static struct attribute *pf_attrs[] = {
	NULL
};

static const struct attribute_group pf_attr_group = {
	.attrs = pf_attrs,
};

static struct attribute *pf_available_attrs[] = {
	&ggtt_free_iov_attr.attr,
	&ggtt_max_quota_iov_attr.attr,
	&contexts_free_iov_attr.attr,
	&contexts_max_quota_iov_attr.attr,
	&doorbells_free_iov_attr.attr,
	&doorbells_max_quota_iov_attr.attr,
	NULL
};

static const struct attribute_group pf_available_attr_group = {
	.name = "available",
	.attrs = pf_available_attrs,
};

static struct attribute *pf_policies_attrs[] = {
	&sched_if_idle_iov_attr.attr,
	&engine_reset_iov_attr.attr,
	&sample_period_ms_iov_attr.attr,
	NULL
};

static const struct attribute_group pf_policies_attr_group = {
	.name = "policies",
	.attrs = pf_policies_attrs,
};

static const struct attribute_group *pf_attr_groups[] = {
	&pf_attr_group,
	&pf_available_attr_group,
	&pf_policies_attr_group,
	NULL
};

/* VFs only attributes */

static ssize_t ggtt_quota_iov_attr_show(struct intel_iov *iov,
					unsigned int id, char *buf)
{
	u64 size = intel_iov_provisioning_get_ggtt(iov, id);

	return sysfs_emit(buf, "%llu\n", size);
}

static ssize_t ggtt_quota_iov_attr_store(struct intel_iov *iov,
					 unsigned int id,
					 const char *buf, size_t count)
{
	u64 size;
	int err;

	err = kstrtou64(buf, 0, &size);
	if (err)
		return err;

	err = intel_iov_provisioning_set_ggtt(iov, id, size);
	return err ?: count;
}

IOV_ATTR(ggtt_quota);

static struct attribute *vf_attrs[] = {
	&ggtt_quota_iov_attr.attr,
	NULL
};

#define __iov_threshold_to_attr_impl(K, N, A) \
static ssize_t A##_iov_attr_show(struct intel_iov *iov, unsigned int id, char *buf)	\
{											\
	u32 value = intel_iov_provisioning_get_threshold(iov, id, IOV_THRESHOLD_##K);	\
											\
	return sysfs_emit(buf, "%u\n", value);						\
}											\
											\
static ssize_t A##_iov_attr_store(struct intel_iov *iov, unsigned int id,		\
				  const char *buf, size_t count)			\
{											\
	u32 value;									\
	int err;									\
											\
	err = kstrtou32(buf, 0, &value);						\
	if (err)									\
		return err;								\
											\
	err = intel_iov_provisioning_set_threshold(iov, id, IOV_THRESHOLD_##K, value);	\
	return err ?: count;								\
}											\
											\
IOV_ATTR(A);

IOV_THRESHOLDS(__iov_threshold_to_attr_impl)
#undef __iov_threshold_to_attr_impl

static struct attribute *vf_threshold_attrs[] = {
#define __iov_threshold_to_attr_list(K, N, A) \
	&A##_iov_attr.attr,
	IOV_THRESHOLDS(__iov_threshold_to_attr_list)
#undef __iov_threshold_to_attr_list
	NULL
};

static ssize_t bin_attr_state_read(struct file *filp, struct kobject *kobj,
				   struct bin_attribute *bin_attr, char *buf,
				   loff_t off, size_t count)
{
	struct intel_iov *iov = kobj_to_iov(kobj);
	unsigned int id = kobj_to_id(kobj);
	int err;

	if (off > 0 || count < SZ_4K)
		return -EINVAL;

	err = intel_iov_state_save_vf(iov, id, buf);
	if (unlikely(err))
		return err;

	return SZ_4K;
}

static ssize_t bin_attr_state_write(struct file *filp, struct kobject *kobj,
				    struct bin_attribute *bin_attr, char *buf,
				    loff_t off, size_t count)
{
	struct intel_iov *iov = kobj_to_iov(kobj);
	unsigned int id = kobj_to_id(kobj);
	int err;

	if (off > 0 || count < SZ_4K)
		return -EINVAL;

	err = intel_iov_state_restore_vf(iov, id, buf);
	if (unlikely(err))
		return err;

	return count;
}

static BIN_ATTR(state, 0600, bin_attr_state_read, bin_attr_state_write, SZ_4K);

static struct bin_attribute *vf_bin_attrs[] = {
	&bin_attr_state,
	NULL
};

static const struct attribute_group vf_attr_group = {
	.attrs = vf_attrs,
	.bin_attrs = vf_bin_attrs,
};

static const struct attribute_group vf_threshold_attr_group = {
	.name = "threshold",
	.attrs = vf_threshold_attrs,
};

static const struct attribute_group *vf_attr_groups[] = {
	&vf_attr_group,
	&vf_threshold_attr_group,
	NULL
};

static const struct attribute_group **iov_attr_groups(unsigned int id)
{
	return id ? vf_attr_groups : pf_attr_groups;
}

/* no user serviceable parts below */

static ssize_t iov_attr_show(struct kobject *kobj,
			     struct attribute *attr, char *buf)
{
	struct iov_attr *iov_attr = to_iov_attr(attr);
	struct intel_iov *iov = kobj_to_iov(kobj);
	unsigned int id = kobj_to_id(kobj);

	return iov_attr->show ? iov_attr->show(iov, id, buf) : -EIO;
}

static ssize_t iov_attr_store(struct kobject *kobj,
			      struct attribute *attr,
			      const char *buf, size_t count)
{
	struct iov_attr *iov_attr = to_iov_attr(attr);
	struct intel_iov *iov = kobj_to_iov(kobj);
	unsigned int id = kobj_to_id(kobj);

	return iov_attr->store ? iov_attr->store(iov, id, buf, count) : -EIO;
}

static const struct sysfs_ops iov_sysfs_ops = {
	.show = iov_attr_show,
	.store = iov_attr_store,
};

static struct kobject *iov_kobj_alloc(struct intel_iov *iov)
{
	struct iov_kobj *iov_kobj;

	iov_kobj = kzalloc(sizeof(*iov_kobj), GFP_KERNEL);
	if (!iov_kobj)
		return NULL;

	iov_kobj->iov = iov;

	return &iov_kobj->base;
}

static void iov_kobj_release(struct kobject *kobj)
{
	struct iov_kobj *iov_kobj = to_iov_kobj(kobj);

	kfree(iov_kobj);
}

static struct kobj_type iov_ktype = {
	.release = iov_kobj_release,
	.sysfs_ops = &iov_sysfs_ops,
	.default_groups = default_iov_attr_groups,
};

static int pf_setup_provisioning(struct intel_iov *iov)
{
	struct i915_sriov_ext_kobj **parents = iov_to_i915(iov)->sriov.pf.sysfs.kobjs;
	struct kobject **kobjs;
	struct kobject *kobj;
	unsigned int count = 1 + pf_get_totalvfs(iov);
	unsigned int n;
	int err;

	GEM_BUG_ON(!intel_iov_is_pf(iov));

	if (!parents) {
		err = -ENODEV;
		goto failed;
	}

	err = i915_inject_probe_error(iov_to_i915(iov), -ENOMEM);
	if (unlikely(err))
		goto failed;

	kobjs = kcalloc(count, sizeof(*kobjs), GFP_KERNEL);
	if (unlikely(!kobjs)) {
		err = -ENOMEM;
		goto failed;
	}

	for (n = 0; n < count; n++) {
		struct kobject *parent;

		err = i915_inject_probe_error(iov_to_i915(iov), -ENOMEM);
		if (unlikely(err)) {
			kobj = NULL;
			goto failed_kobj_n;
		}

		kobj = iov_kobj_alloc(iov);
		if (unlikely(!kobj)) {
			err = -ENOMEM;
			goto failed_kobj_n;
		}

		parent = &parents[n]->base;

		err = kobject_init_and_add(kobj, &iov_ktype, parent, IOV_KOBJ_GT_NAME);
		if (unlikely(err))
			goto failed_kobj_n;

		err = i915_inject_probe_error(iov_to_i915(iov), -EEXIST);
		if (unlikely(err))
			goto failed_kobj_n;

		err = sysfs_create_groups(kobj, iov_attr_groups(n));
		if (unlikely(err))
			goto failed_kobj_n;

		kobjs[n] = kobj;
	}

	GEM_BUG_ON(iov->pf.sysfs.entries);
	iov->pf.sysfs.entries = kobjs;
	return 0;

failed_kobj_n:
	if (kobj)
		kobject_put(kobj);
	while (n--) {
		sysfs_remove_groups(kobjs[n], iov_attr_groups(n));
		kobject_put(kobjs[n]);
	}
	kfree(kobjs);
failed:
	return err;
}

static void pf_teardown_provisioning(struct intel_iov *iov)
{
	struct kobject **kobjs;
	unsigned int count = 1 + pf_get_totalvfs(iov);
	unsigned int n;

	kobjs = fetch_and_zero(&iov->pf.sysfs.entries);
	if (!kobjs)
		return;

	for (n = 0; n < count; n++) {
		sysfs_remove_groups(kobjs[n], iov_attr_groups(n));
		kobject_put(kobjs[n]);
	}

	kfree(kobjs);
}

/**
 * intel_iov_sysfs_setup - Setup GT IOV sysfs.
 * @iov: the IOV struct
 *
 * Setup GT IOV provisioning sysfs.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int intel_iov_sysfs_setup(struct intel_iov *iov)
{
	int err;

	if (!intel_iov_is_pf(iov))
		return 0;

	err = pf_setup_provisioning(iov);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	IOV_PROBE_ERROR(iov, "Failed to setup sysfs (%pe)\n", ERR_PTR(err));
	return err;
}

/**
 * intel_iov_sysfs_teardown - Cleanup GT IOV sysfs.
 * @iov: the IOV struct
 *
 * Remove GT IOV provisioning sysfs.
 */
void intel_iov_sysfs_teardown(struct intel_iov *iov)
{
	if (!intel_iov_is_pf(iov))
		return;

	pf_teardown_provisioning(iov);
}
