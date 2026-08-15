/*
 * VFIO test suite
 *
 * Copyright (C) 2012-2026, Red Hat Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Stress test for VFIO migration state machine. Cycles through:
 *   RUNNING -> STOP -> STOP_COPY -> STOP -> RESUMING -> STOP -> RUNNING
 * saving and restoring device state at each iteration.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <linux/iommufd.h>
#include <linux/vfio.h>

#include "utils.h"

#define DEFAULT_CYCLES 10

struct mig_state_ioctl {
	struct vfio_device_feature hdr;
	struct vfio_device_feature_mig_state state;
};

static const char *state_name(enum vfio_device_mig_state s)
{
	switch (s) {
	case VFIO_DEVICE_STATE_ERROR:     return "ERROR";
	case VFIO_DEVICE_STATE_STOP:      return "STOP";
	case VFIO_DEVICE_STATE_RUNNING:   return "RUNNING";
	case VFIO_DEVICE_STATE_STOP_COPY: return "STOP_COPY";
	case VFIO_DEVICE_STATE_RESUMING:  return "RESUMING";
	case VFIO_DEVICE_STATE_PRE_COPY:  return "PRE_COPY";
	default: return "UNKNOWN";
	}
}

static int get_state(int devfd, enum vfio_device_mig_state *out)
{
	struct mig_state_ioctl m = {
		.hdr.argsz = sizeof(m),
		.hdr.flags = VFIO_DEVICE_FEATURE_GET |
			     VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE,
	};

	if (ioctl(devfd, VFIO_DEVICE_FEATURE, &m)) {
		perror("GET_STATE");
		return -1;
	}
	*out = m.state.device_state;
	return 0;
}

static int set_state(int devfd, enum vfio_device_mig_state new,
		     int *data_fd_out)
{
	enum vfio_device_mig_state before = VFIO_DEVICE_STATE_ERROR;
	struct mig_state_ioctl m = {
		.hdr.argsz = sizeof(m),
		.hdr.flags = VFIO_DEVICE_FEATURE_SET |
			     VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE,
		.state.device_state = new,
		.state.data_fd = -1,
	};

	get_state(devfd, &before);

	if (ioctl(devfd, VFIO_DEVICE_FEATURE, &m)) {
		fprintf(stderr, "  %s -> %s: %s\n",
			state_name(before), state_name(new), strerror(errno));
		return -1;
	}

	printf("  %s -> %s", state_name(before), state_name(new));
	if (m.state.data_fd >= 0)
		printf(" (data_fd=%d)", m.state.data_fd);
	printf("\n");

	if (data_fd_out)
		*data_fd_out = m.state.data_fd;
	else if (m.state.data_fd >= 0)
		close(m.state.data_fd);
	return 0;
}

static bool migration_supported(int devfd)
{
	struct vfio_device_feature probe = {
		.argsz = sizeof(probe),
		.flags = VFIO_DEVICE_FEATURE_PROBE |
			 VFIO_DEVICE_FEATURE_MIG_DEVICE_STATE,
	};

	return !ioctl(devfd, VFIO_DEVICE_FEATURE, &probe);
}

static int open_device(const char *devname, int *devfd_out, int *iommufd_out)
{
	int iommufd, devfd;
	struct iommu_ioas_alloc ioas = { .size = sizeof(ioas) };
	struct vfio_device_bind_iommufd bind = { .argsz = sizeof(bind) };
	struct vfio_device_attach_iommufd_pt attach = { .argsz = sizeof(attach) };

	devfd = vfio_device_iommufd_getfd(devname);
	if (devfd < 0)
		return -1;

	iommufd = open("/dev/iommu", O_RDWR);
	if (iommufd < 0) {
		perror("open /dev/iommu");
		goto err_devfd;
	}

	if (ioctl(iommufd, IOMMU_IOAS_ALLOC, &ioas)) {
		perror("IOAS_ALLOC");
		goto err_iommufd;
	}

	bind.iommufd = iommufd;
	if (ioctl(devfd, VFIO_DEVICE_BIND_IOMMUFD, &bind)) {
		perror("BIND");
		goto err_iommufd;
	}

	attach.pt_id = ioas.out_ioas_id;
	if (ioctl(devfd, VFIO_DEVICE_ATTACH_IOMMUFD_PT, &attach)) {
		perror("ATTACH");
		goto err_iommufd;
	}

	*devfd_out = devfd;
	*iommufd_out = iommufd;
	return 0;

err_iommufd:
	close(iommufd);
err_devfd:
	close(devfd);
	return -1;
}

void usage(char *name)
{
	printf("usage: %s <ssss:bb:dd.f> [cycles]\n", name);
	printf("\tcycles: migration cycles (default 10)\n");
	printf("\nMigration state cycle stress test (iommufd)\n");
}

int main(int argc, char **argv)
{
	const char *devname;
	int cycles = DEFAULT_CYCLES;
	int devfd, iommufd;
	int i, ret = 0;

	if (argc < 2) {
		usage(argv[0]);
		return -1;
	}

	devname = argv[1];
	if (argc > 2)
		cycles = atoi(argv[2]);

	if (open_device(devname, &devfd, &iommufd))
		return -1;

	if (!migration_supported(devfd)) {
		printf("migration not supported\n");
		ret = EXIT_SKIP;
		goto out;
	}
	printf("migration: supported\n");

	enum vfio_device_mig_state cur;
	if (get_state(devfd, &cur))
		return -1;
	printf("initial state: %s\n", state_name(cur));

	for (i = 0; i < cycles; i++) {
		int save_fd = -1, resume_fd = -1;
		char state_buf[4096];
		ssize_t state_size = 0, n;

		printf("--- cycle %d/%d ---\n", i + 1, cycles);

		/* RUNNING -> STOP */
		if (set_state(devfd, VFIO_DEVICE_STATE_STOP, NULL)) {
			ret = -1; break;
		}

		/* STOP -> STOP_COPY (returns data_fd) */
		if (set_state(devfd, VFIO_DEVICE_STATE_STOP_COPY, &save_fd)) {
			ret = -1; break;
		}
		if (save_fd < 0) {
			fprintf(stderr, "no data_fd from STOP_COPY\n");
			ret = -1; break;
		}

		/* Drain state from save fd */
		while ((n = read(save_fd, state_buf + state_size,
				 sizeof(state_buf) - state_size)) > 0)
			state_size += n;
		close(save_fd);
		printf("  saved %zd bytes\n", state_size);

		/* STOP_COPY -> STOP */
		if (set_state(devfd, VFIO_DEVICE_STATE_STOP, NULL)) {
			ret = -1; break;
		}

		/* STOP -> RESUMING (returns data_fd) */
		if (set_state(devfd, VFIO_DEVICE_STATE_RESUMING, &resume_fd)) {
			ret = -1; break;
		}
		if (resume_fd < 0) {
			fprintf(stderr, "no data_fd from RESUMING\n");
			ret = -1; break;
		}

		/* Write saved state back */
		if (state_size > 0) {
			ssize_t w = write(resume_fd, state_buf, state_size);

			if (w != state_size) {
				fprintf(stderr, "write resume: %zd/%zd: %s\n",
					w, state_size, strerror(errno));
				close(resume_fd);
				ret = -1; break;
			}
		}
		close(resume_fd);
		printf("  loaded %zd bytes\n", state_size);

		/* RESUMING -> STOP */
		if (set_state(devfd, VFIO_DEVICE_STATE_STOP, NULL)) {
			ret = -1; break;
		}

		/* STOP -> RUNNING */
		if (set_state(devfd, VFIO_DEVICE_STATE_RUNNING, NULL)) {
			ret = -1; break;
		}

		/* Verify we're back in RUNNING */
		if (get_state(devfd, &cur) || cur != VFIO_DEVICE_STATE_RUNNING) {
			fprintf(stderr, "expected RUNNING, got %s\n",
				state_name(cur));
			ret = -1; break;
		}
	}

	if (!ret)
		printf("migration state cycle: %d/%d cycles passed\n", i, cycles);

out:
	close(devfd);
	close(iommufd);
	return ret;
}
