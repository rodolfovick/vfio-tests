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

#include <linux/ioctl.h>
#include <linux/iommufd.h>
#include <linux/vfio.h>

#include "utils.h"

#ifndef VFIO_DEVICE_FEATURE_DMA_BUF
#define VFIO_DEVICE_FEATURE_DMA_BUF 11

struct vfio_region_dma_range {
	__u64 offset;
	__u64 length;
};

struct vfio_device_feature_dma_buf {
	uint32_t region_index;
	uint32_t open_flags;
	uint32_t flags;
	uint32_t nr_ranges;
	struct vfio_region_dma_range dma_ranges[];
};
#endif

static int try_dmabuf_export(int device, int region_index, __u64 length)
{
	struct {
		struct vfio_device_feature hdr;
		struct vfio_device_feature_dma_buf dma_buf;
		struct vfio_region_dma_range range;
	} dma_buf_feature = {
		.hdr = {
			.argsz = sizeof(dma_buf_feature),
			.flags = VFIO_DEVICE_FEATURE_GET |
				 VFIO_DEVICE_FEATURE_DMA_BUF,
		},
		.dma_buf = {
			.region_index = region_index,
			.open_flags = O_RDWR,
			.nr_ranges = 1,
		},
		.range = {
			.length = length,
		},
	};

	return ioctl(device, VFIO_DEVICE_FEATURE, &dma_buf_feature);
}

static int test_export_bars(int src_device, int iommufd,
			    const char *src_name, const char *dst_name,
			    int dst_ioas)
{
	struct vfio_region_info region_info = { .argsz = sizeof(region_info) };
	int i, ret;

	printf("\nExporting %s BARs as dma-buf, mapping into %s ioas %d\n\n",
	       src_name, dst_name ? dst_name : src_name, dst_ioas);

	for (i = 0; i < VFIO_PCI_ROM_REGION_INDEX; i++) {
		int dmabuf_fd;

		region_info.index = i;
		ret = ioctl(src_device, VFIO_DEVICE_GET_REGION_INFO,
			    &region_info);
		if (ret)
			continue;

		if (!region_info.size ||
		    !(region_info.flags & VFIO_REGION_INFO_FLAG_MMAP))
			continue;

		printf("BAR%d: size 0x%lx, offset 0x%lx, flags 0x%x\n",
		       i, (unsigned long)region_info.size,
		       (unsigned long)region_info.offset, region_info.flags);

		dmabuf_fd = try_dmabuf_export(src_device, i, region_info.size);
		if (dmabuf_fd < 0) {
			printf("\tdma-buf failed (%s)\n", strerror(errno));
			continue;
		}

		printf("\texported as dma-buf fd %d\n", dmabuf_fd);

		void *map = mmap(NULL, (size_t)region_info.size,
				 PROT_READ, MAP_SHARED, src_device,
				 (off_t)region_info.offset);
		if (map == MAP_FAILED) {
			printf("\tmmap failed (%s)\n", strerror(errno));
		} else {
			if (verbose)
				hexdump(map, region_info.size > 64 ? 64 :
					region_info.size);
			munmap(map, (size_t)region_info.size);
		}

		struct iommu_ioas_map_file map_file = {
			.size = sizeof(map_file),
			.flags = IOMMU_IOAS_MAP_READABLE |
				 IOMMU_IOAS_MAP_WRITEABLE,
			.ioas_id = dst_ioas,
			.fd = dmabuf_fd,
			.length = region_info.size,
		};
		ret = ioctl(iommufd, IOMMU_IOAS_MAP_FILE, &map_file);
		if (ret < 0) {
			printf("\tFailed IOMMU_IOAS_MAP_FILE %d (%s)\n",
			       ret, strerror(errno));
			close(dmabuf_fd);
			continue;
		}

		printf("\tmapped in %s ioas %d at IOVA 0x%llx size 0x%lx\n",
		       dst_name ? dst_name : src_name, dst_ioas,
		       (unsigned long long)map_file.iova,
		       (unsigned long)region_info.size);
		close(dmabuf_fd);
	}

	return 0;
}

void usage(char *name)
{
	printf("usage: %s [options] <ssss:bb:dd.f>\n", name);
	printf("\t-d BDF  destination device for P2P mapping\n");
	printf("\tWithout -d: export all mmappable BARs and self-map dma-buf\n");
	printf("\tWith -d:    export src BARs, map into dst IOAS (P2P)\n");
	printf("\nTest VFIO dma-buf BAR export and P2P mapping via iommufd\n");
}

int main(int argc, char **argv)
{
	const char *src_name, *dst_name = NULL;
	int opt, src_device, dst_device, iommufd, ret;
	int src_ioas, dst_ioas;
	struct vfio_device_info device_info = { .argsz = sizeof(device_info) };

	while ((opt = getopt(argc, argv, "d:h")) != -1) {
		switch (opt) {
		case 'd':
			dst_name = optarg;
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

	src_name = argv[optind];

	iommufd = open("/dev/iommu", O_RDWR);
	if (iommufd < 0) {
		printf("Failed to open /dev/iommu, %d (%s)\n",
		       iommufd, strerror(errno));
		return 1;
	}

	if (vfio_device_iommufd_attach(iommufd, src_name, &src_device, &src_ioas))
		return 1;

	struct vfio_device_feature probe = {
		.argsz = sizeof(probe),
		.flags = VFIO_DEVICE_FEATURE_PROBE | VFIO_DEVICE_FEATURE_DMA_BUF,
	};
	ret = ioctl(src_device, VFIO_DEVICE_FEATURE, &probe);
	if (ret < 0) {
		printf("DMA-BUF not supported (%s)\n", strerror(errno));
		return EXIT_SKIP;
	}

	if (dst_name) {
		if (vfio_device_iommufd_attach(iommufd, dst_name, &dst_device,
					  &dst_ioas))
			return 1;
	} else {
		dst_device = src_device;
		dst_ioas = src_ioas;
	}

	ret = ioctl(src_device, VFIO_DEVICE_GET_INFO, &device_info);
	if (ret) {
		printf("Failed to get device info\n");
		return -1;
	}

	ret = test_export_bars(src_device, iommufd, src_name, dst_name,
			       dst_ioas);
	if (ret)
		return ret;

	close(src_device);
	if (dst_name)
		close(dst_device);
	close(iommufd);

	printf("Success\n");
	return 0;
}
