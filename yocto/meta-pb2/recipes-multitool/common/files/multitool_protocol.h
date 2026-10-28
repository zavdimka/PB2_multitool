/* SPDX-License-Identifier: MIT */
#ifndef MULTITOOL_PROTOCOL_H
#define MULTITOOL_PROTOCOL_H

#include <stdint.h>

#define MT_PROTOCOL_MAGIC 0x4d543031u /* "MT01" */
#define MT_PROTOCOL_VERSION 3u
#define MT_JTAG_STREAM_MAX_OPS 400u
#define MT_JTAG_SAMPLE_BYTES ((MT_JTAG_STREAM_MAX_OPS + 7u) / 8u)
#define MT_SWD_MAX_TRANSFERS 32u
#define MT_SWD_SEQUENCE_MAX_BITS 256u
#define MT_SWD_SEQUENCE_BYTES ((MT_SWD_SEQUENCE_MAX_BITS + 7u) / 8u)
#define MT_LOGIC_CHUNK_SAMPLES 192u

#define MT_CAP_JTAG_STREAM (1u << 0)
#define MT_CAP_SWD_TRANSFER (1u << 1)
#define MT_CAP_RUNTIME_PINS (1u << 2)
#define MT_CAP_ADAPTER_SPEED (1u << 3)

#define MT_JTAG_OP_TDI (1u << 0)
#define MT_JTAG_OP_TMS (1u << 1)
#define MT_JTAG_OP_TCK (1u << 2)
#define MT_JTAG_OP_SAMPLE (1u << 3)

#define MT_SWD_CMD_APNDP (1u << 1)
#define MT_SWD_CMD_RNW (1u << 2)
#define MT_SWD_CMD_A32 (3u << 3)
#define MT_SWD_ACK_OK 1u
#define MT_SWD_ACK_WAIT 2u
#define MT_SWD_ACK_FAULT 4u
#define MT_SWD_RESULT_PARITY_ERROR (1u << 0)

enum mt_engine {
	MT_ENGINE_LOGIC = 1,
	MT_ENGINE_PROGRAMMER = 2,
	MT_ENGINE_SCOPE = 3,
};

enum mt_command {
	MT_CMD_HELLO = 1,
	MT_CMD_STATUS = 2,
	MT_CMD_STOP = 3,
	MT_CMD_LOGIC_CONFIG = 0x100,
	MT_CMD_LOGIC_ARM = 0x101,
	MT_CMD_LOGIC_READ = 0x102,
	MT_CMD_PROGRAMMER_CONFIG = 0x200,
	MT_CMD_JTAG_STREAM = 0x201,
	MT_CMD_ADAPTER_SPEED = 0x202,
	MT_CMD_PROGRAMMER_RESET = 0x203,
	MT_CMD_SWD_SEQUENCE = 0x210,
	MT_CMD_SWD_TRANSFER = 0x211,
};

enum mt_status {
	MT_STATUS_OK = 0,
	MT_STATUS_BAD_MAGIC = 1,
	MT_STATUS_BAD_VERSION = 2,
	MT_STATUS_BAD_COMMAND = 3,
	MT_STATUS_BAD_ARGUMENT = 4,
	MT_STATUS_BUSY = 5,
	MT_STATUS_NOT_READY = 6,
	MT_STATUS_IO_ERROR = 7,
};

struct mt_header {
	uint32_t magic;
	uint16_t version;
	uint16_t command;
	uint32_t sequence;
	uint32_t payload_size;
};

struct mt_hello {
	uint32_t engine;
	uint32_t capabilities;
	uint32_t max_payload;
	uint32_t clock_hz;
};

struct mt_reply {
	uint32_t status;
	uint32_t detail;
};

struct mt_logic_config {
	uint32_t input_mask;
	uint32_t direction_mask;
	uint32_t direction_value;
	uint32_t sample_delay_cycles;
	uint32_t trigger_mask;
	uint32_t trigger_value;
	uint32_t trigger_edge_mask;
	uint16_t pretrigger_samples;
	uint16_t posttrigger_samples;
};

struct mt_logic_read {
	uint16_t offset;
	uint16_t count;
};

struct mt_logic_chunk {
	uint16_t offset;
	uint16_t count;
	uint16_t total;
	uint16_t trigger_index;
	uint32_t capture_sequence;
	uint16_t samples[MT_LOGIC_CHUNK_SAMPLES];
};

struct mt_programmer_config {
	uint32_t tck_mask;
	uint32_t tms_mask;
	uint32_t tdi_mask;
	uint32_t tdo_mask;
	uint32_t srst_mask;
	uint32_t trst_mask;
	uint32_t safe_mask;
	uint32_t safe_value;
	uint32_t direction_mask;
	uint32_t direction_value;
	uint32_t swdio_direction_mask;
	uint32_t swdio_pad_address;
	uint32_t swdio_pad_output;
	uint32_t swdio_pad_input;
	uint32_t delay_cycles;
	uint32_t reset_active_low;
};

struct mt_adapter_speed {
	uint32_t delay_cycles;
};

struct mt_programmer_reset {
	uint8_t srst;
	uint8_t trst;
	uint16_t reserved;
};

struct mt_jtag_stream {
	uint16_t count;
	uint16_t sample_count;
	uint8_t ops[MT_JTAG_STREAM_MAX_OPS];
};

struct mt_jtag_stream_result {
	uint16_t count;
	uint16_t sample_count;
	uint8_t samples[MT_JTAG_SAMPLE_BYTES];
};

struct mt_swd_sequence {
	uint16_t bit_count;
	uint16_t reserved;
	uint8_t bits[MT_SWD_SEQUENCE_BYTES];
};

struct mt_swd_transfer {
	uint8_t command;
	uint8_t reserved;
	uint16_t idle_cycles;
	uint32_t data;
};

struct mt_swd_batch {
	uint16_t count;
	uint16_t final_idle_cycles;
	struct mt_swd_transfer transfers[MT_SWD_MAX_TRANSFERS];
};

struct mt_swd_result {
	uint32_t data;
	uint8_t ack;
	uint8_t flags;
	uint16_t reserved;
};

struct mt_swd_batch_result {
	uint16_t count;
	uint16_t completed;
	struct mt_swd_result results[MT_SWD_MAX_TRANSFERS];
};

struct mt_message {
	struct mt_header header;
	union {
		struct mt_hello hello;
		struct mt_reply reply;
		struct mt_logic_config logic_config;
		struct mt_logic_read logic_read;
		struct mt_logic_chunk logic_chunk;
		struct mt_programmer_config programmer_config;
		struct mt_adapter_speed adapter_speed;
		struct mt_programmer_reset programmer_reset;
		struct mt_jtag_stream jtag_stream;
		struct mt_jtag_stream_result jtag_stream_result;
		struct mt_swd_sequence swd_sequence;
		struct mt_swd_batch swd_batch;
		struct mt_swd_batch_result swd_batch_result;
	} payload;
};

typedef char mt_rpmsg_message_must_fit[
	(sizeof(struct mt_message) <= 496u) ? 1 : -1];

#endif
