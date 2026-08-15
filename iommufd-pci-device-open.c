/*
 * VFIO test suite
 *
 * Copyright (C) 2012-2025, Red Hat Inc.
 * Copyright (C) 2023, Intel Corporation.
 *
 * This work is licensed under the terms of the GNU GPL, version 2. See
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
#include <linux/iommufd.h>
#include <linux/vfio.h>

#include "utils.h"

void usage(char *name)
{
        printf("usage: %s <ssss:bb:dd.f>\n", name);
        printf("\nOpen device, enumerate regions, DMA map, hot reset (iommufd)\n");
}

int main(int argc, char **argv)
{
	const char *devname;
        int i, ret, device, iommufd, ioas_id;

        struct vfio_device_info device_info = {
                .argsz = sizeof(device_info)
        };
        struct vfio_region_info region_info = {
                .argsz = sizeof(region_info)
        };

        if (argc < 2) {
                usage(argv[0]);
                return -1;
        }

	devname = argv[1];

        iommufd = open("/dev/iommu", O_RDWR);
        if (iommufd < 0) {
                printf("Failed to open /dev/iommu, %d (%s)\n",
                       iommufd, strerror(errno));
                return 1;
        }

        if (vfio_device_iommufd_attach(iommufd, devname, &device, &ioas_id))
                return 1;

        if (ioctl(device, VFIO_DEVICE_GET_INFO, &device_info)) {
                printf("Failed to get device info\n");
                return -1;
        }

        printf("Device supports %d regions, %d irqs\n",
               device_info.num_regions, device_info.num_irqs);

        for (i = 0; i < device_info.num_regions; i++) {
                printf("Region %d: ", i);
                region_info.index = i;
                if (ioctl(device, VFIO_DEVICE_GET_REGION_INFO, &region_info)) {
                        if (i == VFIO_PCI_VGA_REGION_INDEX &&
                            !vfio_pci_is_vga(devname)) {
                                printf("not available (non-VGA device)\n");
                                continue;
                        }
                        printf("Failed to get info (%s)\n", strerror(errno));
                        return -1;
                }

                printf("size 0x%lx, offset 0x%lx, flags 0x%x\n",
                       (unsigned long)region_info.size,
                       (unsigned long)region_info.offset, region_info.flags);
                if (region_info.flags & VFIO_REGION_INFO_FLAG_MMAP) {
                        void *map = mmap(NULL, (size_t)region_info.size,
                                         PROT_READ, MAP_SHARED, device,
                                         (off_t)region_info.offset);
                        if (map == MAP_FAILED) {
                                printf("mmap failed\n");
                                continue;
                        }

			if (verbose)
				hexdump(map, region_info.size > 64 ? 64 :
					region_info.size);
                        munmap(map, (size_t)region_info.size);
                }
        }

        /* Allocate some space and setup a DMA mapping */
        struct iommu_ioas_map map = {
            .size = sizeof(map),
            .flags = IOMMU_IOAS_MAP_READABLE |
                IOMMU_IOAS_MAP_WRITEABLE |
                IOMMU_IOAS_MAP_FIXED_IOVA,
            .ioas_id = ioas_id,
            .iova = 0,
            .length = 1024 * 1024,
            .user_va = (__u64)mmap(0, 1024 * 1024, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, 0, 0),
        };

        ret = ioctl(iommufd, IOMMU_IOAS_MAP, &map);
        if (ret < 0) {
                printf("Failed IOMMU_IOAS_MAP ioas_id %d %d (%s)\n",
                       map.ioas_id, ret, strerror(errno));
                return 1;
        }
        printf("Mapped user_va %llx size %llx to iova %llx in ioas %d\n",
               map.user_va, map.length, map.iova, map.ioas_id);

        struct vfio_pci_hot_reset_info *reset_info;
        struct vfio_pci_dependent_device *devices;
        struct vfio_pci_hot_reset *reset;

        if (vfio_pci_is_vf(devname)) {
                printf("Skipping hot reset (VF)\n");
                return 0;
        }

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

        printf("Hot reset dependent device count: %d\n", reset_info->count);

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

        for (i = 0; i < reset_info->count; i++) {
                printf("%d: %04x:%02x:%02x.%d devid ", i,
                                devices[i].segment, devices[i].bus,
                                devices[i].devfn >> 3, devices[i].devfn & 7);
                if (devices[i].devid == VFIO_PCI_DEVID_NOT_OWNED)
                        printf("not owned\n");
                else
                        printf("%d\n", devices[i].devid);
        }

        if (!(reset_info->flags & VFIO_PCI_HOT_RESET_FLAG_DEV_ID)) {
                printf("VFIO_PCI_HOT_RESET_FLAG_DEV_ID should be set for IOMMUFD\n");
                return -1;
        }

        int unowned_cnt = 0;
        if (!(reset_info->flags & VFIO_PCI_HOT_RESET_FLAG_DEV_ID_OWNED)) {
                for (i = 0; i < reset_info->count; i++) {
                        if (devices[i].devid == VFIO_PCI_DEVID_NOT_OWNED) {
                                unowned_cnt++;
                                printf("Cannot reset device, "
                                        "depends on device %04x:%02x:%02x.%x "
                                        "which is not owned.\n",
                                        devices[i].segment, devices[i].bus,
                                        devices[i].devfn >> 3, devices[i].devfn & 7);
                        }
                }
                if (!unowned_cnt) {
                        printf("flags mismatch with data field, "
                                "VFIO_PCI_HOT_RESET_FLAG_DEV_ID_OWNED claimed but "
                                "no VFIO_PCI_DEVID_NOT_OWNED\n");
                        return -1;
                }
                printf("Skipping hot reset: reset domain has unowned devices\n");
                printf("All devices in the reset domain must be owned to perform hot reset\n");
                return 0;
        }


        printf("Attempting reset: ");
        fflush(stdout);

        /* Use zero length array for hot reset with iommufd backend */
        reset = malloc(sizeof(*reset));
        reset->argsz = sizeof(*reset);

        /* Bus reset! */
        ret = ioctl(device, VFIO_DEVICE_PCI_HOT_RESET, reset);
        printf("Hot reset: %s\n", ret ? "Failed" : "Pass");

        return 0;
}
