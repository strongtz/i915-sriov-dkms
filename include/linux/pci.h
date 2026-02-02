#include_next <linux/pci.h>

#ifndef __BACKPORT_LINUX_PCI_H__
#define __BACKPORT_LINUX_PCI_H__

#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 19, 0)
#define pci_rebar_size_to_bytes LINUX_BACKPORT(pci_rebar_size_to_bytes)
resource_size_t pci_rebar_size_to_bytes(int size);
#define pci_rebar_size_supported LINUX_BACKPORT(pci_rebar_size_supported)
bool pci_rebar_size_supported(struct pci_dev *pdev, int bar, int size);
#define pci_rebar_get_max_size LINUX_BACKPORT(pci_rebar_get_max_size)
int pci_rebar_get_max_size(struct pci_dev *pdev, int bar);
#endif

#endif /* __BACKPORT_LINUX_PCI_H__ */
