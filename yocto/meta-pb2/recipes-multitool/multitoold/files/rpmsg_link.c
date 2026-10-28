/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "rpmsg_link.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int read_text(const char *path, char *buffer, size_t size)
{
	ssize_t length;
	int fd = open(path, O_RDONLY | O_CLOEXEC);
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

static int try_open_port(uint32_t remote_port)
{
	DIR *directory = opendir("/sys/class/rpmsg");
	struct dirent *entry;
	char path[320], value[128], name[128];
	if (!directory)
		return -1;
	while ((entry = readdir(directory))) {
		char *end;
		unsigned long destination;
		if (strncmp(entry->d_name, "rpmsg", 5) != 0 ||
		    strncmp(entry->d_name, "rpmsg_ctrl", 10) == 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/name", entry->d_name);
		if (read_text(path, name, sizeof(name)) || strcmp(name, "rpmsg-raw") != 0)
			continue;
		snprintf(path, sizeof(path), "/sys/class/rpmsg/%s/dst", entry->d_name);
		if (read_text(path, value, sizeof(value)))
			continue;
		errno = 0;
		destination = strtoul(value, &end, 0);
		if (errno || end == value || destination != remote_port)
			continue;
		snprintf(path, sizeof(path), "/dev/%s", entry->d_name);
		closedir(directory);
		return open(path, O_RDWR | O_CLOEXEC);
	}
	closedir(directory);
	errno = ENODEV;
	return -1;
}

int mt_rpmsg_open_port(uint32_t remote_port, unsigned timeout_ms)
{
	struct timespec delay = { .tv_sec = 0, .tv_nsec = 20000000 };
	unsigned elapsed = 0;
	int fd;
	do {
		fd = try_open_port(remote_port);
		if (fd >= 0)
			return fd;
		nanosleep(&delay, NULL);
		elapsed += 20;
	} while (elapsed < timeout_ms);
	errno = ETIMEDOUT;
	return -1;
}

int mt_rpmsg_exchange(int fd, const struct mt_message *request,
		      struct mt_message *reply, unsigned timeout_ms)
{
	struct pollfd wait = { .fd = fd, .events = POLLIN };
	size_t request_size;
	ssize_t result;
	if (fd < 0 || !request || !reply ||
	    request->header.payload_size > sizeof(request->payload)) {
		errno = EINVAL;
		return -1;
	}
	request_size = sizeof(request->header) + request->header.payload_size;
	result = write(fd, request, request_size);
	if (result != (ssize_t)request_size)
		return -1;
	result = poll(&wait, 1, (int)timeout_ms);
	if (result <= 0) {
		if (!result)
			errno = ETIMEDOUT;
		return -1;
	}
	result = read(fd, reply, sizeof(*reply));
	if (result < (ssize_t)sizeof(reply->header))
		return -1;
	if (reply->header.magic != MT_PROTOCOL_MAGIC ||
	    reply->header.version != MT_PROTOCOL_VERSION ||
	    reply->header.sequence != request->header.sequence) {
		errno = EPROTO;
		return -1;
	}
	return (int)result;
}

int mt_rpmsg_hello(uint32_t remote_port, struct mt_hello *hello)
{
	static uint32_t sequence;
	struct mt_message request, reply;
	int fd = mt_rpmsg_open_port(remote_port, 3000);
	int result;
	if (fd < 0)
		return -1;
	memset(&request, 0, sizeof(request));
	request.header.magic = MT_PROTOCOL_MAGIC;
	request.header.version = MT_PROTOCOL_VERSION;
	request.header.command = MT_CMD_HELLO;
	request.header.sequence = ++sequence;
	result = mt_rpmsg_exchange(fd, &request, &reply, 1000);
	close(fd);
	if (result < 0)
		return -1;
	if (reply.header.command != MT_CMD_HELLO ||
	    reply.header.payload_size != sizeof(reply.payload.hello)) {
		errno = EPROTO;
		return -1;
	}
	*hello = reply.payload.hello;
	return 0;
}
