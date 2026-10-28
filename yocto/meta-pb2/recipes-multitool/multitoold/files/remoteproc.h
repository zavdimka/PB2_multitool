/* SPDX-License-Identifier: MIT */
#ifndef MULTITOOL_REMOTEPROC_H
#define MULTITOOL_REMOTEPROC_H

#include <stddef.h>

int mt_remoteproc_find(const char *name_suffix, char *path, size_t path_size);
int mt_remoteproc_start(const char *name_suffix, const char *firmware);
int mt_remoteproc_stop(const char *name_suffix);

#endif
