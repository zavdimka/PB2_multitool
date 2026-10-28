/* SPDX-License-Identifier: MIT */
#include <stdint.h>
#include <string.h>
#include <pru_intc.h>
#include <pru_rpmsg.h>
#include "resource_table.h"
#include "intc_map_1.h"
#include "multitool_protocol.h"

volatile register uint32_t __R30;
volatile register uint32_t __R31;

#define HOST_INT (1u << 31)
#define TO_ARM_HOST 18
#define FROM_ARM_HOST 19
#define CHAN_NAME "rpmsg-raw"
#define CHAN_PORT 31
#define VIRTIO_CONFIG_S_DRIVER_OK 4
#define PRU_CLOCK_HZ 200000000u
#define MAIN_PADCFG_FIRST 0x000f4000u
#define MAIN_PADCFG_LAST  0x000f42acu

static struct mt_programmer_config config;

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

static uint8_t parity32(uint32_t value)
{
	value ^= value >> 16;
	value ^= value >> 8;
	value ^= value >> 4;
	value ^= value >> 2;
	value ^= value >> 1;
	return (uint8_t)(value & 1u);
}

static uint8_t config_valid(void)
{
	return config.tck_mask != 0u && config.tms_mask != 0u &&
		config.tdi_mask != 0u && config.tdo_mask != 0u &&
		config.swdio_direction_mask != 0u &&
		config.swdio_pad_address >= MAIN_PADCFG_FIRST &&
		config.swdio_pad_address <= MAIN_PADCFG_LAST &&
		(config.swdio_pad_address & 3u) == 0u;
}

static void set_swdio_drive(uint8_t drive)
{
	volatile uint32_t *pad =
		(volatile uint32_t *)config.swdio_pad_address;

	__R30 &= ~config.tck_mask;
	if (drive) {
		*pad = config.swdio_pad_output;
		__R30 |= config.swdio_direction_mask;
	} else {
		__R30 &= ~config.swdio_direction_mask;
		*pad = config.swdio_pad_input;
	}
}

static void swd_output_bit(uint8_t value)
{
	__R30 &= ~config.tck_mask;
	if (value)
		__R30 |= config.tms_mask;
	else
		__R30 &= ~config.tms_mask;
	delay_cycles(config.delay_cycles);
	__R30 |= config.tck_mask;
	delay_cycles(config.delay_cycles);
}

static uint8_t swd_input_bit(void)
{
	uint8_t value;
	__R30 &= ~config.tck_mask;
	delay_cycles(config.delay_cycles);
	__R30 |= config.tck_mask;
	delay_cycles(config.delay_cycles);
	value = (__R31 & config.tms_mask) != 0u;
	return value;
}

static void swd_idle(uint32_t cycles)
{
	uint32_t i;
	for (i = 0; i < cycles; ++i)
		swd_output_bit(0u);
}

static void swd_write_u32(uint32_t value, uint16_t bits)
{
	uint16_t i;
	for (i = 0; i < bits; ++i)
		swd_output_bit((uint8_t)((value >> i) & 1u));
}

static uint32_t swd_read_u32(uint16_t bits)
{
	uint16_t i;
	uint32_t value = 0u;
	for (i = 0; i < bits; ++i) {
		if (swd_input_bit())
			value |= 1u << i;
	}
	return value;
}

static uint8_t swd_targetsel(uint8_t command)
{
	return (command & (MT_SWD_CMD_APNDP | MT_SWD_CMD_RNW |
		MT_SWD_CMD_A32)) == MT_SWD_CMD_A32;
}

static void run_swd_transfer(const struct mt_swd_transfer *transfer,
			     struct mt_swd_result *result)
{
	uint8_t command = transfer->command | 0x81u;
	uint8_t read = (command & MT_SWD_CMD_RNW) != 0u;
	uint8_t ack;
	uint8_t data_parity;

	set_swdio_drive(1u);
	swd_write_u32(command, 8u);
	set_swdio_drive(0u);
	(void)swd_input_bit(); /* request-to-ACK turnaround */
	ack = (uint8_t)swd_read_u32(3u);
	result->ack = ack;

	if (read) {
		result->data = swd_read_u32(32u);
		data_parity = swd_input_bit();
		if (ack == MT_SWD_ACK_OK && data_parity != parity32(result->data))
			result->flags |= MT_SWD_RESULT_PARITY_ERROR;
		(void)swd_input_bit(); /* target-to-host turnaround */
		set_swdio_drive(1u);
	} else {
		(void)swd_input_bit(); /* target-to-host turnaround */
		set_swdio_drive(1u);
		swd_write_u32(transfer->data, 32u);
		swd_output_bit(parity32(transfer->data));
	}

	swd_idle(transfer->idle_cycles);
}

static void handle_jtag_stream(struct mt_message *reply,
			       const struct mt_message *request)
{
	uint16_t i;
	uint16_t samples = 0u;
	uint16_t count = request->payload.jtag_stream.count;
	uint32_t data_mask = config.tck_mask | config.tms_mask | config.tdi_mask;

	if (count > MT_JTAG_STREAM_MAX_OPS)
		count = MT_JTAG_STREAM_MAX_OPS;
	init_reply(reply, request, MT_CMD_JTAG_STREAM,
		   4u + ((request->payload.jtag_stream.sample_count + 7u) / 8u));
	reply->payload.jtag_stream_result.count = count;
	for (i = 0; i < count; ++i) {
		uint8_t op = request->payload.jtag_stream.ops[i];
		uint32_t value = 0u;
		if (op & MT_JTAG_OP_TCK)
			value |= config.tck_mask;
		if (op & MT_JTAG_OP_TMS)
			value |= config.tms_mask;
		if (op & MT_JTAG_OP_TDI)
			value |= config.tdi_mask;
		__R30 = (__R30 & ~data_mask) | value;
		delay_cycles(config.delay_cycles);
		if (op & MT_JTAG_OP_SAMPLE) {
			if (__R31 & config.tdo_mask)
				reply->payload.jtag_stream_result.samples[samples >> 3] |=
					(uint8_t)(1u << (samples & 7u));
			samples++;
		}
	}
	reply->payload.jtag_stream_result.sample_count = samples;
	reply->header.payload_size = 4u + ((samples + 7u) / 8u);
}

static void handle_swd_sequence(struct mt_message *reply,
				const struct mt_message *request)
{
	uint16_t i;
	uint16_t bits = request->payload.swd_sequence.bit_count;
	init_reply(reply, request, MT_CMD_SWD_SEQUENCE,
		   sizeof(reply->payload.reply));
	if (bits > MT_SWD_SEQUENCE_MAX_BITS || !config_valid()) {
		reply->payload.reply.status = MT_STATUS_BAD_ARGUMENT;
		return;
	}
	set_swdio_drive(1u);
	for (i = 0; i < bits; ++i)
		swd_output_bit((request->payload.swd_sequence.bits[i >> 3] >>
				(i & 7u)) & 1u);
	reply->payload.reply.status = MT_STATUS_OK;
}

static void handle_swd_batch(struct mt_message *reply,
			     const struct mt_message *request)
{
	uint16_t i;
	uint16_t count = request->payload.swd_batch.count;
	if (count > MT_SWD_MAX_TRANSFERS)
		count = MT_SWD_MAX_TRANSFERS;
	init_reply(reply, request, MT_CMD_SWD_TRANSFER,
		   4u + count * sizeof(struct mt_swd_result));
	reply->payload.swd_batch_result.count = count;
	for (i = 0; i < count; ++i) {
		const struct mt_swd_transfer *transfer =
			&request->payload.swd_batch.transfers[i];
		struct mt_swd_result *result =
			&reply->payload.swd_batch_result.results[i];
		run_swd_transfer(transfer, result);
		reply->payload.swd_batch_result.completed = i + 1u;
		if (!swd_targetsel(transfer->command) &&
		    result->ack != MT_SWD_ACK_OK)
			break;
		if (result->flags & MT_SWD_RESULT_PARITY_ERROR)
			break;
	}
	set_swdio_drive(1u);
	swd_idle(request->payload.swd_batch.final_idle_cycles);
	reply->header.payload_size = 4u +
		reply->payload.swd_batch_result.completed *
		sizeof(struct mt_swd_result);
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
	for (;;) {
		if (!(__R31 & HOST_INT))
			continue;
		CT_INTC.STATUS_CLR_INDEX_REG_bit.STATUS_CLR_INDEX = FROM_ARM_HOST;
		while (pru_rpmsg_receive(&transport, &src, &dst, &request,
					 &length) == PRU_RPMSG_SUCCESS) {
			uint32_t result = MT_STATUS_OK;
			if (length < sizeof(struct mt_header) ||
			    request.header.magic != MT_PROTOCOL_MAGIC)
				result = MT_STATUS_BAD_MAGIC;
			else if (request.header.version != MT_PROTOCOL_VERSION)
				result = MT_STATUS_BAD_VERSION;

			if (result != MT_STATUS_OK) {
				init_reply(&reply, &request, request.header.command,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = result;
			} else if (request.header.command == MT_CMD_HELLO) {
				init_reply(&reply, &request, MT_CMD_HELLO,
					   sizeof(reply.payload.hello));
				reply.payload.hello.engine = MT_ENGINE_PROGRAMMER;
				reply.payload.hello.capabilities =
					MT_CAP_JTAG_STREAM | MT_CAP_SWD_TRANSFER |
					MT_CAP_RUNTIME_PINS | MT_CAP_ADAPTER_SPEED;
				reply.payload.hello.max_payload = sizeof(reply.payload);
				reply.payload.hello.clock_hz = PRU_CLOCK_HZ;
			} else if (request.header.command == MT_CMD_PROGRAMMER_CONFIG) {
				config = request.payload.programmer_config;
				init_reply(&reply, &request, MT_CMD_PROGRAMMER_CONFIG,
					   sizeof(reply.payload.reply));
				if (!config_valid()) {
					reply.payload.reply.status = MT_STATUS_BAD_ARGUMENT;
				} else {
					__R30 = (__R30 & ~config.direction_mask) |
						(config.direction_value & config.direction_mask);
					__R30 = (__R30 & ~config.safe_mask) |
						(config.safe_value & config.safe_mask);
					set_swdio_drive(1u);
					reply.payload.reply.status = MT_STATUS_OK;
				}
			} else if (request.header.command == MT_CMD_ADAPTER_SPEED) {
				init_reply(&reply, &request, MT_CMD_ADAPTER_SPEED,
					   sizeof(reply.payload.reply));
				config.delay_cycles = request.payload.adapter_speed.delay_cycles;
				reply.payload.reply.status = MT_STATUS_OK;
			} else if (request.header.command == MT_CMD_JTAG_STREAM) {
				handle_jtag_stream(&reply, &request);
			} else if (request.header.command == MT_CMD_PROGRAMMER_RESET) {
				const struct mt_programmer_reset *reset =
					&request.payload.programmer_reset;
				uint32_t mask = config.srst_mask | config.trst_mask;
				uint32_t value;
				if (config.reset_active_low)
					value = (reset->srst ? 0u : config.srst_mask) |
						(reset->trst ? 0u : config.trst_mask);
				else
					value = (reset->srst ? config.srst_mask : 0u) |
						(reset->trst ? config.trst_mask : 0u);
				__R30 = (__R30 & ~mask) | value;
				init_reply(&reply, &request, MT_CMD_PROGRAMMER_RESET,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = MT_STATUS_OK;
			} else if (request.header.command == MT_CMD_SWD_SEQUENCE) {
				handle_swd_sequence(&reply, &request);
			} else if (request.header.command == MT_CMD_SWD_TRANSFER) {
				handle_swd_batch(&reply, &request);
			} else if (request.header.command == MT_CMD_STOP) {
				if (config_valid())
					set_swdio_drive(0u);
				__R30 &= ~config.direction_mask;
				init_reply(&reply, &request, MT_CMD_STOP,
					   sizeof(reply.payload.reply));
				reply.payload.reply.status = MT_STATUS_OK;
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
