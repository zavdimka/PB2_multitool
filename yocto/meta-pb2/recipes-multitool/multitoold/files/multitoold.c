/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "iio_scope.h"
#include "openocd_bridge.h"
#include "remoteproc.h"
#include "rpmsg_link.h"
#include "scope_ring.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#define SOCKET_PATH "/run/multitool/multitoold.sock"
#define RING_PATH "/run/multitool/scope.ring"
#define RING_BLOCK_SIZE (64u * 1024u)
#define RING_BLOCK_COUNT 256u

static volatile sig_atomic_t terminate_requested;

static void handle_signal(int signal_number)
{
	(void)signal_number;
	terminate_requested = 1;
}

static int make_runtime_directory(void)
{
	if (mkdir("/run/multitool", 0755) && errno != EEXIST)
		return -1;
	return 0;
}

static int create_server_socket(void)
{
	struct sockaddr_un address;
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, SOCKET_PATH, sizeof(address.sun_path) - 1u);
	unlink(SOCKET_PATH);
	if (bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 8)) {
		close(fd);
		return -1;
	}
	chmod(SOCKET_PATH, 0660);
	return fd;
}

static void reply_status(int client, const struct mt_scope_engine *scope,
			 const struct mt_scope_ring *ring)
{
	char response[512];
	int length = snprintf(response, sizeof(response),
		"{\"scope\":\"%s\",\"ring\":\"%s\",\"openocd\":\"/run/multitool/openocd.sock\","
		"\"block_size\":%u,\"block_count\":%u,\"write_sequence\":%llu,\"bytes\":%llu,"
		"\"read_errors\":%llu,\"short_reads\":%llu}\n",
		atomic_load(&scope->running) ? "capturing" : "idle", RING_PATH,
		ring->header->block_size, ring->header->block_count,
		(unsigned long long)atomic_load(&ring->header->write_sequence),
		(unsigned long long)atomic_load(&ring->header->bytes_received),
		(unsigned long long)atomic_load(&ring->header->read_errors),
		(unsigned long long)atomic_load(&ring->header->short_reads));
	(void)!write(client, response, (size_t)length);
}

static void handle_client(int client, struct mt_scope_engine *scope,
			  struct mt_scope_ring *ring)
{
	char command[512];
	struct mt_hello hello0, hello1;
	char device[64] = "ads8688";
	char trigger[128] = "-";
	unsigned channel_mask = 1;
	unsigned buffer_scans = 262144;
	ssize_t length = read(client, command, sizeof(command) - 1u);
	if (length <= 0)
		return;
	command[length] = '\0';
	if (strncmp(command, "STATUS", 6) == 0) {
		reply_status(client, scope, ring);
	} else if (sscanf(command, "SCOPE START %63s %127s %x %u", device, trigger,
			  &channel_mask, &buffer_scans) >= 1) {
		if (mt_scope_start(scope, ring, device, trigger, channel_mask, buffer_scans))
			dprintf(client, "ERR %d %s\n", errno, strerror(errno));
		else
			dprintf(client, "OK scope started ring=%s\n", RING_PATH);
	} else if (strncmp(command, "SCOPE STOP", 10) == 0) {
		mt_scope_stop(scope);
		dprintf(client, "OK scope stopped\n");
	} else if (strncmp(command, "PRU START", 9) == 0) {
		int error0 = mt_remoteproc_start("30074000.pru", "am62x-pru0-fw");
		int saved0 = errno;
		int error1 = mt_remoteproc_start("30078000.pru", "am62x-pru1-fw");
		int saved1 = errno;
		if (error0 || error1)
			dprintf(client, "ERR pru0=%d/%d pru1=%d/%d\n", error0, saved0, error1, saved1);
		else if (mt_rpmsg_hello(30, &hello0) || mt_rpmsg_hello(31, &hello1))
			dprintf(client, "ERR PRUs started, RPMsg HELLO failed: %d %s\n", errno, strerror(errno));
		else
			dprintf(client, "OK PRUs started logic_caps=0x%x programmer_caps=0x%x\n", hello0.capabilities, hello1.capabilities);
	} else if (strncmp(command, "PRU STOP", 8) == 0) {
		mt_remoteproc_stop("30074000.pru");
		mt_remoteproc_stop("30078000.pru");
		dprintf(client, "OK PRUs stopped\n");
	} else {
		dprintf(client, "ERR commands: STATUS | PRU START|STOP | "
			"SCOPE START [iio-name] [trigger|-] [channel-mask-hex] [buffer-scans] | "
			"SCOPE STOP\n");
	}
}

static int ring_self_test(void)
{
	struct mt_scope_ring ring;
	uint8_t sample[32];
	uint64_t sequence;
	memset(sample, 0xa5, sizeof(sample));
	if (mt_scope_ring_create(&ring, "/tmp/multitool-ring-test", 64, 4))
		return 1;
	if (mt_scope_ring_push(&ring, sample, sizeof(sample), 1234, 0))
		return 2;
	sequence = atomic_load(&ring.header->write_sequence);
	mt_scope_ring_close(&ring);
	unlink("/tmp/multitool-ring-test");
	return sequence == 1 ? 0 : 3;
}

int main(int argc, char **argv)
{
	struct mt_scope_ring ring;
	struct mt_scope_engine scope;
	struct mt_openocd_bridge openocd;
	int server;
	if (argc == 2 && strcmp(argv[1], "--self-test") == 0)
		return ring_self_test();
	if (make_runtime_directory()) {
		perror("runtime directory");
		return 1;
	}
	if (mt_scope_ring_create(&ring, RING_PATH, RING_BLOCK_SIZE, RING_BLOCK_COUNT)) {
		perror("scope ring");
		return 1;
	}
	memset(&scope, 0, sizeof(scope));
	scope.device_fd = -1;
	mlockall(MCL_CURRENT | MCL_FUTURE);
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	server = create_server_socket();
	if (server < 0) {
		perror("server socket");
		mt_scope_ring_close(&ring);
		return 1;
	}
	if (mt_openocd_bridge_start(&openocd)) {
		perror("OpenOCD bridge");
		close(server);
		unlink(SOCKET_PATH);
		mt_scope_ring_close(&ring);
		return 1;
	}
	while (!terminate_requested) {
		int client = accept4(server, NULL, NULL, SOCK_CLOEXEC);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		handle_client(client, &scope, &ring);
		close(client);
	}
	mt_openocd_bridge_stop(&openocd);
	mt_scope_stop(&scope);
	close(server);
	unlink(SOCKET_PATH);
	mt_scope_ring_close(&ring);
	return 0;
}