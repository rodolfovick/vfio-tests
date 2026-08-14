/*
 * VFIO test suite
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
#include <sys/param.h>
#include <sys/resource.h>
#include <sys/types.h>

#include <linux/ioctl.h>
#include <linux/vfio.h>

#include "utils.h"

#define ITERATIONS 10

static const size_t default_pagesizes[] = {
	4ul * 1024,
	2ul * 1024 * 1024,
	1ul * 1024 * 1024 * 1024,
	0,
};

static int parse_pagesizes(int argc, char **argv, size_t *out)
{
	int i, j, n = MIN(argc - 3, 3);

	for (i = 0; i < n; i++) {
		out[i] = strtoul(argv[3 + i], NULL, 0) * 1024;
		for (j = 0; default_pagesizes[j]; j++)
			if (out[i] == default_pagesizes[j])
				break;
		if (!default_pagesizes[j]) {
			printf("Invalid page size: %sKB\n", argv[3 + i]);
			return -1;
		}
	}
	out[n] = 0;
	return 0;
}

void usage(char *name)
{
	printf("usage: %s <ssss:bb:dd.f> [iterations] [4|2048|1048576 ...]\n", name);
}

static void do_fault_timing(int device, struct vfio_region_info *region,
			    int bar, size_t pagesz, int iterations)
{
	unsigned long total_ns = 0;
	unsigned long min_ns = ~0ul;
	unsigned long max_ns = 0;
	unsigned long total_faults = 0;
	unsigned long accesses = region->size / pagesz;
	int i;

	for (i = 0; i < iterations; i++) {
		unsigned long before, after, elapsed;
		struct rusage ru_before, ru_after;
		volatile char *p;
		size_t off;
		void *map = mmap_align(NULL, region->size, PROT_READ, MAP_SHARED,
				       device, (off_t)region->offset, pagesz);
		if (map == MAP_FAILED) {
			printf("  mmap failed: %s\n", strerror(errno));
			return;
		}

		if (i == 0) {
			if (verbose)
				printf("  BAR%d mmap %p\n", bar, map);
			if ((unsigned long)map & (pagesz - 1))
				printf("  BAR%d mmap %p not aligned to 0x%lx\n",
				       bar, map, pagesz);
		}

		getrusage(RUSAGE_SELF, &ru_before);
		before = now_nsec();
		for (off = 0; off < region->size; off += pagesz) {
			p = (volatile char *)map + off;
			(void)*p;
		}
		after = now_nsec();
		getrusage(RUSAGE_SELF, &ru_after);

		elapsed = after - before;
		total_ns += elapsed;
		total_faults += ru_after.ru_minflt - ru_before.ru_minflt;
		if (elapsed < min_ns)
			min_ns = elapsed;
		if (elapsed > max_ns)
			max_ns = elapsed;

		munmap(map, region->size);
	}

	char barsz[16], pgsz[16];

	size_str((unsigned long)region->size, barsz, sizeof(barsz));
	size_str(pagesz, pgsz, sizeof(pgsz));

	unsigned long avg_ns = total_ns / iterations;

	printf("  BAR%d %5s, page %4s: %8llu accesses, %8ld faults, "
	       "avg %6ld.%03ldms, min %6ld.%03ldms, max %6ld.%03ldms\n",
	       bar, barsz, pgsz,
	       (unsigned long long)region->size / pagesz,
	       total_faults / iterations,
	       avg_ns / NSEC_PER_MSEC, (avg_ns % NSEC_PER_MSEC) / USEC_PER_MSEC,
	       min_ns / NSEC_PER_MSEC, (min_ns % NSEC_PER_MSEC) / USEC_PER_MSEC,
	       max_ns / NSEC_PER_MSEC, (max_ns % NSEC_PER_MSEC) / USEC_PER_MSEC);

	unsigned long avg_faults = total_faults / iterations;

	if (avg_faults < accesses / 2)
		printf("         ** WARNING: faults (%ld) much lower than accesses (%ld), "
		       "pfnmap huge pages likely active (VMA accidentally aligned?) **\n",
		       avg_faults, accesses);
}

int main(int argc, char **argv)
{
	const char *devname;
	int container, device;
	int i, ret;
	int iterations = ITERATIONS;
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };
	struct vfio_region_info region_info = { .argsz = sizeof(region_info) };
	const size_t *pagesizes = default_pagesizes;
	size_t cmd_pagesizes[4];

	if (argc < 2) {
		usage(argv[0]);
		return 1;
	}

	devname = argv[1];

	if (argc > 2)
		iterations = atoi(argv[2]);

	if (argc > 3) {
		if (parse_pagesizes(argc, argv, cmd_pagesizes))
			return 1;
		pagesizes = cmd_pagesizes;
	}

	if (vfio_pci_is_vf(devname)) {
		printf("Skipping: %s is a VF\n", devname);
		return EXIT_SKIP;
	}

	if (vfio_device_attach(devname, &container, &device, NULL))
		return 1;

	ret = ioctl(device, VFIO_DEVICE_GET_INFO, &device_info);
	if (ret) {
		printf("VFIO_DEVICE_GET_INFO failed: %d (%s)\n",
		       ret, strerror(errno));
		return 1;
	}

	if (!(device_info.flags & VFIO_DEVICE_FLAGS_PCI) ||
	    device_info.num_regions < VFIO_PCI_BAR5_REGION_INDEX) {
		printf("Invalid vfio-pci device\n");
		return 1;
	}

	if (vfio_pci_is_d3(devname))
		printf("WARNING: device %s is in D3 state, MMIO timing will be degraded\n",
		       devname);

	printf("Device %s, %d iterations per test\n", devname, iterations);

	for (i = 0; i < VFIO_PCI_ROM_REGION_INDEX; i++) {
		const size_t *pgsz;

		region_info.index = i;
		ret = ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &region_info);
		if (ret)
			continue;

		if (!region_info.size ||
		    !(region_info.flags & VFIO_REGION_INFO_FLAG_MMAP))
			continue;

		char bsz[16];

		size_str((unsigned long)region_info.size, bsz, sizeof(bsz));
		printf("BAR%d: size %s (0x%lx), order %d\n", i, bsz,
		       (unsigned long)region_info.size,
		       __builtin_ctzll((unsigned long long)region_info.size));

		for (pgsz = &pagesizes[0]; *pgsz; pgsz++) {
			if (*pgsz > region_info.size)
				continue;
			do_fault_timing(device, &region_info, i, *pgsz, iterations);
		}
	}

	printf("Success\n");
	return 0;
}
