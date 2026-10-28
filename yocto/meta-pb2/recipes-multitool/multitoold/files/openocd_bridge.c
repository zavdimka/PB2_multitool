/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "openocd_bridge.h"
#include "remoteproc.h"
#include "rpmsg_link.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define OPENOCD_SOCKET "/run/multitool/openocd.sock"
#define PIN_CONFIG "/etc/multitool/jtag-pins.conf"
#define PRU1_PORT 31u

struct mt_pin_profile {
	unsigned tck, tms, tdi, tdo, srst, trst;
	unsigned tck_dir, tms_dir, tdi_dir, tdo_dir, srst_dir, trst_dir;
	uint32_t swdio_pad_address;
	uint32_t swdio_pad_output;
	uint32_t swdio_pad_input;
	uint32_t delay_cycles;
	bool reset_active_low;
};

struct connection {
	int socket_fd;
	int rpmsg_fd;
	uint32_t sequence;
	struct mt_pin_profile pins;
};

static uint32_t bit(unsigned index)
{
	return index < 32u ? 1u << index : 0u;
}

static void default_profile(struct mt_pin_profile *pins)
{
	*pins = (struct mt_pin_profile) {
		.tck = 3, .tms = 6, .tdi = 12, .tdo = 14,
		.srst = 16, .trst = 8,
		.tck_dir = 2, .tms_dir = 4, .tdi_dir = 15, .tdo_dir = 11,
		.srst_dir = 19, .trst_dir = 7,
		.swdio_pad_address = 0x000f40d0u,
		.swdio_pad_output = 0x00010002u,
		.swdio_pad_input = 0x00054003u,
		.delay_cycles = 16,
		.reset_active_low = true,
	};
}

static int assign_value(struct mt_pin_profile *pins, const char *key,
			unsigned long value)
{
#define PIN_VALUE(name) if (strcmp(key, #name) == 0) { pins->name = (unsigned)value; return 0; }
	PIN_VALUE(tck)
	PIN_VALUE(tms)
	PIN_VALUE(tdi)
	PIN_VALUE(tdo)
	PIN_VALUE(srst)
	PIN_VALUE(trst)
	PIN_VALUE(tck_dir)
	PIN_VALUE(tms_dir)
	PIN_VALUE(tdi_dir)
	PIN_VALUE(tdo_dir)
	PIN_VALUE(srst_dir)
	PIN_VALUE(trst_dir)
#undef PIN_VALUE
	if (strcmp(key, "swdio_pad_address") == 0)
		pins->swdio_pad_address = (uint32_t)value;
	else if (strcmp(key, "swdio_pad_output") == 0)
		pins->swdio_pad_output = (uint32_t)value;
	else if (strcmp(key, "swdio_pad_input") == 0)
		pins->swdio_pad_input = (uint32_t)value;
	else if (strcmp(key, "delay_cycles") == 0)
		pins->delay_cycles = (uint32_t)value;
	else if (strcmp(key, "reset_active_low") == 0)
		pins->reset_active_low = value != 0;
	else
		return -1;
	return 0;
}

static int load_profile(struct mt_pin_profile *pins)
{
	char line[160];
	FILE *file;
	default_profile(pins);
	file = fopen(PIN_CONFIG, "r");
	if (!file)
		return errno == ENOENT ? 0 : -1;
	while (fgets(line, sizeof(line), file)) {
		char key[64], extra;
		unsigned long value;
		char *start = line;
		while (*start == ' ' || *start == '\t')
			start++;
		if (*start == '#' || *start == '\n' || *start == '\0')
			continue;
		if (sscanf(start, " %63[^= ] = %li %c", key, &value, &extra) != 2 ||
		    assign_value(pins, key, value)) {
			fclose(file);
			errno = EINVAL;
			return -1;
		}
	}
	fclose(file);
	if (!bit(pins->tck) || !bit(pins->tms) || !bit(pins->tdi) ||
	    !bit(pins->tdo) || !bit(pins->srst) || !bit(pins->trst) ||
	    !bit(pins->tck_dir) || !bit(pins->tms_dir) ||
	    !bit(pins->tdi_dir) || !bit(pins->tdo_dir) ||
	    !bit(pins->srst_dir) || !bit(pins->trst_dir) ||
	    pins->delay_cycles == 0u) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

static void request_header(struct connection *connection,
			   struct mt_message *request, uint16_t command,
			   uint32_t payload_size)
{
	memset(request, 0, sizeof(*request));
	request->header.magic = MT_PROTOCOL_MAGIC;
	request->header.version = MT_PROTOCOL_VERSION;
	request->header.command = command;
	request->header.sequence = ++connection->sequence;
	request->header.payload_size = payload_size;
}

static int exchange_reply(struct connection *connection,
			  struct mt_message *request, struct mt_message *reply)
{
	int length = mt_rpmsg_exchange(connection->rpmsg_fd, request, reply, 3000);
	if (length < (int)sizeof(struct mt_header))
		return -1;
	if (reply->header.magic != MT_PROTOCOL_MAGIC ||
	    reply->header.version != MT_PROTOCOL_VERSION ||
	    reply->header.command != request->header.command ||
	    reply->header.sequence != request->header.sequence) {
		errno = EPROTO;
		return -1;
	}
	return length;
}

static int configure_pru(struct connection *connection)
{
	const struct mt_pin_profile *p = &connection->pins;
	struct mt_programmer_config *config;
	struct mt_message request, reply;
	uint32_t outputs = bit(p->tck) | bit(p->tms) | bit(p->tdi) |
			   bit(p->srst) | bit(p->trst);
	uint32_t directions = bit(p->tck_dir) | bit(p->tms_dir) |
			      bit(p->tdi_dir) | bit(p->tdo_dir) |
			      bit(p->srst_dir) | bit(p->trst_dir);

	request_header(connection, &request, MT_CMD_PROGRAMMER_CONFIG,
		       sizeof(request.payload.programmer_config));
	config = &request.payload.programmer_config;
	config->tck_mask = bit(p->tck);
	config->tms_mask = bit(p->tms);
	config->tdi_mask = bit(p->tdi);
	config->tdo_mask = bit(p->tdo);
	config->srst_mask = bit(p->srst);
	config->trst_mask = bit(p->trst);
	config->safe_mask = outputs;
	config->safe_value = bit(p->tms) | bit(p->srst) | bit(p->trst);
	config->direction_mask = directions;
	config->direction_value = directions & ~bit(p->tdo_dir);
	config->swdio_direction_mask = bit(p->tms_dir);
	config->swdio_pad_address = p->swdio_pad_address;
	config->swdio_pad_output = p->swdio_pad_output;
	config->swdio_pad_input = p->swdio_pad_input;
	config->delay_cycles = p->delay_cycles;
	config->reset_active_low = p->reset_active_low ? 1u : 0u;
	if (exchange_reply(connection, &request, &reply) < 0)
		return -1;
	if (reply.header.payload_size != sizeof(reply.payload.reply) ||
	    reply.payload.reply.status != MT_STATUS_OK) {
		errno = EIO;
		return -1;
	}
	return 0;
}

static ssize_t read_full(int fd, void *buffer, size_t length)
{
	uint8_t *position = buffer;
	size_t done = 0;
	while (done < length) {
		ssize_t result = recv(fd, position + done, length - done, 0);
		if (result == 0)
			return 0;
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		done += (size_t)result;
	}
	return (ssize_t)done;
}

static int write_full(int fd, const void *buffer, size_t length)
{
	const uint8_t *position = buffer;
	size_t done = 0;
	while (done < length) {
		ssize_t result = send(fd, position + done, length - done, MSG_NOSIGNAL);
		if (result < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		done += (size_t)result;
	}
	return 0;
}

static int valid_client_request(const struct mt_message *request)
{
	uint32_t size = request->header.payload_size;
	if (request->header.magic != MT_PROTOCOL_MAGIC ||
	    request->header.version != MT_PROTOCOL_VERSION ||
	    size > sizeof(request->payload))
		return 0;
	switch (request->header.command) {
	case MT_CMD_HELLO:
	case MT_CMD_STOP:
		return size == 0u;
	case MT_CMD_ADAPTER_SPEED:
		return size == sizeof(request->payload.adapter_speed) &&
			request->payload.adapter_speed.delay_cycles > 0u;
	case MT_CMD_PROGRAMMER_RESET:
		return size == sizeof(request->payload.programmer_reset);
	case MT_CMD_JTAG_STREAM:
		return size >= 4u &&
			request->payload.jtag_stream.count <= MT_JTAG_STREAM_MAX_OPS &&
			size == 4u + request->payload.jtag_stream.count;
	case MT_CMD_SWD_SEQUENCE:
		return size >= 4u &&
			request->payload.swd_sequence.bit_count <= MT_SWD_SEQUENCE_MAX_BITS &&
			size == 4u + (request->payload.swd_sequence.bit_count + 7u) / 8u;
	case MT_CMD_SWD_TRANSFER:
		return size >= 4u &&
			request->payload.swd_batch.count <= MT_SWD_MAX_TRANSFERS &&
			size == 4u + request->payload.swd_batch.count *
				sizeof(struct mt_swd_transfer);
	default:
		return 0;
	}
}

static void stop_pru_connection(struct connection *connection)
{
	struct mt_message request, reply;
	request_header(connection, &request, MT_CMD_STOP, 0u);
	(void)exchange_reply(connection, &request, &reply);
}

static void serve_client(int socket_fd, const atomic_bool *running)
{
	struct connection connection;
	memset(&connection, 0, sizeof(connection));
	connection.socket_fd = socket_fd;
	connection.rpmsg_fd = -1;
	if (load_profile(&connection.pins)) {
		perror("OpenOCD pin profile");
		return;
	}
	if (mt_remoteproc_start("30078000.pru", "am62x-pru1-fw")) {
		perror("OpenOCD PRU1 start");
		return;
	}
	connection.rpmsg_fd = mt_rpmsg_open_port(PRU1_PORT, 3000);
	if (connection.rpmsg_fd < 0 || configure_pru(&connection)) {
		perror("OpenOCD RPMsg");
		if (connection.rpmsg_fd >= 0)
			close(connection.rpmsg_fd);
		return;
	}

	while (atomic_load(running)) {
		struct mt_message request, reply;
		ssize_t received = read_full(socket_fd, &request.header,
					     sizeof(request.header));
		int reply_length;
		if (received <= 0)
			break;
		if (request.header.payload_size > sizeof(request.payload) ||
		    read_full(socket_fd, &request.payload,
			      request.header.payload_size) <= 0)
			break;
		if (!valid_client_request(&request)) {
			memset(&reply, 0, sizeof(reply));
			reply.header.magic = MT_PROTOCOL_MAGIC;
			reply.header.version = MT_PROTOCOL_VERSION;
			reply.header.command = request.header.command;
			reply.header.sequence = request.header.sequence;
			reply.header.payload_size = sizeof(reply.payload.reply);
			reply.payload.reply.status = MT_STATUS_BAD_ARGUMENT;
			if (write_full(socket_fd, &reply,
				       sizeof(reply.header) + reply.header.payload_size))
				break;
			continue;
		}
		reply_length = exchange_reply(&connection, &request, &reply);
		if (reply_length < 0 ||
		    write_full(socket_fd, &reply, (size_t)reply_length))
			break;
	}
	stop_pru_connection(&connection);
	close(connection.rpmsg_fd);
}

static int make_server_socket(void)
{
	struct sockaddr_un address;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, OPENOCD_SOCKET, sizeof(address.sun_path) - 1u);
	unlink(OPENOCD_SOCKET);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 1)) {
		close(fd);
		return -1;
	}
	chmod(OPENOCD_SOCKET, 0660);
	return fd;
}

static void *bridge_thread(void *argument)
{
	struct mt_openocd_bridge *bridge = argument;
	while (atomic_load(&bridge->running)) {
		struct pollfd wait = { .fd = bridge->server_fd, .events = POLLIN };
		int result = poll(&wait, 1, 250);
		if (result < 0 && errno == EINTR)
			continue;
		if (result <= 0)
			continue;
		if (wait.revents & POLLIN) {
			int client = accept4(bridge->server_fd, NULL, NULL, SOCK_CLOEXEC);
			if (client >= 0) {
				serve_client(client, &bridge->running);
				close(client);
			}
		}
	}
	return NULL;
}

int mt_openocd_bridge_start(struct mt_openocd_bridge *bridge)
{
	int error;
	memset(bridge, 0, sizeof(*bridge));
	bridge->server_fd = make_server_socket();
	if (bridge->server_fd < 0)
		return -1;
	atomic_store(&bridge->running, true);
	error = pthread_create(&bridge->thread, NULL, bridge_thread, bridge);
	if (error) {
		errno = error;
		close(bridge->server_fd);
		unlink(OPENOCD_SOCKET);
		return -1;
	}
	return 0;
}

void mt_openocd_bridge_stop(struct mt_openocd_bridge *bridge)
{
	if (!atomic_exchange(&bridge->running, false))
		return;
	shutdown(bridge->server_fd, SHUT_RDWR);
	pthread_join(bridge->thread, NULL);
	close(bridge->server_fd);
	unlink(OPENOCD_SOCKET);
}
