/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <string.h>
#include <pru_intc.h>
#include <pru_rpmsg.h>
#include "resource_table.h"
#include "intc_map_0.h"
#include "multitool_protocol.h"

volatile register uint32_t __R30;
volatile register uint32_t __R31;

#define HOST_INT (1u << 30)
#define TO_ARM_HOST 16
#define FROM_ARM_HOST 17
#define CHAN_NAME "rpmsg-raw"
#define CHAN_PORT 30
#define VIRTIO_CONFIG_S_DRIVER_OK 4
#define LOGIC_CAPACITY 2048u
#define PRU_CLOCK_HZ 200000000u
#define LOGIC_INPUT_MASK ((1u << 8) | (1u << 17) | (1u << 7) | (1u << 1))

static uint16_t capture[LOGIC_CAPACITY];
static struct mt_logic_config config;
static uint16_t capture_total;
static uint16_t capture_origin;
static uint16_t trigger_index;
static uint32_t capture_sequence;

static void delay_cycles(uint32_t cycles)
{
	volatile uint32_t i;
	for (i = 0; i < cycles; ++i)
		__asm(" NOP");
}

static void init_reply(struct mt_message *reply, const struct mt_message *request,
		       uint16_t command, uint32_t payload_size)
{
	memset(reply, 0, sizeof(*reply));
	reply->header.magic = MT_PROTOCOL_MAGIC;
	reply->header.version = MT_PROTOCOL_VERSION;
	reply->header.command = command;
	reply->header.sequence = request->header.sequence;
	reply->header.payload_size = payload_size;
}

static uint32_t validate(const struct mt_message *message, uint16_t length)
{
	if (length < sizeof(struct mt_header) || message->header.magic != MT_PROTOCOL_MAGIC)
		return MT_STATUS_BAD_MAGIC;
	if (message->header.version != MT_PROTOCOL_VERSION)
		return MT_STATUS_BAD_VERSION;
	if (message->header.payload_size > length - sizeof(struct mt_header))
		return MT_STATUS_BAD_ARGUMENT;
	return MT_STATUS_OK;
}

static uint16_t pack_sample(uint32_t value, uint32_t mask)
{
	uint16_t packed = 0;
	unsigned source, target = 0;
	for (source = 0; source < 32 && target < 16; ++source) {
		if (!(mask & (1u << source)))
			continue;
		if (value & (1u << source))
			packed |= (uint16_t)(1u << target);
		target++;
	}
	return packed;
}

static void run_capture(void)
{
	uint32_t previous = __R31 & config.input_mask;
	uint32_t current;
	uint16_t write_index = 0;
	uint16_t pre_count = 0;
	uint16_t post_count = 0;
	uint16_t pre_limit = config.pretrigger_samples;
	uint16_t post_limit = config.posttrigger_samples;
	uint8_t triggered = 0;

	if (pre_limit + post_limit > LOGIC_CAPACITY)
		post_limit = LOGIC_CAPACITY - pre_limit;

	while (!triggered) {
		current = __R31 & config.input_mask;
		capture[write_index] = pack_sample(current, config.input_mask);
		write_index = (write_index + 1u) % LOGIC_CAPACITY;
		if (pre_count < pre_limit)
			pre_count++;

		if (config.trigger_edge_mask) {
			uint32_t changed = (previous ^ current) & config.trigger_edge_mask;
			triggered = changed && ((current & config.trigger_mask) == config.trigger_value);
		} else {
			triggered = ((current & config.trigger_mask) == config.trigger_value);
		}
		previous = current;
		delay_cycles(config.sample_delay_cycles);
	}

	capture_origin = (write_index + LOGIC_CAPACITY - pre_count) % LOGIC_CAPACITY;
	trigger_index = pre_count ? (pre_count - 1u) : 0u;

	while (post_count < post_limit) {
		capture[write_index] = pack_sample(__R31, config.input_mask);
		write_index = (write_index + 1u) % LOGIC_CAPACITY;
		post_count++;
		delay_cycles(config.sample_delay_cycles);
	}
	capture_total = pre_count + post_count;
	capture_sequence++;
}

void main(void)
{
	struct pru_rpmsg_transport transport;
	struct mt_message request;
	struct mt_message reply;
	uint16_t src, dst, length;
	volatile uint8_t *status = &resourceTable.rpmsg_vdev.status;

	CT_INTC.STATUS_CLR_INDEX_REG_bit.STATUS_CLR_INDEX = FROM_ARM_HOST;
	while (!(*status & VIRTIO_CONFIG_S_DRIVER_OK)) { }
	pru_rpmsg_init(&transport, &resourceTable.rpmsg_vring0,
			&resourceTable.rpmsg_vring1, TO_ARM_HOST, FROM_ARM_HOST);
	while (pru_rpmsg_channel(RPMSG_NS_CREATE, &transport, CHAN_NAME,
				 CHAN_PORT) != PRU_RPMSG_SUCCESS) { }

	memset(&config, 0, sizeof(config));
	config.input_mask = LOGIC_INPUT_MASK;
	config.pretrigger_samples = 512;
	config.posttrigger_samples = 512;

	for (;;) {
		if (!(__R31 & HOST_INT))
			continue;
		CT_INTC.STATUS_CLR_INDEX_REG_bit.STATUS_CLR_INDEX = FROM_ARM_HOST;
		while (pru_rpmsg_receive(&transport, &src, &dst, &request,
					 &length) == PRU_RPMSG_SUCCESS) {
			uint32_t result = validate(&request, length);
			if (result != MT_STATUS_OK) {
				init_reply(&reply, &request, request.header.command,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = result;
			} else if (request.header.command == MT_CMD_HELLO) {
				init_reply(&reply, &request, MT_CMD_HELLO,
					   sizeof(reply.payload.hello));
				reply.payload.hello.engine = MT_ENGINE_LOGIC;
				reply.payload.hello.capabilities = 1u;
				reply.payload.hello.max_payload = sizeof(reply.payload);
				reply.payload.hello.clock_hz = PRU_CLOCK_HZ;
			} else if (request.header.command == MT_CMD_LOGIC_CONFIG) {
				config = request.payload.logic_config;
				if (config.pretrigger_samples + config.posttrigger_samples > LOGIC_CAPACITY)
					result = MT_STATUS_BAD_ARGUMENT;
				__R30 = (__R30 & ~config.direction_mask) |
					(config.direction_value & config.direction_mask);
				init_reply(&reply, &request, MT_CMD_LOGIC_CONFIG,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = result;
			} else if (request.header.command == MT_CMD_LOGIC_ARM) {
				run_capture();
				init_reply(&reply, &request, MT_CMD_LOGIC_ARM,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = MT_STATUS_OK;
				reply.payload.reply.detail = capture_total;
			} else if (request.header.command == MT_CMD_LOGIC_READ) {
				uint16_t i;
				uint16_t offset = request.payload.logic_read.offset;
				uint16_t count = request.payload.logic_read.count;
				if (count > MT_LOGIC_CHUNK_SAMPLES)
					count = MT_LOGIC_CHUNK_SAMPLES;
				if (offset >= capture_total)
					count = 0;
				else if (offset + count > capture_total)
					count = capture_total - offset;
				init_reply(&reply, &request, MT_CMD_LOGIC_READ,
					   12u + count * sizeof(uint16_t));
				reply.payload.logic_chunk.offset = offset;
				reply.payload.logic_chunk.count = count;
				reply.payload.logic_chunk.total = capture_total;
				reply.payload.logic_chunk.trigger_index = trigger_index;
				reply.payload.logic_chunk.capture_sequence = capture_sequence;
				for (i = 0; i < count; ++i)
					reply.payload.logic_chunk.samples[i] =
						capture[(capture_origin + offset + i) % LOGIC_CAPACITY];
			} else {
				init_reply(&reply, &request, request.header.command,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = MT_STATUS_BAD_COMMAND;
			}
			pru_rpmsg_send(&transport, dst, src, &reply,
				       sizeof(reply.header) + reply.header.payload_size);
		}
	}
}
