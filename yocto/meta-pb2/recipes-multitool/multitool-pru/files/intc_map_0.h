/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef MULTITOOL_INTC_MAP_0_H
#define MULTITOOL_INTC_MAP_0_H
#include <rsc_types.h>
#pragma DATA_SECTION(my_irq_rsc, ".pru_irq_map")
#pragma RETAIN(my_irq_rsc)
struct pru_irq_rsc my_irq_rsc = { 0, 1, { { 17, 0, 0 } } };
#endif
