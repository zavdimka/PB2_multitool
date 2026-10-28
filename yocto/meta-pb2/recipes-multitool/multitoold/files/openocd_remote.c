/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LISTEN_ADDRESS "192.168.7.2"
#define LISTEN_PORT 3333
#define OPENOCD_ADDRESS "127.0.0.1"
#define OPENOCD_PORT 3334
#define OWNER_PATH "/run/multitool/openocd-owner"
#define OWNER_TEMP "/run/multitool/openocd-owner.tmp"
#define CONFIG_PATH "/etc/multitool/openocd-remote.conf"
#define SCRIPT_ROOT "/usr/share/openocd/scripts"

static volatile sig_atomic_t terminate_requested;
static pid_t openocd_pid = -1;

struct remote_config {
	char profile[160];
	char transport[16];
};

static void signal_handler(int signal_number)
{
	(void)signal_number;
	terminate_requested = 1;
	if (openocd_pid > 0)
		kill(openocd_pid, SIGTERM);
}

static void sleep_milliseconds(long milliseconds)
{
	struct timespec delay = {
		.tv_sec = milliseconds / 1000,
		.tv_nsec = (milliseconds % 1000) * 1000000L,
	};
	while (nanosleep(&delay, &delay) && errno == EINTR && !terminate_requested)
		;
}

static void publish_owner(const char *owner, const char *peer)
{
	FILE *file = fopen(OWNER_TEMP, "w");
	if (!file)
		return;
	fprintf(file, "%s %s\n", owner, peer ? peer : "-");
	if (fclose(file) == 0)
		(void)rename(OWNER_TEMP, OWNER_PATH);
}

static bool valid_profile(const char *value)
{
	const char *position;
	if (strncmp(value, "target/", 7) != 0 && strncmp(value, "board/", 6) != 0)
		return false;
	if (strstr(value, "..") || !strstr(value, ".cfg"))
		return false;
	for (position = value; *position; ++position) {
		if (!isalnum((unsigned char)*position) && *position != '/' &&
		    *position != '_' && *position != '-' && *position != '.')
			return false;
	}
	return true;
}

static void load_config(struct remote_config *config)
{
	char line[256];
	FILE *file;
	strcpy(config->profile, "target/stm32f4x.cfg");
	strcpy(config->transport, "swd");
	file = fopen(CONFIG_PATH, "r");
	if (!file)
		return;
	while (fgets(line, sizeof(line), file)) {
		char *newline = strpbrk(line, "\r\n");
		char *value;
		if (newline)
			*newline = '\0';
		if (strncmp(line, "PROFILE=", 8) == 0) {
			value = line + 8;
			if (valid_profile(value))
				snprintf(config->profile, sizeof(config->profile), "%s", value);
		} else if (strncmp(line, "TRANSPORT=", 10) == 0) {
			value = line + 10;
			if (strcmp(value, "swd") == 0 || strcmp(value, "jtag") == 0)
				snprintf(config->transport, sizeof(config->transport), "%s", value);
		}
	}
	fclose(file);
}

static int create_listener(void)
{
	struct sockaddr_in address = { 0 };
	int enabled = 1;
	int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0)
		return -1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
	address.sin_family = AF_INET;
	address.sin_port = htons(LISTEN_PORT);
	if (inet_pton(AF_INET, LISTEN_ADDRESS, &address.sin_addr) != 1 ||
	    bind(fd, (struct sockaddr *)&address, sizeof(address)) || listen(fd, 1)) {
		close(fd);
		return -1;
	}
	return fd;
}

static pid_t launch_openocd(const struct remote_config *config)
{
	char transport_command[48];
	pid_t child;
	snprintf(transport_command, sizeof(transport_command),
		 "transport select %s", config->transport);
	child = fork();
	if (child != 0)
		return child;
	execl("/usr/bin/openocd", "openocd",
	      "-s", SCRIPT_ROOT,
	      "-f", "interface/pb2-multitool.cfg",
	      "-c", transport_command,
	      "-f", config->profile,
	      "-c", "bindto 127.0.0.1",
	      "-c", "gdb_port 3334",
	      "-c", "tcl_port disabled",
	      "-c", "telnet_port disabled",
	      (char *)NULL);
	_exit(127);
}

static int connect_openocd(pid_t child)
{
	struct sockaddr_in address = { 0 };
	unsigned attempt;
	address.sin_family = AF_INET;
	address.sin_port = htons(OPENOCD_PORT);
	inet_pton(AF_INET, OPENOCD_ADDRESS, &address.sin_addr);
	for (attempt = 0; attempt < 100 && !terminate_requested; ++attempt) {
		int status;
		int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
		if (fd >= 0) {
			if (connect(fd, (struct sockaddr *)&address, sizeof(address)) == 0)
				return fd;
			close(fd);
		}
		if (waitpid(child, &status, WNOHANG) == child) {
			errno = ECONNREFUSED;
			return -1;
		}
		sleep_milliseconds(50);
	}
	errno = ETIMEDOUT;
	return -1;
}

static int send_all(int fd, const void *buffer, size_t length)
{
	const unsigned char *position = buffer;
	while (length) {
		ssize_t sent = send(fd, position, length, MSG_NOSIGNAL);
		if (sent < 0 && errno == EINTR)
			continue;
		if (sent <= 0)
			return -1;
		position += sent;
		length -= (size_t)sent;
	}
	return 0;
}

static void proxy_connection(int client, int server)
{
	unsigned char buffer[16384];
	struct pollfd descriptors[2] = {
		{ .fd = client, .events = POLLIN },
		{ .fd = server, .events = POLLIN },
	};
	while (!terminate_requested) {
		int ready = poll(descriptors, 2, 500);
		int index;
		if (ready < 0 && errno == EINTR)
			continue;
		if (ready < 0)
			break;
		for (index = 0; index < 2; ++index) {
			int destination = index == 0 ? server : client;
			if (descriptors[index].revents & (POLLERR | POLLHUP | POLLNVAL))
				return;
			if (descriptors[index].revents & POLLIN) {
				ssize_t received = recv(descriptors[index].fd, buffer,
							sizeof(buffer), 0);
				if (received <= 0 || send_all(destination, buffer,
								 (size_t)received))
					return;
			}
		}
	}
}

static void stop_openocd(pid_t child)
{
	unsigned attempt;
	int status;
	if (child <= 0)
		return;
	kill(child, SIGTERM);
	for (attempt = 0; attempt < 20; ++attempt) {
		if (waitpid(child, &status, WNOHANG) == child)
			return;
		sleep_milliseconds(50);
	}
	kill(child, SIGKILL);
	(void)waitpid(child, &status, 0);
}

static void serve_remote(int client, const char *peer)
{
	struct remote_config config;
	int server;
	load_config(&config);
	publish_owner("remote", peer);
	openocd_pid = launch_openocd(&config);
	if (openocd_pid < 0) {
		publish_owner("local", "-");
		return;
	}
	server = connect_openocd(openocd_pid);
	if (server >= 0) {
		proxy_connection(client, server);
		close(server);
	}
	stop_openocd(openocd_pid);
	openocd_pid = -1;
	publish_owner("local", "-");
}

int main(void)
{
	int listener = -1;
	unsigned retries = 0;
	struct sigaction action = { .sa_handler = signal_handler };
	mkdir("/run/multitool", 0755);
	sigemptyset(&action.sa_mask);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTERM, &action, NULL);
	signal(SIGPIPE, SIG_IGN);
	publish_owner("local", "-");

	while (!terminate_requested && listener < 0) {
		listener = create_listener();
		if (listener < 0) {
			if (++retries >= 100) {
				perror("OpenOCD remote listen");
				return 1;
			}
			sleep_milliseconds(100);
		}
	}

	while (!terminate_requested) {
		struct sockaddr_in peer_address;
		socklen_t peer_length = sizeof(peer_address);
		char peer[INET_ADDRSTRLEN] = "unknown";
		int client = accept4(listener, (struct sockaddr *)&peer_address,
				     &peer_length, SOCK_CLOEXEC);
		if (client < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		inet_ntop(AF_INET, &peer_address.sin_addr, peer, sizeof(peer));
		serve_remote(client, peer);
		close(client);
	}

	if (openocd_pid > 0)
		stop_openocd(openocd_pid);
	publish_owner("local", "-");
	if (listener >= 0)
		close(listener);
	return 0;
}
