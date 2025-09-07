#include_next <linux/fs.h>

#ifndef __BACKPORT_LINUX_FS_H__
#define __BACKPORT_LINUX_FS_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6,17,0)
static inline int vfs_mmap(struct file *file, struct vm_area_struct *vma)
{
	if (file->f_op->mmap_prepare)
		return compat_vma_mmap_prepare(file, vma);

	return file->f_op->mmap(file, vma);
}

static inline int vfs_mmap_prepare(struct file *file, struct vm_area_desc *desc)
{
	return file->f_op->mmap_prepare(desc);
}
#endif

#endif /* __BACKPORT_LINUX_FS_H__ */
