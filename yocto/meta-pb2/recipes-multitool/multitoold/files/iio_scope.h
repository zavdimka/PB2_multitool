/* SPDX-License-Identifier: MIT */
#ifndef MULTITOOL_IIO_SCOPE_H
#define MULTITOOL_IIO_SCOPE_H

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include "scope_ring.h"

struct mt_scope_engine {
	pthread_t thread;
	_Atomic int running;
	_Atomic int stop_requested;
	int thread_started;
	int device_fd;
	char sysfs_path[256];
	char device_path[256];
	uint32_t read_size;
	struct mt_scope_ring *ring;
};

int mt_scope_find_device(const char *iio_name, char *sysfs_path,
			 size_t sysfs_size, char *device_path, size_t device_size);
int mt_scope_start(struct mt_scope_engine *engine, struct mt_scope_ring *ring,
		   const char *iio_name, const char *trigger_name,
		   uint32_t channel_mask, uint32_t buffer_scans);
void mt_scope_stop(struct mt_scope_engine *engine);

#endif
