/*
 * VFIO test suite - IOMMUFD DMA map/unmap
 *
 * Copyright (C) 2012-2025, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/iommufd.h>
#include <linux/vfio.h>

#include "utils.h"

#define MAP_CHUNK	(4 * 1024)
#define MAP_SIZE_DEFAULT (64UL * 1024 * 1024)
#define MAX_CYCLES_DEFAULT 5

void usage(char *name)
{
	printf("usage: %s <ssss:bb:dd.f> [map_size_mb] [max_cycles]\n", name);
}

int main(int argc, char **argv)
{
	const char *devname;
	int ret, device, iommufd;
	unsigned long i, count, map_size, max_cycles, nr_chunks;
	void **maps;

	struct vfio_device_bind_iommufd bind = {
		.argsz = sizeof(bind),
		.flags = 0,
	};
	struct iommu_ioas_alloc alloc_data = {
		.size = sizeof(alloc_data),
		.flags = 0,
	};
	struct vfio_device_attach_iommufd_pt attach_data = {
		.argsz = sizeof(attach_data),
		.flags = 0,
	};
	struct iommu_ioas_map map = {
		.size = sizeof(map),
		.flags = IOMMU_IOAS_MAP_READABLE |
			 IOMMU_IOAS_MAP_WRITEABLE |
			 IOMMU_IOAS_MAP_FIXED_IOVA,
	};
	struct iommu_ioas_unmap unmap = {
		.size = sizeof(unmap),
	};

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	devname = argv[1];
	map_size = argc > 2 ? strtoul(argv[2], NULL, 0) * 1024 * 1024
			    : MAP_SIZE_DEFAULT;
	max_cycles = argc > 3 ? strtoul(argv[3], NULL, 0) : MAX_CYCLES_DEFAULT;
	nr_chunks = map_size / MAP_CHUNK;

	device = vfio_device_iommufd_getfd(devname);
	if (device < 0)
		return 1;

	iommufd = open("/dev/iommu", O_RDWR);
	if (iommufd < 0) {
		printf("Failed to open /dev/iommu: %s\n", strerror(errno));
		return 1;
	}

	bind.iommufd = iommufd;
	ret = ioctl(device, VFIO_DEVICE_BIND_IOMMUFD, &bind);
	if (ret) {
		printf("VFIO_DEVICE_BIND_IOMMUFD failed: %s\n", strerror(errno));
		return 1;
	}

	ret = ioctl(iommufd, IOMMU_IOAS_ALLOC, &alloc_data);
	if (ret) {
		printf("IOMMU_IOAS_ALLOC failed: %s\n", strerror(errno));
		return 1;
	}

	attach_data.pt_id = alloc_data.out_ioas_id;
	ret = ioctl(device, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach_data);
	if (ret) {
		printf("VFIO_DEVICE_ATTACH_IOMMUFD_PT failed: %s\n",
		       strerror(errno));
		return 1;
	}

	printf("Attached iommufd=%d ioas=%d\n",
	       iommufd, alloc_data.out_ioas_id);
	printf("map_size=%luMB nr_chunks=%lu max_cycles=%lu\n",
	       map_size / (1024 * 1024), nr_chunks, max_cycles);

	maps = malloc(sizeof(void *) * nr_chunks);
	if (!maps) {
		printf("Failed to allocate tracking array: %s\n",
		       strerror(errno));
		return 1;
	}
	memset(maps, 0, sizeof(void *) * nr_chunks);

	map.ioas_id = alloc_data.out_ioas_id;
	map.length = MAP_CHUNK;

	unmap.ioas_id = alloc_data.out_ioas_id;

	for (count = 0; count < max_cycles; count++) {

		/* Map chunks across the IOVA range */
		for (i = 0; i < nr_chunks; i++) {
			if (!maps[i]) {
				maps[i] = mmap(NULL, MAP_CHUNK,
					       PROT_READ | PROT_WRITE,
					       MAP_PRIVATE | MAP_ANONYMOUS,
					       -1, 0);
				if (maps[i] == MAP_FAILED) {
					printf("mmap failed: %s\n",
					       strerror(errno));
					return 1;
				}
			}

			map.user_va = (__u64)maps[i];
			map.iova = i * MAP_CHUNK;

			ret = ioctl(iommufd, IOMMU_IOAS_MAP, &map);
			if (ret) {
				printf("IOMMU_IOAS_MAP iova=0x%lx failed: %s\n",
				       i * MAP_CHUNK, strerror(errno));
				return 1;
			}
		}

		printf("+");
		fflush(stdout);

		/* Unmap each chunk individually */
		for (i = 0; i < nr_chunks; i++) {
			unmap.iova = i * MAP_CHUNK;
			unmap.length = MAP_CHUNK;

			ret = ioctl(iommufd, IOMMU_IOAS_UNMAP, &unmap);
			if (ret) {
				printf("IOMMU_IOAS_UNMAP iova=0x%lx failed: %s\n",
				       i * MAP_CHUNK, strerror(errno));
				return 1;
			}
		}

		printf("-");
		fflush(stdout);
	}

	/* Final pass: map everything, then bulk unmap */
	for (i = 0; i < nr_chunks; i++) {
		map.user_va = (__u64)maps[i];
		map.iova = i * MAP_CHUNK;

		ret = ioctl(iommufd, IOMMU_IOAS_MAP, &map);
		if (ret) {
			printf("IOMMU_IOAS_MAP (bulk) iova=0x%lx failed: %s\n",
			       i * MAP_CHUNK, strerror(errno));
			return 1;
		}
	}

	printf("+");
	fflush(stdout);

	unmap.iova = 0;
	unmap.length = map_size;
	ret = ioctl(iommufd, IOMMU_IOAS_UNMAP, &unmap);
	if (ret) {
		printf("IOMMU_IOAS_UNMAP (bulk) failed: %s\n", strerror(errno));
		return 1;
	}

	printf("-");

	for (i = 0; i < nr_chunks; i++) {
		if (maps[i])
			munmap(maps[i], MAP_CHUNK);
	}
	free(maps);

	printf("\nSuccess\n");
	return 0;
}
