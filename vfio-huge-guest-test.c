/*
 * VFIO test suite
 *
 * Copyright (C) 2012-2025, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */


#include <errno.h>
#include <libgen.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>

#include <linux/ioctl.h>
#include <linux/vfio.h>

#include "utils.h"

#define DEFAULT_MMAP_GB 4UL
#define DEFAULT_GUEST_GB 1024UL

void usage(char *name)
{
	printf("usage: %s <iommu group id> [hugepage path | guest_gb]\n", name);
}

int main(int argc, char **argv)
{
	int ret, container, groupid, fd = -1;
	char path[PATH_MAX + NAME_MAX + sizeof("/.XXXXXX")];
	char mempath[PATH_MAX] = "";
	unsigned long vaddr;
	unsigned long guest_gb = DEFAULT_GUEST_GB;
	unsigned long mmap_gb = DEFAULT_MMAP_GB;
	unsigned long mmap_size;
	struct vfio_group_status group_status = {
		.argsz = sizeof(group_status)
	};
	struct vfio_iommu_type1_dma_map dma_map = {
		.argsz = sizeof(dma_map)
	};

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	ret = sscanf(argv[1], "%d", &groupid);
	if (ret != 1) {
		usage(argv[0]);
		return -1;
	}

	if (vfio_group_attach(groupid, &container, NULL))
		return -1;

	if (argc > 2) {
		char *endp;
		unsigned long val = strtoul(argv[2], &endp, 0);
		if (*endp == '\0' && val > 0)
			guest_gb = val;
		else
			strncpy(mempath, argv[2], sizeof(mempath) - 1);
	}

	if (argc > 3)
		guest_gb = strtoul(argv[3], NULL, 0);
	if (guest_gb < mmap_gb)
		mmap_gb = guest_gb;
	mmap_size = mmap_gb * 1024 * 1024 * 1024;

	printf("guest_gb=%lu mmap_gb=%lu\n", guest_gb, mmap_gb);

	if (strlen(mempath)) {
		struct statfs fs;

		do {
			ret = statfs(mempath, &fs);
		} while (ret != 0 && errno == EINTR);

		if (ret) {
			printf("Can't statfs on %s\n", mempath);
		} else
			printf("Using %ldK huge page size\n", fs.f_bsize >> 10);

		sprintf(path, "%s/%s.XXXXXX", mempath, basename(argv[0]));
		fd = mkstemp(path);
		if (fd < 0)
			printf("Failed to open mempath file %s (%s)\n",
			       path, strerror(errno));
	}

	/* 4G of host memory */
	if (fd < 0) {
		vaddr = (unsigned long)mmap(0, mmap_size,
					    PROT_READ | PROT_WRITE,
					    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	} else {
		ftruncate(fd, mmap_size);
		vaddr = (unsigned long)mmap(0, mmap_size,
					    PROT_READ | PROT_WRITE,
					    MAP_POPULATE | MAP_SHARED, fd, 0);
	}

	if ((void *)vaddr == MAP_FAILED) {
		printf("Failed to allocate memory\n");
		return -1;
	}

	dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

	/* 640K@0, enough for anyone */
	printf("Mapping 0-640K");
	fflush(stdout);
	dma_map.vaddr = vaddr;
	dma_map.size = 640 * 1024;
	dma_map.iova = 0;
	ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
	if (ret) {
		printf("Failed to map memory (%s)\n", strerror(errno));
		return ret;
	}
	printf(".\n");

	/* (3G - 1M)@1M "low memory" */
	printf("Mapping low memory");
	fflush(stdout);
	dma_map.size = (3UL * 1024 * 1024 * 1024) - (1024 * 1024);
	dma_map.iova = 1024 * 1024;
	dma_map.vaddr = vaddr + dma_map.iova;
	ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
	if (ret) {
		printf("Failed to map memory (%s)\n", strerror(errno));
		return ret;
	}
	printf(".\n");

	/* (1TB - 4G)@4G "high memory" after the I/O hole */
	printf("Mapping high memory");
	fflush(stdout);
	dma_map.size = mmap_size;
	dma_map.iova = 4UL * 1024 * 1024 * 1024;
	dma_map.vaddr = vaddr;
	while (dma_map.iova < guest_gb * 1024 * 1024 * 1024) {
		ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
		if (ret) {
			printf("Failed to map memory (%s)\n", strerror(errno));
			return ret;
		}
		printf(".");
		fflush(stdout);
		dma_map.iova += mmap_size;
	}
	printf("\n");

	if (fd >= 0)
		unlink(path);

	return 0;
}
