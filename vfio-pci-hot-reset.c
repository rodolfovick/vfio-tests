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

#define MAX_GROUPS 32

void usage(char *name)
{
	printf("usage: %s <ssss:bb:dd.f> [ssss:bb:dd.f ...]\n", name);
	printf("\tExtra BDFs own other IOMMU groups in the reset domain\n");
	printf("\nPCI hot reset via VFIO (legacy group)\n");
}

int main(int argc, char **argv)
{
	int i, j, ret, container, group, device;
	const char *devname;
	struct vfio_pci_hot_reset_info *reset_info;
	struct vfio_pci_dependent_device *devices;
	struct vfio_pci_hot_reset *reset;
	int group_fds[MAX_GROUPS];
	int group_ids[MAX_GROUPS];
	int nr_groups = 0;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	devname = argv[1];

	if (vfio_pci_is_vf(devname)) {
		printf("Skipping: %s is a VF\n", devname);
		return EXIT_SKIP;
	}

	if (vfio_device_attach(devname, &container, &device, &group))
		return -1;

	/* Query dependent devices */
	reset_info = malloc(sizeof(*reset_info));
	if (!reset_info) {
		printf("Failed to alloc info struct\n");
		return 1;
	}

	reset_info->argsz = sizeof(*reset_info);

	ret = ioctl(device, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, reset_info);
	if (ret && errno == ENODEV) {
		printf("Device does not support hot reset\n");
		return 0;
	}
	if (!ret || errno != ENOSPC) {
		printf("Expected fail/-ENOSPC, got %d/%d\n", ret, -errno);
		return -1;
	}

	printf("Dependent device count: %d\n", reset_info->count);

	reset_info = realloc(reset_info, sizeof(*reset_info) +
			     (reset_info->count * sizeof(*devices)));
	if (!reset_info) {
		printf("Failed to re-alloc info struct\n");
		return 1;
	}

	reset_info->argsz = sizeof(*reset_info) +
                             (reset_info->count * sizeof(*devices));
	ret = ioctl(device, VFIO_DEVICE_GET_PCI_HOT_RESET_INFO, reset_info);
	if (ret) {
		printf("Reset Info error\n");
		return 1;
	}

	devices = &reset_info->devices[0];

	/* Collect unique group IDs from the reset domain */
	group_ids[0] = devices[0].group_id;
	group_fds[0] = group;
	nr_groups = 1;

	for (i = 0; i < reset_info->count; i++) {
		int found = 0;

		printf("%d: %04x:%02x:%02x.%d group %d\n", i,
		       devices[i].segment, devices[i].bus,
		       devices[i].devfn >> 3, devices[i].devfn & 7,
		       devices[i].group_id);

		for (j = 0; j < nr_groups; j++) {
			if (group_ids[j] == devices[i].group_id) {
				found = 1;
				break;
			}
		}
		if (found)
			continue;

		if (nr_groups >= MAX_GROUPS) {
			printf("Too many groups in reset domain\n");
			return 1;
		}

		group_ids[nr_groups] = devices[i].group_id;
		group_fds[nr_groups] = -1;
		nr_groups++;
	}

	/* Open extra groups from additional BDFs on the command line */
	for (i = 2; i < argc; i++) {
		int gid = vfio_device_get_groupid(argv[i]);
		if (gid < 0) {
			printf("Failed to get IOMMU group for %s\n", argv[i]);
			return 1;
		}

		for (j = 0; j < nr_groups; j++) {
			if (group_ids[j] == gid && group_fds[j] == -1) {
				int fd = vfio_group_open(gid, false);
				if (fd < 0)
					return 1;
				ret = ioctl(fd, VFIO_GROUP_SET_CONTAINER, &container);
				if (ret) {
					printf("Failed to add group %d to container: %s\n",
					       gid, strerror(errno));
					return 1;
				}
				group_fds[j] = fd;
				printf("Added %s (group %d)\n", argv[i], gid);
				break;
			}
		}
	}

	/* Check all groups are owned */
	for (i = 0; i < nr_groups; i++) {
		if (group_fds[i] == -1) {
			printf("Skipping: group %d not owned, pass its BDF on the command line\n",
			       group_ids[i]);
			return EXIT_SKIP;
		}
	}

	printf("Attempting reset (%d groups): ", nr_groups);
	fflush(stdout);

	reset = malloc(sizeof(*reset) + nr_groups * sizeof(int));
	reset->argsz = sizeof(*reset) + nr_groups * sizeof(int);
	reset->count = nr_groups;
	reset->flags = 0;
	for (i = 0; i < nr_groups; i++)
		reset->group_fds[i] = group_fds[i];

	ret = ioctl(device, VFIO_DEVICE_PCI_HOT_RESET, reset);
	printf("%s\n", ret ? "Failed" : "Pass");

	free(reset);
	free(reset_info);
	return ret ? 1 : 0;
}
