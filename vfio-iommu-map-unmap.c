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

#define MAP_SIZE_DEFAULT (255UL * 1024 * 1024)
#define MAP_CHUNK (4 * 1024)
#define REALLOC_INTERVAL 20
#define MAX_CYCLES_DEFAULT 40

void usage(char *name)
{
	printf("usage: %s <ssss:bb:dd.f> [map_size_mb] [cycles] [stride_kb]\n", name);
	printf("\tmap_size_mb: total mapped size in MB (default 255)\n");
	printf("\tcycles:      map/unmap cycles (default 40)\n");
	printf("\tstride_kb:   IOVA stride in KB (default 4)\n");
	printf("\nDMA map/unmap stress test with 4KB chunks (legacy container)\n");
}

int main(int argc, char **argv)
{
	const char *devname;
	int ret, container;
	unsigned long i, count, map_size, max_cycles, nr_chunks, stride;
	long slab_before, slab_delta = 0;
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
	stride = argc > 4 ? strtoul(argv[4], NULL, 0) * 1024 : MAP_CHUNK;
	if (stride < MAP_CHUNK) {
		printf("stride_kb must be >= %d\n", MAP_CHUNK / 1024);
		return -1;
	}
	nr_chunks = map_size / MAP_CHUNK;

	long entry_limit = vfio_dma_entry_limit();
	if (entry_limit && nr_chunks > entry_limit) {
		printf("map_size %luMB needs %lu mappings but dma_entry_limit is %ld\n",
		       map_size / (1024 * 1024), nr_chunks, entry_limit);
		printf("increase with: echo %lu > /sys/module/vfio_iommu_type1/parameters/dma_entry_limit\n",
		       nr_chunks);
		return -1;
	}

	if (vfio_device_attach(devname, &container, NULL, NULL))
		return -1;

	char range_buf[16];

	printf("map_size=%luMB dma_size=%luKB stride=%luKB iova_range=%s max_cycles=%lu\n",
	       map_size / (1024 * 1024), (unsigned long)MAP_CHUNK / 1024,
	       stride / 1024, size_str(nr_chunks * stride, range_buf, sizeof(range_buf)),
	       max_cycles);

	/* Test code */
	dma_map.flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE;
	dma_map.size = MAP_CHUNK;
	dma_unmap.size = nr_chunks * stride;
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
				if (!verbose) {
					printf("|");
					fflush(stdout);
				}
			}
		}

		if (count == 0)
			slab_before = slab_sunreclaim_kb();

		if (verbose)
			printf("cycle %lu/%lu: map ", count + 1, max_cycles);

		/* Map MAP_CHUNK at a time, each chunk is pinned on map, so THP can't do anything until unmap */
		for (i = dma_map.iova = 0; i < nr_chunks; i++, dma_map.iova += stride) {
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
				if (errno == EINVAL && stride > MAP_CHUNK)
					continue;
				printf("Failed to map memory (%s)\n",
					strerror(errno));
				return 1;
			}
			if (verbose && nr_chunks > 100 &&
			    (i + 1) * 10 / nr_chunks != i * 10 / nr_chunks) {
				printf(".");
				fflush(stdout);
			}
		}

		if (!verbose) {
			printf("+");
			fflush(stdout);
		} else
			printf(" unmap ");

		if (count == 0)
			slab_delta = slab_sunreclaim_kb() - slab_before;

		/* Unmap everything at once */
		ret = ioctl(container, VFIO_IOMMU_UNMAP_DMA, &dma_unmap);
		if (ret) {
			printf("Failed to unmap memory (%s)\n", strerror(errno));
			return 1;
		}

		if (!verbose) {
			printf("-");
		} else
			printf("done\n");
		fflush(stdout);
	}

	if (slab_delta)
		printf("\nIOMMU page tables: ~%ldMB per cycle\n", slab_delta / 1024);

	printf("\n%lu mappings, Success\n", nr_chunks);
	return 0;
}
