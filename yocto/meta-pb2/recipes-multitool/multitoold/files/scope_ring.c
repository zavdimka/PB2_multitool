/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE
#include "scope_ring.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t block_stride(const struct mt_scope_ring_header *header)
{
	return sizeof(struct mt_scope_block_header) + header->block_size;
}

int mt_scope_ring_create(struct mt_scope_ring *ring, const char *path,
			 uint32_t block_size, uint32_t block_count)
{
	size_t size;
	void *mapping;
	int fd;

	if (!ring || !path || !block_size || block_count < 2) {
		errno = EINVAL;
		return -1;
	}
	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
	size = sizeof(struct mt_scope_ring_header) +
		block_count * (sizeof(struct mt_scope_block_header) + block_size);
	fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0660);
	if (fd < 0)
		return -1;
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return -1;
	}
	mapping = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mapping == MAP_FAILED) {
		close(fd);
		return -1;
	}
	memset(mapping, 0, size);
	ring->fd = fd;
	ring->mapping = mapping;
	ring->mapping_size = size;
	ring->header = mapping;
	ring->header->magic = MT_SCOPE_RING_MAGIC;
	ring->header->version = MT_SCOPE_RING_VERSION;
	ring->header->header_size = sizeof(*ring->header);
	ring->header->block_size = block_size;
	ring->header->block_count = block_count;
	atomic_store_explicit(&ring->header->write_sequence, 0, memory_order_relaxed);
	return 0;
}

void mt_scope_ring_close(struct mt_scope_ring *ring)
{
	if (!ring)
		return;
	if (ring->mapping && ring->mapping != MAP_FAILED)
		munmap(ring->mapping, ring->mapping_size);
	if (ring->fd >= 0)
		close(ring->fd);
	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
}

int mt_scope_ring_push(struct mt_scope_ring *ring, const void *data,
		       uint32_t size, uint64_t monotonic_ns, uint32_t flags)
{
	struct mt_scope_block_header *block;
	uint8_t *payload;
	uint64_t sequence;
	size_t offset;

	if (!ring || !ring->header || !data || size > ring->header->block_size) {
		errno = EINVAL;
		return -1;
	}
	sequence = atomic_load_explicit(&ring->header->write_sequence,
					memory_order_relaxed) + 1u;
	offset = sizeof(*ring->header) +
		(sequence % ring->header->block_count) * block_stride(ring->header);
	block = (struct mt_scope_block_header *)((uint8_t *)ring->mapping + offset);
	payload = (uint8_t *)block + sizeof(*block);

	/* A zero sequence means the slot is being updated. Readers retry it. */
	atomic_store_explicit(&block->sequence, 0, memory_order_relaxed);
	memcpy(payload, data, size);
	block->monotonic_ns = monotonic_ns;
	block->payload_bytes = size;
	block->flags = flags;
	atomic_thread_fence(memory_order_release);
	atomic_store_explicit(&block->sequence, sequence, memory_order_release);
	atomic_fetch_add_explicit(&ring->header->bytes_received, size,
				  memory_order_relaxed);
	atomic_store_explicit(&ring->header->write_sequence, sequence,
			      memory_order_release);
	return 0;
}
