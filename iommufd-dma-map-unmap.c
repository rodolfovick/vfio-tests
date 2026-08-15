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
#define MAP_SIZE_DEFAULT (256UL * 1024 * 1024)
#define MAX_CYCLES_DEFAULT 5

void usage(char *name)
{
	printf("usage: %s <ssss:bb:dd.f> [map_size_mb] [cycles] [stride_kb]\n", name);
	printf("\tmap_size_mb: total mapped size in MB (default 256)\n");
	printf("\tcycles:      map/unmap cycles (default 5)\n");
	printf("\tstride_kb:   IOVA stride in KB (default 4)\n");
	printf("\nDMA map/unmap stress test with 4KB chunks (iommufd)\n");
}

int main(int argc, char **argv)
{
	const char *devname;
	int ret, device, iommufd, ioas_id;
	unsigned long i, count, map_size, max_cycles, nr_chunks, stride;
	long slab_before, slab_delta = 0;
	void **maps;

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
	stride = argc > 4 ? strtoul(argv[4], NULL, 0) * 1024 : MAP_CHUNK;
	if (stride < MAP_CHUNK) {
		printf("stride_kb must be >= %d\n", MAP_CHUNK / 1024);
		return 1;
	}
	nr_chunks = map_size / MAP_CHUNK;

	iommufd = open("/dev/iommu", O_RDWR);
	if (iommufd < 0) {
		printf("Failed to open /dev/iommu: %s\n", strerror(errno));
		return 1;
	}

	if (vfio_device_iommufd_attach(iommufd, devname, &device, &ioas_id))
		return 1;

	char range_buf[16];

	printf("map_size=%luMB dma_size=%luKB stride=%luKB iova_range=%s max_cycles=%lu\n",
	       map_size / (1024 * 1024), (unsigned long)MAP_CHUNK / 1024,
	       stride / 1024, size_str(nr_chunks * stride, range_buf, sizeof(range_buf)),
	       max_cycles);

	maps = malloc(sizeof(void *) * nr_chunks);
	if (!maps) {
		printf("Failed to allocate tracking array: %s\n",
		       strerror(errno));
		return 1;
	}
	memset(maps, 0, sizeof(void *) * nr_chunks);

	map.ioas_id = ioas_id;
	map.length = MAP_CHUNK;

	unmap.ioas_id = ioas_id;

	for (count = 0; count < max_cycles; count++) {

		if (count == 0)
			slab_before = slab_sunreclaim_kb();

		if (verbose)
			printf("cycle %lu/%lu: map ", count + 1, max_cycles);

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
			map.iova = i * stride;

			ret = ioctl(iommufd, IOMMU_IOAS_MAP, &map);
			if (ret) {
				if (errno == EINVAL && stride > MAP_CHUNK)
					continue;
				printf("IOMMU_IOAS_MAP iova=0x%lx failed: %s\n",
				       i * stride, strerror(errno));
				return 1;
			}
			if (verbose && nr_chunks > 100 &&
			    (i + 1) * 10 / nr_chunks != i * 10 / nr_chunks) {
				printf(".");
				fflush(stdout);
			}
		}

		if (count == 0)
			slab_delta = slab_sunreclaim_kb() - slab_before;

		if (!verbose) {
			printf("+");
			fflush(stdout);
		} else
			printf(" unmap ");

		/* Unmap each chunk individually */
		for (i = 0; i < nr_chunks; i++) {
			unmap.iova = i * stride;
			unmap.length = MAP_CHUNK;

			ret = ioctl(iommufd, IOMMU_IOAS_UNMAP, &unmap);
			if (ret && errno != ENOENT) {
				printf("IOMMU_IOAS_UNMAP iova=0x%lx failed: %s\n",
				       i * stride, strerror(errno));
				return 1;
			}
			if (verbose && nr_chunks > 100 &&
			    (i + 1) * 10 / nr_chunks != i * 10 / nr_chunks) {
				printf(".");
				fflush(stdout);
			}
		}

		if (!verbose) {
			printf("-");
			fflush(stdout);
		} else
			printf("\n");
	}

	if (slab_delta)
		printf("\nIOMMU page tables: ~%ldMB per cycle\n", slab_delta / 1024);

	/* Final pass: map everything, then bulk unmap */
	for (i = 0; i < nr_chunks; i++) {
		map.user_va = (__u64)maps[i];
		map.iova = i * stride;

		ret = ioctl(iommufd, IOMMU_IOAS_MAP, &map);
		if (ret) {
			if (errno == EINVAL && stride > MAP_CHUNK)
				continue;
			printf("IOMMU_IOAS_MAP (bulk) iova=0x%lx failed: %s\n",
			       i * stride, strerror(errno));
			return 1;
		}
	}

	unmap.iova = 0;
	unmap.length = nr_chunks * stride;
	ret = ioctl(iommufd, IOMMU_IOAS_UNMAP, &unmap);
	if (ret) {
		printf("IOMMU_IOAS_UNMAP (bulk) failed: %s\n", strerror(errno));
		return 1;
	}

	for (i = 0; i < nr_chunks; i++) {
		if (maps[i])
			munmap(maps[i], MAP_CHUNK);
	}
	free(maps);

	printf("\n%lu mappings, Success\n", nr_chunks);
	return 0;
}
