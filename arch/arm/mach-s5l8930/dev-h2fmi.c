/**
 * Copyright (c) 2016 Erfan Abdi.
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/dma-mapping.h>

#include <asm/mach-types.h>

#include <plat/h2fmi.h>
#include <mach/map.h>
#include <mach/irqs.h>

static struct resource h2fmi0_res[] = {
	[0] = {
		.start = PA_FMI0,
		.end = PA_FMI0 + SZ_FMI0 -1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PA_FMI1,
		.end = PA_FMI1 + SZ_FMI1 -1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = PA_FMI2,
		.end = PA_FMI2 + SZ_FMI2 -1,
		.flags = IORESOURCE_MEM,
	},
	[3] = {
		.start = IRQ_FMI0,
		.end = IRQ_FMI0,
		.flags = IORESOURCE_IRQ,
	},
};

static struct resource h2fmi1_res[] = {
	[0] = {
		.start = PA_FMI3,
		.end = PA_FMI3 + SZ_FMI3 -1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PA_FMI4,
		.end = PA_FMI4 + SZ_FMI4 -1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = PA_FMI5,
		.end = PA_FMI5 + SZ_FMI5 -1,
		.flags = IORESOURCE_MEM,
	},
	[3] = {
		.start = IRQ_FMI1,
		.end = IRQ_FMI1,
		.flags = IORESOURCE_IRQ,
	},
};

#if defined(CONFIG_MACH_IPHONE_4)
static struct h2fmi_smth h2fmi_smth_i4 = { { 0, 0, 0, 3, 4, 3, 4, 0 }, { 0xF0F, 0, 0 } };
#endif
#if defined(CONFIG_MACH_IPAD_1G)
static struct h2fmi_smth h2fmi_smth_ipad = { { 0, 0, 0, 3, 3, 4, 4, 0 }, { 0x3333, 0xCCCC, 0 } };
#endif
#if defined(CONFIG_MACH_IPOD_TOUCH_4G)
static struct h2fmi_smth h2fmi_smth_ipt4g = { { 0, 0, 0, 5, 5, 5, 5, 0 }, { 0x3333, 0xCCCC, 0 } };
#endif
#if defined(CONFIG_MACH_APPLETV_2G)
static struct h2fmi_smth h2fmi_smth_atv2g = { { 0, 0, 0, 3, 3, 3, 4, 0 }, { 0x3333, 0xCCCC, 0 } };
#endif

static struct apple_vfl vfl = {
	.max_devices = 2,
};
static struct apple_ftl ftl;

static struct h2fmi_platform_data pdata0 = {
	.ecc_step_shift = 10,

	.dma0 = 4,
	.dma1 = 5,

	.pid0 = 1,
	.pid1 = 2,

	.vfl = &vfl,
};

static struct h2fmi_platform_data pdata1 = {
	.ecc_step_shift = 10,

	.dma0 = 6,
	.dma1 = 7,

	.pid0 = 3,
	.pid1 = 4,

	.vfl = &vfl,
};

static struct platform_device h2fmi0 = {
	.name = "apple-h2fmi",
	.id = 0,

	.resource = h2fmi0_res,
	.num_resources = ARRAY_SIZE(h2fmi0_res),

	.dev = {
		.platform_data = &pdata0,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

static struct platform_device h2fmi1 = {
	.name = "apple-h2fmi",
	.id = 1,

	.resource = h2fmi1_res,
	.num_resources = ARRAY_SIZE(h2fmi1_res),

	.dev = {
		.platform_data = &pdata1,
		.coherent_dma_mask = DMA_BIT_MASK(32),
	},
};

int s5l8930_register_h2fmi(void)
{
	int ret;

#ifdef CONFIG_MACH_IPHONE_4
	if(machine_is_iphone_4())
	{
		pdata0.smth = &h2fmi_smth_i4;
		pdata1.smth = &h2fmi_smth_i4;
	}
#endif

#ifdef CONFIG_MACH_IPAD_1G
	if(machine_is_ipad_1g())
	{
		pdata0.smth = &h2fmi_smth_ipad;
		pdata1.smth = &h2fmi_smth_ipad;
	}
#endif

#ifdef CONFIG_MACH_IPOD_TOUCH_4G
	if(machine_is_ipod_touch_4g())
	{
		pdata0.smth = &h2fmi_smth_ipt4g;
		pdata1.smth = &h2fmi_smth_ipt4g;
	}
#endif

#ifdef CONFIG_MACH_APPLETV_2G
	if(machine_is_appletv_2g())
	{
		pdata0.smth = &h2fmi_smth_atv2g;
		pdata1.smth = &h2fmi_smth_atv2g;
	}
#endif

	apple_vfl_init(&vfl);

	ret = platform_device_register(&h2fmi0);
	if(ret)
		return ret;

	ret = platform_device_register(&h2fmi1);
	if(ret)
		return ret;


	return 0;
}

int register_vfl(void)
{
	int ret = 0;
	wait_for_device_probe();
	ret = apple_vfl_register(&vfl, APPLE_VFL_NEW_STYLE);
	if (ret)
		return ret;

	ret = apple_ftl_register(&ftl,&vfl);
	if (ret)
		return ret;
	get_encryption_keys(&vfl);
	ret = iphone_block();	//TODO
	return ret;
}
late_initcall(register_vfl);

