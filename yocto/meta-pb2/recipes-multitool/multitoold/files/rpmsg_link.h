/* SPDX-License-Identifier: MIT */
#ifndef MULTITOOL_RPMSG_LINK_H
#define MULTITOOL_RPMSG_LINK_H

#include <stdint.h>
#include "multitool_protocol.h"

int mt_rpmsg_open_port(uint32_t remote_port, unsigned timeout_ms);
int mt_rpmsg_exchange(int fd, const struct mt_message *request,
		      struct mt_message *reply, unsigned timeout_ms);
int mt_rpmsg_hello(uint32_t remote_port, struct mt_hello *hello);

#endif
