/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "remoteproc.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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

int mt_remoteproc_find(const char *name_suffix, char *path, size_t path_size)
{
	DIR *directory = opendir("/sys/class/remoteproc");
	struct dirent *entry;
	char candidate[320];
	char name[128];
	size_t suffix_length = strlen(name_suffix);
	if (!directory)
		return -1;
	while ((entry = readdir(directory))) {
		size_t name_length;
		if (strncmp(entry->d_name, "remoteproc", 10) != 0)
			continue;
		snprintf(candidate, sizeof(candidate), "/sys/class/remoteproc/%s/name",
			 entry->d_name);
		if (read_text(candidate, name, sizeof(name)))
			continue;
		name_length = strlen(name);
		if (name_length < suffix_length ||
		    strcmp(name + name_length - suffix_length, name_suffix) != 0)
			continue;
		snprintf(path, path_size, "/sys/class/remoteproc/%s", entry->d_name);
		closedir(directory);
		return 0;
	}
	closedir(directory);
	errno = ENODEV;
	return -1;
}

int mt_remoteproc_stop(const char *name_suffix)
{
	char root[256], path[320], state[64];
	if (mt_remoteproc_find(name_suffix, root, sizeof(root)))
		return -1;
	snprintf(path, sizeof(path), "%s/state", root);
	if (!read_text(path, state, sizeof(state)) && strcmp(state, "offline") == 0)
		return 0;
	return write_text(path, "stop");
}

int mt_remoteproc_start(const char *name_suffix, const char *firmware)
{
	char root[256], path[320], state[64];
	if (mt_remoteproc_find(name_suffix, root, sizeof(root)))
		return -1;
	snprintf(path, sizeof(path), "%s/state", root);
	if (!read_text(path, state, sizeof(state)) && strcmp(state, "running") == 0)
		return 0;
	snprintf(path, sizeof(path), "%s/firmware", root);
	if (write_text(path, firmware))
		return -1;
	snprintf(path, sizeof(path), "%s/state", root);
	return write_text(path, "start");
}
