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
#include <sys/stat.h>
#include <sys/types.h>

#include <linux/ioctl.h>
#include <linux/vfio.h>

#include "utils.h"

#define MAP_SIZE (1UL * 1024 * 1024 * 1024)
#define MAP_MAX_DEFAULT 127
/*
 * 2MB aligns with the PMD/THP boundary to stress IOMMU page table
 * coalescing
 */
#define DMA_CHUNK (2UL * 1024 * 1024)

void usage(char *name)
{
	printf("usage: %s [options] <ssss:bb:dd.f>\n", name);
	printf("\t-c iterations  number of 1GB IOVA iterations (default %d)\n",
	       MAP_MAX_DEFAULT);
	printf("\t-s             sequential mode (default is interleaved)\n");
	printf("\nInterleaved 2MB DMA mapping stress test (legacy container)\n");
}

int main(int argc, char **argv)
{
	const char *devname;
	int opt, container;
	unsigned long i, j, vaddr, map_max;
	long slab_before;
	int ret;
	int sequential = 0;
	struct vfio_iommu_type1_dma_map dma_map = {
		.argsz = sizeof(dma_map)
	};
	struct vfio_iommu_type1_dma_unmap dma_unmap = {
		.argsz = sizeof(dma_unmap)
	};

	map_max = MAP_MAX_DEFAULT;

	while ((opt = getopt(argc, argv, "c:sh")) != -1) {
		switch (opt) {
		case 'c':
			map_max = strtoul(optarg, NULL, 0);
			break;
		case 's':
			sequential = 1;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : -1;
		}
	}

	if (optind >= argc) {
		usage(argv[0]);
		return -1;
	}

	devname = argv[optind];

	if (vfio_dma_entry_limit_check(map_max * (MAP_SIZE / DMA_CHUNK)))
		return -1;

	if (vfio_device_attach(devname, &container, NULL, NULL))
		return -1;

	vaddr = (unsigned long)mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
	if (!vaddr) {
		printf("Failed to allocate memory\n");
		return -1;
	}
	if (verbose)
		printf("vaddr: %lx\n", vaddr);

	dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;

	slab_before = slab_sunreclaim_kb();

	printf("%lu iterations, chunk=%luMB, IOVA range=%luGB, %s\n",
	       map_max, DMA_CHUNK >> 20, map_max * (MAP_SIZE >> 30),
	       sequential ? "sequential" : "interleaved");
	printf("Mapping:   0%%");
	fflush(stdout);
	for (i = 0; i < map_max; i++) {
		dma_map.size = DMA_CHUNK;

		if (!(i % 3))
			continue;

		if (sequential) {
			for (j = 0; j < MAP_SIZE / DMA_CHUNK; j++) {
				dma_map.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);
				dma_map.vaddr = vaddr + (j * DMA_CHUNK);

				ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
				if (ret) {
					printf("Failed to map memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}
		} else {
			/* Map in order 0,4,8... 1,5,9... 3,7,11... 2,6,10... */
			for (j = 0; j < MAP_SIZE / DMA_CHUNK; j += 4) {
				dma_map.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);
				dma_map.vaddr = vaddr + (j * DMA_CHUNK);

				ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
				if (ret) {
					printf("Failed to map memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}

			for (j = 1; j < MAP_SIZE / DMA_CHUNK; j += 4) {
				dma_map.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);
				dma_map.vaddr = vaddr + (j * DMA_CHUNK);

				ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
				if (ret) {
					printf("Failed to map memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}

			for (j = 3; j < MAP_SIZE / DMA_CHUNK; j += 4) {
				dma_map.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);
				dma_map.vaddr = vaddr + (j * DMA_CHUNK);

				ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
				if (ret) {
					printf("Failed to map memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}

			for (j = 2; j < MAP_SIZE / DMA_CHUNK; j += 4) {
				dma_map.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);
				dma_map.vaddr = vaddr + (j * DMA_CHUNK);

				ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
				if (ret) {
					printf("Failed to map memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}
		}

		if (((i + 1) * 100)/map_max != (i * 100)/map_max) {
			printf("\b\b\b\b%3ld%%", (i * 100)/map_max);
			fflush(stdout);
		}
	}
	printf("\b\b\b\b100%%\n");

	printf("IOMMU page tables: ~%ldMB\n",
	       (slab_sunreclaim_kb() - slab_before) / 1024);

	printf("Unmapping:   0%%");
	fflush(stdout);
	for (i = 0; i < map_max; i++) {
		dma_unmap.size = DMA_CHUNK;

		if (!(i % 3))
			continue;

		if (sequential) {
			for (j = 0; j < MAP_SIZE / DMA_CHUNK; j++) {
				dma_unmap.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);

				ret = ioctl(container,
					    VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
				if (ret) {
					printf("Failed to unmap memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}
		} else {
			/* Unmap first half forward by 2, second half backward by 2 */
			for (j = 0; j < MAP_SIZE / DMA_CHUNK / 2; j += 2) {
				dma_unmap.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);

				ret = ioctl(container,
					    VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
				if (ret) {
					printf("Failed to unmap memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}

			for (j = (MAP_SIZE / DMA_CHUNK) - 1;
			     j > MAP_SIZE / DMA_CHUNK / 2; j -= 2) {
				dma_unmap.iova = (i * MAP_SIZE) + (j * DMA_CHUNK);

				ret = ioctl(container,
					    VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
				if (ret) {
					printf("Failed to unmap memory %ld/%ld (%s)\n",
					       i, j, strerror(errno));
					return 1;
				}
			}
		}

		if (((i + 1) * 100)/map_max != (i * 100)/map_max) {
			printf("\b\b\b\b%3ld%%", (i * 100)/map_max);
			fflush(stdout);
		}
	}
	printf("\b\b\b\b100%%\n");

	printf("Success\n");
	return 0;
}
