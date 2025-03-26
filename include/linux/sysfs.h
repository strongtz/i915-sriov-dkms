#include <linux/version.h>
#include_next <linux/sysfs.h>

#ifndef _BACKPORT_LINUX_SYSFS_H
#define _BACKPORT_LINUX_SYSFS_H

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 10, 0)

#ifdef CONFIG_SYSFS
ssize_t sysfs_bin_attr_simple_read(struct file *file, struct kobject *kobj,
  struct bin_attribute *attr, char *buf,
  loff_t off, size_t count);
#endif

#define __BIN_ATTR_SIMPLE_RO(_name, _mode) {				\
	.attr	= { .name = __stringify(_name), .mode = _mode },	\
	.read	= sysfs_bin_attr_simple_read,				\
}

#define BIN_ATTR_SIMPLE_RO(_name)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_SIMPLE_RO(_name, 0444)

#define BIN_ATTR_SIMPLE_ADMIN_RO(_name)					\
struct bin_attribute bin_attr_##_name = __BIN_ATTR_SIMPLE_RO(_name, 0400)
#endif
#endif