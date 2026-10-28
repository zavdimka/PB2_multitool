/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MULTITOOL_PRU_RESOURCE_TABLE_H
#define MULTITOOL_PRU_RESOURCE_TABLE_H

#include <stddef.h>
#include <rsc_types.h>
#include <pru_virtio_ids.h>

#define PRU_RPMSG_VQ0_SIZE 16
#define PRU_RPMSG_VQ1_SIZE 16
#define VIRTIO_RPMSG_F_NS 0
#define RPMSG_FEATURES (1u << VIRTIO_RPMSG_F_NS)

struct mt_resource_table {
	struct resource_table base;
	uint32_t offset[1];
	struct fw_rsc_vdev rpmsg_vdev;
	struct fw_rsc_vdev_vring rpmsg_vring0;
	struct fw_rsc_vdev_vring rpmsg_vring1;
};

#pragma DATA_SECTION(resourceTable, ".resource_table")
#pragma RETAIN(resourceTable)
struct mt_resource_table resourceTable = {
	1, 1, 0, 0,
	{ offsetof(struct mt_resource_table, rpmsg_vdev) },
	{
		(uint32_t)TYPE_VDEV, (uint32_t)VIRTIO_ID_RPMSG, 0,
		RPMSG_FEATURES, 0, 0, 0, 2, { 0, 0 },
	},
	{ FW_RSC_ADDR_ANY, 16, PRU_RPMSG_VQ0_SIZE, 0, 0 },
	{ FW_RSC_ADDR_ANY, 16, PRU_RPMSG_VQ1_SIZE, 0, 0 },
};

#endif
