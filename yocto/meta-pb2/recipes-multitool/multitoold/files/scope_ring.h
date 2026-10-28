/* SPDX-License-Identifier: MIT */
#ifndef MULTITOOL_SCOPE_RING_H
#define MULTITOOL_SCOPE_RING_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#define MT_SCOPE_RING_MAGIC 0x4d545247u /* MTRG */
#define MT_SCOPE_RING_VERSION 1u

struct mt_scope_ring_header {
	uint32_t magic;
	uint16_t version;
	uint16_t header_size;
	uint32_t block_size;
	uint32_t block_count;
	uint32_t scan_bytes;
	uint32_t sample_rate_hz;
	_Atomic uint64_t write_sequence;
	_Atomic uint64_t bytes_received;
	_Atomic uint64_t read_errors;
	_Atomic uint64_t short_reads;
};

struct mt_scope_block_header {
	_Atomic uint64_t sequence;
	uint64_t monotonic_ns;
	uint32_t payload_bytes;
	uint32_t flags;
};

struct mt_scope_ring {
	int fd;
	void *mapping;
	size_t mapping_size;
	struct mt_scope_ring_header *header;
};

int mt_scope_ring_create(struct mt_scope_ring *ring, const char *path,
			 uint32_t block_size, uint32_t block_count);
void mt_scope_ring_close(struct mt_scope_ring *ring);
int mt_scope_ring_push(struct mt_scope_ring *ring, const void *data,
		       uint32_t size, uint64_t monotonic_ns, uint32_t flags);

#endif
