/*
 * VFIO test suite
 *
 * Copyright (C) 2012-2025, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#ifndef VFIO_TESTSUITE_UTILS_H
#define VFIO_TESTSUITE_UTILS_H

#include <stdbool.h>
#include <time.h>

/*
 * Logging
 */
extern int verbose;

int vfio_group_attach(int groupid, int *container_out, int *group_out);
int vfio_device_attach(const char *devname, int *container_out,
		       int *device_out, int *group_out);
int vfio_device_attach_iommu_type(const char *devname, int *container_out,
				  int *device_out, int *group_out,
				  int iommu_type);
int vfio_device_iommufd_getfd(const char *devname);
int vfio_device_iommufd_attach(int iommufd, const char *devname,
			       int *device_out, int *ioas_id_out);

#define NSEC_PER_SEC 1000000000ul
#define USEC_PER_SEC 1000000ul

static inline unsigned long now_nsec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
	return NSEC_PER_SEC * (unsigned long) ts.tv_sec + ts.tv_nsec;
}

void hexdump(const void *data, size_t len);
int vfio_pci_is_vf(const char *devname);
int vfio_pci_is_vga(const char *devname);
int vfio_pci_is_d3(const char *devname);
long slab_sunreclaim_kb(void);
long vfio_dma_entry_limit(void);
int vfio_dma_entry_limit_check(unsigned long nr_mappings);
int vfio_noiommu_enabled(void);
int vfio_device_get_groupid(const char *devname);
int vfio_group_open(int groupid, bool noiommu);
long hugepages_free(void);
unsigned int vfio_pci_vendor(const char *devname);

void *mmap_align(void *addr, size_t length, int prot, int flags,
		 int fd, off_t offset, size_t align);

#define EXIT_SKIP 77

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))

#define NSEC_PER_MSEC (NSEC_PER_SEC / 1000)
#define USEC_PER_MSEC (USEC_PER_SEC / 1000)

static inline const char *size_str(unsigned long size, char *buf, size_t len)
{
	if (size >= (1ul << 40) && !(size & ((1ul << 40) - 1)))
		snprintf(buf, len, "%ldTB", size >> 40);
	else if (size >= (1ul << 30) && !(size & ((1ul << 30) - 1)))
		snprintf(buf, len, "%ldGB", size >> 30);
	else if (size >= (1ul << 20) && !(size & ((1ul << 20) - 1)))
		snprintf(buf, len, "%ldMB", size >> 20);
	else
		snprintf(buf, len, "%ldKB", size >> 10);
	return buf;
}

#endif /* VFIO_TESTSUITE_UTILS_H */
