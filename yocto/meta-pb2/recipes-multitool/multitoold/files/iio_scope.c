/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "iio_scope.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int read_text(const char *path, char *buffer, size_t size)
{
	ssize_t length;
	int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
		return -1;
	length = read(fd, buffer, size - 1u);
	close(fd);
	if (length < 0)
		return -1;
	while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
		length--;
	buffer[length] = '\0';
	return 0;
}

static int write_text(const char *path, const char *value)
{
	ssize_t length = (ssize_t)strlen(value);
	int fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	if (write(fd, value, (size_t)length) != length) {
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

int mt_scope_find_device(const char *iio_name, char *sysfs_path,
			 size_t sysfs_size, char *device_path, size_t device_size)
{
	DIR *directory = opendir("/sys/bus/iio/devices");
	struct dirent *entry;
	char path[320];
	char name[128];
	if (!directory)
		return -1;
	while ((entry = readdir(directory))) {
		if (strncmp(entry->d_name, "iio:device", 10) != 0)
			continue;
		snprintf(path, sizeof(path), "/sys/bus/iio/devices/%s/name", entry->d_name);
		if (read_text(path, name, sizeof(name)) || strcmp(name, iio_name) != 0)
			continue;
		snprintf(sysfs_path, sysfs_size, "/sys/bus/iio/devices/%s", entry->d_name);
		snprintf(device_path, device_size, "/dev/%s", entry->d_name);
		closedir(directory);
		return 0;
	}
	closedir(directory);
	errno = ENODEV;
	return -1;
}

static uint64_t monotonic_ns(void)
{
	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
}

static void *capture_thread(void *opaque)
{
	struct mt_scope_engine *engine = opaque;
	struct sched_param priority = { .sched_priority = 40 };
	uint8_t *buffer = malloc(engine->read_size);
	if (!buffer) {
		atomic_store(&engine->running, 0);
		return NULL;
	}
	pthread_setschedparam(pthread_self(), SCHED_FIFO, &priority);
	while (!atomic_load_explicit(&engine->stop_requested, memory_order_relaxed)) {
		struct pollfd waiter = { .fd = engine->device_fd, .events = POLLIN };
		int ready = poll(&waiter, 1, 250);
		ssize_t received;
		if (ready < 0) {
			if (errno == EINTR)
				continue;
			atomic_fetch_add(&engine->ring->header->read_errors, 1);
			break;
		}
		if (!ready)
			continue;
		received = read(engine->device_fd, buffer, engine->read_size);
		if (received < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			if (!atomic_load(&engine->stop_requested))
				atomic_fetch_add(&engine->ring->header->read_errors, 1);
			break;
		}
		if (!received)
			continue;
		if ((uint32_t)received != engine->read_size)
			atomic_fetch_add(&engine->ring->header->short_reads, 1);
		mt_scope_ring_push(engine->ring, buffer, (uint32_t)received, monotonic_ns(), 0);
	}
	free(buffer);
	atomic_store(&engine->running, 0);
	return NULL;
}

static int configure_channels(const char *sysfs, uint32_t channel_mask)
{
	char path[384];
	unsigned channel;
	for (channel = 0; channel < 8; ++channel) {
		snprintf(path, sizeof(path), "%s/scan_elements/in_voltage%u_en", sysfs, channel);
		if (access(path, F_OK) == 0 &&
		    write_text(path, (channel_mask & (1u << channel)) ? "1" : "0"))
			return -1;
	}
	snprintf(path, sizeof(path), "%s/scan_elements/in_timestamp_en", sysfs);
	if (access(path, F_OK) == 0)
		write_text(path, "0");
	return 0;
}

int mt_scope_start(struct mt_scope_engine *engine, struct mt_scope_ring *ring,
		   const char *iio_name, const char *trigger_name,
		   uint32_t channel_mask, uint32_t buffer_scans)
{
	char path[384];
	char value[64];
	if (!engine || !ring || !iio_name || !channel_mask) {
		errno = EINVAL;
		return -1;
	}
	if (atomic_load(&engine->running)) {
		errno = EBUSY;
		return -1;
	}
	memset(engine, 0, sizeof(*engine));
	engine->device_fd = -1;
	engine->ring = ring;
	engine->read_size = ring->header->block_size;
	if (mt_scope_find_device(iio_name, engine->sysfs_path, sizeof(engine->sysfs_path),
				 engine->device_path, sizeof(engine->device_path)))
		return -1;

	snprintf(path, sizeof(path), "%s/buffer/enable", engine->sysfs_path);
	write_text(path, "0");
	if (configure_channels(engine->sysfs_path, channel_mask))
		return -1;
	if (trigger_name && trigger_name[0] && strcmp(trigger_name, "-") != 0) {
		snprintf(path, sizeof(path), "%s/trigger/current_trigger", engine->sysfs_path);
		if (write_text(path, trigger_name))
			return -1;
	}
	snprintf(path, sizeof(path), "%s/buffer/length", engine->sysfs_path);
	snprintf(value, sizeof(value), "%u", buffer_scans);
	if (write_text(path, value))
		return -1;

	engine->device_fd = open(engine->device_path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
	if (engine->device_fd < 0)
		return -1;
	snprintf(path, sizeof(path), "%s/buffer/enable", engine->sysfs_path);
	if (write_text(path, "1")) {
		close(engine->device_fd);
		engine->device_fd = -1;
		return -1;
	}
	atomic_store(&engine->stop_requested, 0);
	atomic_store(&engine->running, 1);
	if (pthread_create(&engine->thread, NULL, capture_thread, engine)) {
		atomic_store(&engine->running, 0);
		write_text(path, "0");
		close(engine->device_fd);
		engine->device_fd = -1;
		return -1;
	}
	engine->thread_started = 1;
	return 0;
}

void mt_scope_stop(struct mt_scope_engine *engine)
{
	char path[384];
	if (!engine)
		return;
	if (!engine->thread_started && engine->device_fd < 0)
		return;
	atomic_store(&engine->stop_requested, 1);
	snprintf(path, sizeof(path), "%s/buffer/enable", engine->sysfs_path);
	write_text(path, "0");
	if (engine->device_fd >= 0)
		close(engine->device_fd);
	engine->device_fd = -1;
	if (engine->thread_started) {
		pthread_join(engine->thread, NULL);
		engine->thread_started = 0;
	}
	atomic_store(&engine->running, 0);
}
