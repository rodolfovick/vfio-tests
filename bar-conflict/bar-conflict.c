// SPDX-License-Identifier: GPL-2.0
/*
 * bar-conflict - Claim a PCI BAR region to create a resource conflict
 *
 * Used to test that vfio-pci correctly rejects DMABUF export for BARs
 * whose resources could not be claimed (kernel commit 702809dabdec).
 *
 * Usage:
 *   insmod bar-conflict.ko bdf=0000:31:00.0 bar=0
 *   # bind device to vfio-pci, run test
 *   rmmod bar-conflict
 */

#include <linux/module.h>
#include <linux/pci.h>

static char *bdf;
module_param(bdf, charp, 0444);
MODULE_PARM_DESC(bdf, "PCI device BDF (e.g. 0000:31:00.0)");

static int bar;
module_param(bar, int, 0444);
MODULE_PARM_DESC(bar, "BAR index (0-5)");

static struct resource *res;
static resource_size_t addr;
static resource_size_t size;

static int __init bar_conflict_init(void)
{
	int domain, bus, slot, func;
	struct pci_dev *pdev;

	if (!bdf) {
		pr_err("bar-conflict: bdf parameter required\n");
		return -EINVAL;
	}

	if (bar < 0 || bar > 5) {
		pr_err("bar-conflict: bar must be 0-5\n");
		return -EINVAL;
	}

	if (sscanf(bdf, "%x:%x:%x.%x", &domain, &bus, &slot, &func) != 4) {
		pr_err("bar-conflict: invalid BDF format '%s'\n", bdf);
		return -EINVAL;
	}

	pdev = pci_get_domain_bus_and_slot(domain, bus, PCI_DEVFN(slot, func));
	if (!pdev) {
		pr_err("bar-conflict: device %s not found\n", bdf);
		return -ENODEV;
	}

	addr = pci_resource_start(pdev, bar);
	size = pci_resource_len(pdev, bar);
	pci_dev_put(pdev);

	if (!size) {
		pr_err("bar-conflict: %s BAR%d has no resource\n", bdf, bar);
		return -ENODEV;
	}

	res = request_mem_region(addr, size, "bar-conflict");
	if (!res) {
		pr_err("bar-conflict: failed to claim %s BAR%d [%pa+%pa]\n",
		       bdf, bar, &addr, &size);
		return -EBUSY;
	}

	pr_info("bar-conflict: claimed %s BAR%d [%pa+%pa]\n",
		bdf, bar, &addr, &size);
	return 0;
}

static void __exit bar_conflict_exit(void)
{
	release_mem_region(addr, size);
	pr_info("bar-conflict: released BAR%d [%pa+%pa]\n", bar, &addr, &size);
}

module_init(bar_conflict_init);
module_exit(bar_conflict_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Claim a PCI BAR region for vfio DMABUF testing");
