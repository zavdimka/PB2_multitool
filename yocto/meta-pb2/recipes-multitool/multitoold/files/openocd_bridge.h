/* SPDX-License-Identifier: MIT */
#ifndef MULTITOOL_OPENOCD_BRIDGE_H
#define MULTITOOL_OPENOCD_BRIDGE_H

#include <pthread.h>
#include <stdatomic.h>

struct mt_openocd_bridge {
	pthread_t thread;
	atomic_bool running;
	int server_fd;
};

int mt_openocd_bridge_start(struct mt_openocd_bridge *bridge);
void mt_openocd_bridge_stop(struct mt_openocd_bridge *bridge);

#endif