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

#define MAP_SIZE_DEFAULT (1UL * 1024 * 1024 * 1024)
#define MAP_CHUNK (4 * 1024)
#define REALLOC_INTERVAL 30
#define MAX_CYCLES_DEFAULT 0

void usage(char *name)
{
	printf("usage: %s ssss:bb:dd.f [map_size_mb] [max_cycles]\n", name);
	printf("\tssss: PCI segment, ex. 0000\n");
	printf("\tbb:   PCI bus, ex. 01\n");
	printf("\tdd:   PCI device, ex. 06\n");
	printf("\tf:    PCI function, ex. 0\n");
	printf("\tmap_size_mb: IOVA range in MB (default 1024)\n");
	printf("\tmax_cycles:  stop after N cycles, 0 = infinite (default 0)\n");
}

int main(int argc, char **argv)
{
	const char *devname;
	int ret, container;
	unsigned long i, count, map_size, max_cycles, nr_chunks;
	void **maps;
	struct vfio_iommu_type1_dma_map dma_map = {
		.argsz = sizeof(dma_map)
	};
	struct vfio_iommu_type1_dma_unmap dma_unmap = {
		.argsz = sizeof(dma_unmap)
	};

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	devname = argv[1];
	map_size = argc > 2 ? strtoul(argv[2], NULL, 0) * 1024 * 1024
			    : MAP_SIZE_DEFAULT;
	max_cycles = argc > 3 ? strtoul(argv[3], NULL, 0) : MAX_CYCLES_DEFAULT;
	nr_chunks = map_size / MAP_CHUNK;

	if (vfio_device_attach(devname, &container, NULL, NULL))
		return -1;

	printf("map_size=%luMB max_cycles=%lu\n", map_size / (1024 * 1024),
	       max_cycles);

	/* Test code */
	dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
	dma_map.size = MAP_CHUNK;
	dma_unmap.size = map_size;
	dma_unmap.iova = 0;

	/* Track our mmaps for re-use */
	maps = malloc(sizeof(void *) * nr_chunks);
	if (!maps) {
		printf("Failed to allocate map (%s)\n", strerror(errno));
		return -1;
	}

	memset(maps, 0, sizeof(void *) * nr_chunks);

	for (count = 0; !max_cycles || count < max_cycles; count++) {

		/* Every REALLOC_INTERVAL, dump our mappings to give THP something to collapse */
		if (count % REALLOC_INTERVAL == 0) {
			for (i = 0; i < nr_chunks; i++) {
				if (maps[i]) {
					munmap(maps[i], dma_map.size);
					maps[i] = NULL;
				}
			}
			if (count) {
				printf("\t%ld\n", count);
				//return 0;
			}
			printf("|");
			fflush(stdout);
		}

		/* Map MAP_CHUNK at a time, each chunk is pinned on map, so THP can't do anything until unmap */
		for (i = dma_map.iova = 0; i < nr_chunks; i++, dma_map.iova += dma_map.size) {
			if (!maps[i]) {
				maps[i] = mmap(NULL, dma_map.size,
						PROT_READ | PROT_WRITE,
						MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (maps[i] == MAP_FAILED) {
					printf("Failed to mmap memory (%s)\n", strerror(errno));
					return -1;
				}
			}

			ret = madvise(maps[i], dma_map.size, MADV_HUGEPAGE);
			if (ret) {
				printf("Madvise failed (%s)\n", strerror(errno));
			}

			dma_map.vaddr = (unsigned long)maps[i];

			ret = ioctl(container, VFIO_IOMMU_MAP_DMA, &dma_map);
			if (ret) {
				printf("Failed to map memory (%s)\n",
					strerror(errno));
				return 1;
			}
		}

		printf("+");
		fflush(stdout);

		/* Unmap everything at once */
		ret = ioctl(container, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
		if (ret) {
			printf("Failed to unmap memory (%s)\n", strerror(errno));
			return 1;
		}

		printf("-");
		fflush(stdout);
	}

	printf("\nSuccess\n");
	return 0;
}
