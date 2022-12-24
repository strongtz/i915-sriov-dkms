// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_sriov_sysfs.h"
#include "i915_sriov_sysfs_types.h"
#include "i915_sysfs.h"

#include "gt/iov/intel_iov_provisioning.h"
#include "gt/iov/intel_iov_state.h"

/*
 * /sys/class/drm/card*
 * └── iov/
 *     ├── ...
 *     ├── pf/
 *     │   └── ...
 *     ├── vf1/
 *     │   └── ...
 */

#define SRIOV_KOBJ_HOME_NAME "iov"
#define SRIOV_EXT_KOBJ_PF_NAME "pf"
#define SRIOV_EXT_KOBJ_VFn_NAME "vf%u"
#define SRIOV_DEVICE_LINK_NAME "device"

struct drm_i915_private *sriov_kobj_to_i915(struct i915_sriov_kobj *kobj)
{
	struct device *kdev = kobj_to_dev(kobj->base.parent);
	struct drm_i915_private *i915 = kdev_minor_to_i915(kdev);

	return i915;
}

struct drm_i915_private *sriov_ext_kobj_to_i915(struct i915_sriov_ext_kobj *kobj)
{
	return sriov_kobj_to_i915(to_sriov_kobj(kobj->base.parent));
}

static inline bool sriov_ext_kobj_is_pf(struct i915_sriov_ext_kobj *kobj)
{
	return !kobj->id;
}

/* core SR-IOV attributes */

static ssize_t mode_sriov_attr_show(struct drm_i915_private *i915, char *buf)
{
	return sysfs_emit(buf, "%s\n", i915_iov_mode_to_string(IOV_MODE(i915)));
}

I915_SRIOV_ATTR_RO(mode);

static struct attribute *sriov_attrs[] = {
	&mode_sriov_attr.attr,
	NULL
};

static const struct attribute_group sriov_attr_group = {
	.attrs = sriov_attrs,
};

static const struct attribute_group *default_sriov_attr_groups[] = {
	&sriov_attr_group,
	NULL
};

/* extended (PF and VFs) SR-IOV attributes */

static ssize_t auto_provisioning_sriov_ext_attr_show(struct drm_i915_private *i915,
						     unsigned int id, char *buf)
{
	int value = i915_sriov_pf_is_auto_provisioning_enabled(i915);

	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t auto_provisioning_sriov_ext_attr_store(struct drm_i915_private *i915,
						      unsigned int id,
						      const char *buf, size_t count)
{
	bool value;
	int err;

	err = kstrtobool(buf, &value);
	if (err)
		return err;

	err = i915_sriov_pf_set_auto_provisioning(i915, value);
	return err ?: count;
}

I915_SRIOV_EXT_ATTR(auto_provisioning);

static ssize_t id_sriov_ext_attr_show(struct drm_i915_private *i915,
				      unsigned int id, char *buf)
{
	return sysfs_emit(buf, "%u\n", id);
}

#define CONTROL_STOP "stop"
#define CONTROL_PAUSE "pause"
#define CONTROL_RESUME "resume"
#define CONTROL_CLEAR "clear"

static ssize_t control_sriov_ext_attr_store(struct drm_i915_private *i915,
					    unsigned int id,
					    const char *buf, size_t count)
{
	struct intel_iov *iov = &to_gt(i915)->iov;
	int err = -EPERM;

	if (sysfs_streq(buf, CONTROL_STOP)) {
		err = intel_iov_state_stop_vf(iov, id);
	} else if (sysfs_streq(buf, CONTROL_PAUSE)) {
		err = intel_iov_state_pause_vf(iov, id);
	} else if (sysfs_streq(buf, CONTROL_RESUME)) {
		err = intel_iov_state_resume_vf(iov, id);
	} else if (sysfs_streq(buf, CONTROL_CLEAR)) {
		err = intel_iov_provisioning_clear(iov, id);
	} else {
		err = -EINVAL;
	}

	return err ?: count;
}

I915_SRIOV_EXT_ATTR_RO(id);
I915_SRIOV_EXT_ATTR_WO(control);

static struct attribute *sriov_ext_attrs[] = {
	NULL
};

static const struct attribute_group sriov_ext_attr_group = {
	.attrs = sriov_ext_attrs,
};

static struct attribute *pf_ext_attrs[] = {
	&auto_provisioning_sriov_ext_attr.attr,
	NULL
};

static umode_t pf_ext_attr_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct i915_sriov_ext_kobj *sriov_kobj = to_sriov_ext_kobj(kobj);

	if (!sriov_ext_kobj_is_pf(sriov_kobj))
		return 0;

	return attr->mode;
}

static const struct attribute_group pf_ext_attr_group = {
	.attrs = pf_ext_attrs,
	.is_visible = pf_ext_attr_is_visible,
};

static struct attribute *vf_ext_attrs[] = {
	&id_sriov_ext_attr.attr,
	&control_sriov_ext_attr.attr,
	NULL
};

static umode_t vf_ext_attr_is_visible(struct kobject *kobj,
				      struct attribute *attr, int index)
{
	struct i915_sriov_ext_kobj *sriov_kobj = to_sriov_ext_kobj(kobj);

	if (sriov_ext_kobj_is_pf(sriov_kobj))
		return 0;

	return attr->mode;
}

static const struct attribute_group vf_ext_attr_group = {
	.attrs = vf_ext_attrs,
	.is_visible = vf_ext_attr_is_visible,
};

static const struct attribute_group *default_sriov_ext_attr_groups[] = {
	&sriov_ext_attr_group,
	&pf_ext_attr_group,
	&vf_ext_attr_group,
	NULL,
};

/* no user serviceable parts below */

static ssize_t sriov_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct drm_i915_private *i915 = sriov_kobj_to_i915(to_sriov_kobj(kobj));
	struct i915_sriov_attr *sriov_attr = to_sriov_attr(attr);

	return sriov_attr->show ? sriov_attr->show(i915, buf) : -EIO;
}

static ssize_t sriov_attr_store(struct kobject *kobj, struct attribute *attr,
				const char *buf, size_t count)
{
	struct drm_i915_private *i915 = sriov_kobj_to_i915(to_sriov_kobj(kobj));
	struct i915_sriov_attr *sriov_attr = to_sriov_attr(attr);

	return sriov_attr->store ? sriov_attr->store(i915, buf, count) : -EIO;
}

static const struct sysfs_ops sriov_sysfs_ops = {
	.show = sriov_attr_show,
	.store = sriov_attr_store,
};

static void sriov_kobj_release(struct kobject *kobj)
{
	struct i915_sriov_kobj *sriov_kobj = to_sriov_kobj(kobj);

	kfree(sriov_kobj);
}

static struct kobj_type sriov_ktype = {
	.release = sriov_kobj_release,
	.sysfs_ops = &sriov_sysfs_ops,
	.default_groups = default_sriov_attr_groups,
};

static ssize_t sriov_ext_attr_show(struct kobject *kobj, struct attribute *attr,
				   char *buf)
{
	struct i915_sriov_ext_kobj *sriov_kobj = to_sriov_ext_kobj(kobj);
	struct i915_sriov_ext_attr *sriov_attr = to_sriov_ext_attr(attr);
	struct drm_i915_private *i915 = sriov_ext_kobj_to_i915(sriov_kobj);
	unsigned int id = sriov_kobj->id;

	return sriov_attr->show ? sriov_attr->show(i915, id, buf) : -EIO;
}

static ssize_t sriov_ext_attr_store(struct kobject *kobj, struct attribute *attr,
				    const char *buf, size_t count)
{
	struct i915_sriov_ext_kobj *sriov_kobj = to_sriov_ext_kobj(kobj);
	struct i915_sriov_ext_attr *sriov_attr = to_sriov_ext_attr(attr);
	struct drm_i915_private *i915 = sriov_ext_kobj_to_i915(sriov_kobj);
	unsigned int id = sriov_kobj->id;

	return sriov_attr->store ? sriov_attr->store(i915, id, buf, count) : -EIO;
}

static const struct sysfs_ops sriov_ext_sysfs_ops = {
	.show = sriov_ext_attr_show,
	.store = sriov_ext_attr_store,
};

static void sriov_ext_kobj_release(struct kobject *kobj)
{
	struct i915_sriov_ext_kobj *sriov_kobj = to_sriov_ext_kobj(kobj);

	kfree(sriov_kobj);
}

static struct kobj_type sriov_ext_ktype = {
	.release = sriov_ext_kobj_release,
	.sysfs_ops = &sriov_ext_sysfs_ops,
	.default_groups = default_sriov_ext_attr_groups,
};

static unsigned int pf_nodes_count(struct drm_i915_private *i915)
{
	/* 1 x PF + n x VFs */
	return 1 + i915_sriov_pf_get_totalvfs(i915);
}

static int pf_setup_failed(struct drm_i915_private *i915, int err, const char *what)
{
	i915_probe_error(i915, "Failed to setup SR-IOV sysfs %s (%pe)\n",
			 what, ERR_PTR(err));
	return err;
}

static int pf_setup_home(struct drm_i915_private *i915)
{
	struct device *kdev = i915->drm.primary->kdev;
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_kobj *home = pf->sysfs.home;
	int err;

	GEM_BUG_ON(!IS_SRIOV_PF(i915));
	GEM_BUG_ON(home);

	err = i915_inject_probe_error(i915, -ENOMEM);
	if (unlikely(err))
		goto failed;

	home = kzalloc(sizeof(*home), GFP_KERNEL);
	if (unlikely(!home)) {
		err = -ENOMEM;
		goto failed;
	}

	err = kobject_init_and_add(&home->base, &sriov_ktype, &kdev->kobj, SRIOV_KOBJ_HOME_NAME);
	if (unlikely(err)) {
		goto failed_init;
	}

	GEM_BUG_ON(pf->sysfs.home);
	pf->sysfs.home = home;
	return 0;

failed_init:
	kobject_put(&home->base);
failed:
	return pf_setup_failed(i915, err, "home");
}

static void pf_teardown_home(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_kobj *home = fetch_and_zero(&pf->sysfs.home);

	if (home)
		kobject_put(&home->base);
}

static int pf_setup_tree(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_kobj *home = pf->sysfs.home;
	struct i915_sriov_ext_kobj **kobjs;
	struct i915_sriov_ext_kobj *kobj;
	unsigned int count = pf_nodes_count(i915);
	unsigned int n;
	int err;

	err = i915_inject_probe_error(i915, -ENOMEM);
	if (unlikely(err))
		goto failed;

	kobjs = kcalloc(count, sizeof(*kobjs), GFP_KERNEL);
	if (unlikely(!kobjs)) {
		err = -ENOMEM;
		goto failed;
	}

	for (n = 0; n < count; n++) {
		kobj = kzalloc(sizeof(*kobj), GFP_KERNEL);
		if (!kobj) {
			err = -ENOMEM;
			goto failed_kobj_n;
		}

		kobj->id = n;
		if (n) {
			err = kobject_init_and_add(&kobj->base, &sriov_ext_ktype,
						   &home->base, SRIOV_EXT_KOBJ_VFn_NAME, n);
		} else {
			err = kobject_init_and_add(&kobj->base, &sriov_ext_ktype,
						   &home->base, SRIOV_EXT_KOBJ_PF_NAME);
		}
		if (unlikely(err))
			goto failed_kobj_n;

		err = i915_inject_probe_error(i915, -EEXIST);
		if (unlikely(err))
			goto failed_kobj_n;

		kobjs[n] = kobj;
	}

	GEM_BUG_ON(pf->sysfs.kobjs);
	pf->sysfs.kobjs = kobjs;
	return 0;

failed_kobj_n:
	if (kobj)
		kobject_put(&kobj->base);
	while (n--)
		kobject_put(&kobjs[n]->base);
failed:
	return pf_setup_failed(i915, err, "tree");
}

static void pf_teardown_tree(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_ext_kobj **kobjs = fetch_and_zero(&pf->sysfs.kobjs);
	unsigned int count = pf_nodes_count(i915);
	unsigned int n;

	if (!kobjs)
		return;

	for (n = 0; n < count; n++)
		kobject_put(&kobjs[n]->base);

	kfree(kobjs);
}

static int pf_setup_device_link(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_ext_kobj **kobjs = pf->sysfs.kobjs;
	int err;

	err = i915_inject_probe_error(i915, -EEXIST);
	if (unlikely(err))
		goto failed;

	err = sysfs_create_link(&kobjs[0]->base, &i915->drm.dev->kobj, SRIOV_DEVICE_LINK_NAME);
	if (unlikely(err))
		goto failed;

	return 0;

failed:
	return pf_setup_failed(i915, err, "link");
}

static void pf_teardown_device_link(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_ext_kobj **kobjs = pf->sysfs.kobjs;

	sysfs_remove_link(&kobjs[0]->base, SRIOV_DEVICE_LINK_NAME);
}

static void pf_welcome(struct drm_i915_private *i915)
{
#if IS_ENABLED(CONFIG_DRM_I915_DEBUG)
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	const char *path = kobject_get_path(&pf->sysfs.home->base, GFP_KERNEL);

	drm_dbg(&i915->drm, "SR-IOV sysfs available at /sys%s\n", path);
	kfree(path);
#endif
	GEM_BUG_ON(!i915->sriov.pf.sysfs.kobjs);
}

static void pf_goodbye(struct drm_i915_private *i915)
{
	GEM_WARN_ON(i915->sriov.pf.sysfs.kobjs);
	GEM_WARN_ON(i915->sriov.pf.sysfs.home);
}

/**
 * i915_sriov_sysfs_setup - Setup SR-IOV sysfs tree.
 * @i915: the i915 struct
 *
 * On SR-IOV PF this function will setup dedicated sysfs tree
 * with PF and VFs attributes.
 *
 * Return: 0 on success or a negative error code on failure.
 */
int i915_sriov_sysfs_setup(struct drm_i915_private *i915)
{
	int err;

	if (!IS_SRIOV_PF(i915))
		return 0;

	if (i915_sriov_pf_aborted(i915))
		return 0;

	err = pf_setup_home(i915);
	if (unlikely(err))
		goto failed;

	err = pf_setup_tree(i915);
	if (unlikely(err))
		goto failed_tree;

	err = pf_setup_device_link(i915);
	if (unlikely(err))
		goto failed_link;

	pf_welcome(i915);
	return 0;

failed_link:
	pf_teardown_tree(i915);
failed_tree:
	pf_teardown_home(i915);
failed:
	return pf_setup_failed(i915, err, "");
}

/**
 * i915_sriov_sysfs_teardown - Cleanup SR-IOV sysfs tree.
 * @i915: the i915 struct
 *
 * Cleanup data initialized by @i915_sriov_sysfs_setup.
 */
void i915_sriov_sysfs_teardown(struct drm_i915_private *i915)
{
	if (!IS_SRIOV_PF(i915))
		return;

	pf_teardown_device_link(i915);
	pf_teardown_tree(i915);
	pf_teardown_home(i915);
	pf_goodbye(i915);
}

/* our Gen12 SR-IOV platforms are simple */
#define GEN12_VF_OFFSET 1
#define GEN12_VF_STRIDE 1
#define GEN12_VF_ROUTING_OFFSET(id) (GEN12_VF_OFFSET + ((id) - 1) * GEN12_VF_STRIDE)

static struct pci_dev *pf_get_vf_pci_dev(struct drm_i915_private *i915, unsigned int id)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	u16 vf_devid = pci_dev_id(pdev) + GEN12_VF_ROUTING_OFFSET(id);

	GEM_BUG_ON(!dev_is_pf(&pdev->dev));
	GEM_BUG_ON(!id);

	/* caller must use pci_dev_put() */
	return pci_get_domain_bus_and_slot(pci_domain_nr(pdev->bus),
					   PCI_BUS_NUM(vf_devid),
					   PCI_DEVFN(PCI_SLOT(vf_devid),
						     PCI_FUNC(vf_devid)));
}

static int pf_add_vfs_device_links(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_ext_kobj **kobjs = pf->sysfs.kobjs;
	struct pci_dev *pf_pdev = to_pci_dev(i915->drm.dev);
	struct pci_dev *vf_pdev = NULL;
	unsigned int numvfs = pci_num_vf(pf_pdev);
	unsigned int n;
	int err;

	if (!kobjs)
		return 0;

	GEM_BUG_ON(numvfs > pf_nodes_count(i915));

	for (n = 1; n <= numvfs; n++) {

		err = i915_inject_probe_error(i915, -ENODEV);
		if (unlikely(err)) {
			vf_pdev = NULL;
			goto failed_n;
		}

		vf_pdev = pf_get_vf_pci_dev(i915, n);
		if (unlikely(!vf_pdev)) {
			err = -ENODEV;
			goto failed_n;
		}

		err = i915_inject_probe_error(i915, -EEXIST);
		if (unlikely(err))
			goto failed_n;

		err = sysfs_create_link(&kobjs[n]->base, &vf_pdev->dev.kobj,
					SRIOV_DEVICE_LINK_NAME);
		if (unlikely(err))
			goto failed_n;

		/* balance pf_get_vf_pci_dev() */
		pci_dev_put(vf_pdev);
	}

	return 0;

failed_n:
	if (vf_pdev)
		pci_dev_put(vf_pdev);
	while (n-- > 1)
		sysfs_remove_link(&kobjs[n]->base, SRIOV_DEVICE_LINK_NAME);

	return pf_setup_failed(i915, err, "links");
}

static void pf_remove_vfs_device_links(struct drm_i915_private *i915)
{
	struct i915_sriov_pf *pf = &i915->sriov.pf;
	struct i915_sriov_ext_kobj **kobjs = pf->sysfs.kobjs;
	struct pci_dev *pf_pdev = to_pci_dev(i915->drm.dev);
	unsigned int numvfs = pci_num_vf(pf_pdev);
	unsigned int n;

	if (!kobjs)
		return;

	GEM_BUG_ON(numvfs > pf_nodes_count(i915));

	for (n = 1; n <= numvfs; n++)
		sysfs_remove_link(&kobjs[n]->base, SRIOV_DEVICE_LINK_NAME);
}

/**
 * i915_sriov_sysfs_update_links - Update links in SR-IOV sysfs tree.
 * @i915: the i915 struct
 *
 * On PF this function will add or remove PCI device links from VFs.
 */
void i915_sriov_sysfs_update_links(struct drm_i915_private *i915, bool add)
{
	if (!IS_SRIOV_PF(i915))
		return;

	if (add)
		pf_add_vfs_device_links(i915);
	else
		pf_remove_vfs_device_links(i915);
}
