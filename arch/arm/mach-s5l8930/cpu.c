/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/serial_core.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>

#include <mach/cpu.h>
#include <mach/map.h>
#include <asm/mach/map.h>
#include <plat/irq.h>
#include <asm/page.h>

#include <asm/mach/arch.h>
#include <plat/cpu.h>
#include <plat/clock.h>
#include <plat/devs.h>
#include <plat/cpu.h>
#include <plat/iic.h>
#include <plat/regs-uart.h>
#include <plat/irq-vic-timer.h>
#include <plat/sdhci.h>

static struct map_desc s5l8930_iodesc[] __initdata = {
	{
		.virtual	= (unsigned long)VA_VIC0,
		.pfn		= __phys_to_pfn(PA_VIC0),
		.length		= SZ_VIC,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_VIC1,
		.pfn		= __phys_to_pfn(PA_VIC1),
		.length		= SZ_VIC,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_VIC2,
		.pfn		= __phys_to_pfn(PA_VIC2),
		.length		= SZ_VIC,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_VIC3,
		.pfn		= __phys_to_pfn(PA_VIC3),
		.length		= SZ_VIC,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_UART0,
		.pfn		= __phys_to_pfn(PA_UART0),
		.length		= SZ_UART,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_UART1,
		.pfn		= __phys_to_pfn(PA_UART1),
		.length		= SZ_UART,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_UART2,
		.pfn		= __phys_to_pfn(PA_UART2),
		.length		= SZ_UART,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR0,
		.pfn		= __phys_to_pfn(PA_PMGR0),
		.length		= SZ_PMGR0,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR1,
		.pfn		= __phys_to_pfn(PA_PMGR1),
		.length		= SZ_PMGR1,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR2,
		.pfn		= __phys_to_pfn(PA_PMGR2),
		.length		= SZ_PMGR2,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR3,
		.pfn		= __phys_to_pfn(PA_PMGR3),
		.length		= SZ_PMGR3,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR4,
		.pfn		= __phys_to_pfn(PA_PMGR4),
		.length		= SZ_PMGR4,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR5,
		.pfn		= __phys_to_pfn(PA_PMGR5),
		.length		= SZ_PMGR5,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_PMGR6,
		.pfn		= __phys_to_pfn(PA_PMGR6),
		.length		= SZ_PMGR6,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)VA_GPIO,
		.pfn		= __phys_to_pfn(PA_GPIO),
		.length		= SZ_GPIO,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)S3C_VA_USB_HSPHY,
		.pfn		= __phys_to_pfn(S3C_PA_USB_HSPHY),
		.length		= SZ_USB_PHY,
		.type		= MT_DEVICE,
	},
	{
		.virtual	= (unsigned long)S3C_VA_TIMER,
		.pfn		= __phys_to_pfn(PA_PWM),
		.length		= SZ_PWM,
		.type		= MT_DEVICE,
	},
};

static __init void s5l8930_cpu_map_io(void)
{
	printk("%s\n", __func__);
	iotable_init(s5l8930_iodesc, ARRAY_SIZE(s5l8930_iodesc));
	printk("%s done\n", __func__);
}

static char *sdio0_clocks[] = {
	"sdio",
	NULL,
	NULL,
	NULL,
};

static struct s3c_sdhci_platdata sdio0_pdata = {
	.clocks = sdio0_clocks,
};

static struct resource cdma_res[] = {
	[0] = {
		.start = PA_CDMA,
		.end = PA_CDMA + SZ_CDMA - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = PA_CDMA_AES,
		.end = PA_CDMA_AES + SZ_CDMA_AES - 1,
		.flags = IORESOURCE_MEM,
	},
	[2] = {
		.start = IRQ_CDMA0,
		.end = IRQ_CDMA36,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device cdma_dev = {
	.name = "apple-cdma",
	.id = -1,
	
	.resource = cdma_res,
	.num_resources = ARRAY_SIZE(cdma_res),

	.dev = {
		.coherent_dma_mask = DMA_32BIT_MASK,
	},
};

extern struct platform_device s5l8930_spi0;
extern struct platform_device s5l8930_spi1;

static struct platform_device *s5l8930_devices[] __initdata = {
	&s5l8930_spi0,
	&s5l8930_spi1,
	&s3c_device_i2c0,
	&s3c_device_i2c1,
	&s3c_device_usb_hsotg,
	&s3c_device_hsmmc0,
};

static __init int s5l8930_cpu_init(void)
{
	s3c_sdhci0_set_platdata(&sdio0_pdata);
	s3c_i2c0_set_platdata(NULL);
	s3c_i2c1_set_platdata(NULL);
	platform_add_devices(s5l8930_devices, ARRAY_SIZE(s5l8930_devices));
	return 0;
}

extern __init void s5l8930_cpu_init_clocks(int _xtal);
extern struct s3c24xx_uart_resources s5l_uart_resources[] __initdata;
static __init void s5l8930_cpu_init_uarts(struct s3c2410_uartcfg *cfg, int no)
{
	s3c24xx_init_uartdevs("s5l8900-uart", s5l_uart_resources, cfg, no);
}

static struct cpu_table cpu_id[] __initdata = {
	{
		.idcode			= 0x8930,
		.idmask			= 0xffff,
		.map_io			= s5l8930_cpu_map_io,
		.init			= s5l8930_cpu_init,
		.init_clocks	= s5l8930_cpu_init_clocks,
		.init_uarts		= s5l8930_cpu_init_uarts,
		.name			= "S5L8930",
	},
};

#define S5L8930_UCON_DEFAULT	(S5L8930_UCON_TXIRQMODE |	\
				 S5L8930_UCON_RXIRQMODE)

#define S5L8930_ULCON_DEFAULT	S5L8930_LCON_CS8
#if 0

#define S5L8930_UFCON_DEFAULT	(S5L8930_UFCON_FIFOMODE |	\
				 S3C2440_UFCON_RXTRIG8 |	\
				 S3C2440_UFCON_TXTRIG16)
#else
#define S5L8930_UFCON_DEFAULT	(S5L8930_UFCON_FIFOMODE)
#endif

static struct s3c24xx_uart_clksrc s5l8930_uart_srcs[] = {
	[0] = {
		.name = "uclk0",
		.divisor = 1,
	},
};

static struct s3c2410_uartcfg s5l8930_uartcfgs[] __initdata = {
	[0] = {
		.hwport	     = 0,
		.flags	     = 0,
		.ucon	     = S5L8930_UCON_DEFAULT,
		.ulcon	     = S5L8930_ULCON_DEFAULT,
		.ufcon	     = S5L8930_UFCON_DEFAULT,

		.clocks = s5l8930_uart_srcs,
		.clocks_size = ARRAY_SIZE(s5l8930_uart_srcs),
	},
	[1] = {
		.hwport	     = 1,
		.flags	     = 0,
		.ucon	     = S5L8930_UCON_DEFAULT,
		.ulcon	     = S5L8930_ULCON_DEFAULT,
		.ufcon	     = S5L8930_UFCON_DEFAULT,

		.clocks = s5l8930_uart_srcs,
		.clocks_size = ARRAY_SIZE(s5l8930_uart_srcs),
	},
	[2] = {
		.hwport	     = 2,
		.flags	     = 0,
		.ucon	     = S5L8930_UCON_DEFAULT,
		.ulcon	     = S5L8930_ULCON_DEFAULT,
		.ufcon	     = S5L8930_UFCON_DEFAULT,

		.clocks = s5l8930_uart_srcs,
		.clocks_size = ARRAY_SIZE(s5l8930_uart_srcs),
	},
	[3] = {
		.hwport	     = 3,
		.flags	     = 0,
		.ucon	     = S5L8930_UCON_DEFAULT,
		.ulcon	     = S5L8930_ULCON_DEFAULT,
		.ufcon	     = S5L8930_UFCON_DEFAULT,

		.clocks = s5l8930_uart_srcs,
		.clocks_size = ARRAY_SIZE(s5l8930_uart_srcs),
	},
};

// TODO: This might need implementing!
void s3c_setup_uart_cfg_gpio(unsigned char port)
{
}
EXPORT_SYMBOL(s3c_setup_uart_cfg_gpio);

void __init s5l8930_map_io(void)
{
	s3c_init_cpu(0x8930, cpu_id, ARRAY_SIZE(cpu_id));
	s3c24xx_init_clocks(24000000);
	s3c24xx_init_uarts(s5l8930_uartcfgs, ARRAY_SIZE(s5l8930_uartcfgs));
}

void __init s5l8930_init_irq(void)
{
	u32 vic[] = {~0, ~0, ~0, ~0};
	printk("%s\n", __func__);
	s5l_init_irq(vic, ARRAY_SIZE(vic));
	printk("%s done\n", __func__);
}

void __init s5l8930_init(void)
{
	printk("%s\n", __func__);
	platform_device_register(&cdma_dev);
}
