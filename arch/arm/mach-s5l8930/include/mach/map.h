/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef  _S5L8930_MAP_
#define  _S5L8930_MAP_

#include <asm/memory.h>
#include <plat/map-base.h>

#define PA_VIC(x)		(0xBF200000 + (x*SZ_VIC))
#define VA_VIC(x)		(S3C_VA_IRQ + (x*SZ_VIC))
#define SZ_VIC			0x10000
#define PA_VIC0			PA_VIC(0)
#define VA_VIC0			VA_VIC(0)
#define PA_VIC1			PA_VIC(1)
#define VA_VIC1			VA_VIC(1)
#define PA_VIC2			PA_VIC(2)
#define VA_VIC2			VA_VIC(2)
#define PA_VIC3			PA_VIC(3)
#define VA_VIC3			VA_VIC(3)

#define PA_GPIO			0xBFA00000
#define SZ_GPIO			0x1000
#define VA_GPIO			(S3C_ADDR_CPU(0x800000))

#define VA_PMGR(x)		(S3C_ADDR_CPU(0x100000*x))
#define PA_PMGR0		0xBF100000
#define SZ_PMGR0		0x6000
#define VA_PMGR0		VA_PMGR(0)
#define PA_PMGR1		0x85E00000
#define SZ_PMGR1		0x1000
#define VA_PMGR1		VA_PMGR(1)
#define PA_PMGR2		0x85F00000
#define SZ_PMGR2		0x1000
#define VA_PMGR2		VA_PMGR(2)
#define PA_PMGR3		0x88E00000
#define SZ_PMGR3		0x1000
#define VA_PMGR3		VA_PMGR(3)
#define PA_PMGR4		0x88F00000
#define SZ_PMGR4		0x1000
#define VA_PMGR4		VA_PMGR(4)
#define PA_PMGR5		0x89E00000
#define SZ_PMGR5		0x1000
#define VA_PMGR5		VA_PMGR(5)
#define PA_PMGR6		0x89F00000
#define SZ_PMGR6		0x1000
#define VA_PMGR6		VA_PMGR(6)

#define PA_DEBUG		0xBF701000
#define SZ_DEBUG		0x1000

#define PA_CDMA			0x87000000
#define SZ_CDMA			0x26000
#define PA_CDMA_AES		0x87800000
#define SZ_CDMA_AES		0x9000

#define PA_DART1		0x88D00000
#define SZ_DART1		0x2000
#define PA_DART2		0x89D00000
#define SZ_DART2		0x2000

#define PA_SDIO			0x80000000
#define SZ_SDIO			0x1000

#define PA_SHA			0x80100000
#define SZ_SHA			0x1000

#define PA_CEATA		0x81000000
#define SZ_CEATA		0x1000

#define PA_FMI0			0x81200000
#define SZ_FMI0			0x1000
#define PA_FMI1			0x81240000
#define SZ_FMI1			0x1000
#define PA_FMI2			0x81280000
#define SZ_FMI2			0x1000
#define PA_FMI3			0x81300000
#define SZ_FMI3			0x1000
#define PA_FMI4			0x81340000
#define SZ_FMI4			0x1000
#define PA_FMI5			0x81380000
#define SZ_FMI5			0x1000

#define PA_SPI0			0x82000000
#define SZ_SPI0			0x1000
#define PA_SPI1			0x82100000
#define SZ_SPI1			0x1000

#define PA_UART(x)		(0x82500000 + ((x)*0x100000))
#define VA_UART(x)		(S3C_VA_UART + ((x)*0x4000))
#define S3C_VA_UARTx(x)		VA_UART(x)
#define SZ_UART			0x1000
#define PA_UART0		PA_UART(0)
#define VA_UART0		VA_UART(0)
#define PA_UART1		PA_UART(1)
#define VA_UART1		VA_UART(1)
#define PA_UART2		PA_UART(2)
#define VA_UART2		VA_UART(2)
#define PA_UART3		PA_UART(3)
#define VA_UART3		VA_UART(3)
#define PA_UART4		PA_UART(4)
#define VA_UART4		VA_UART(4)
#define PA_UART5		PA_UART(5)
#define VA_UART5		VA_UART(5)

#define PA_PKE			0x83100000
#define SZ_PKE			0x1000

#define PA_I2C(x)		(0x83200000 + 0x100000*(x))
#define SZ_I2C			0x1000
#define PA_I2C0			PA_I2C(0)
#define PA_I2C1			PA_I2C(1)

#define PA_PWM			0x83500000
#define SZ_PWM			0x1000

#define PA_I2S0			0x84500400
#define SZ_I2S0			0xC00

#define PA_USB_PHY		0x86000000
#define SZ_USB_PHY		0x1000

#define PA_USB			0x86100000
#define SZ_USB			0x10000

#define PA_USB_EHCI		0x86400000
#define SZ_USB_EHCI		0x10000

#define PA_USB_OHCI0	0x86500000
#define SZ_USB_OHCI0	0x10000
#define PA_USB_OHCI1	0x86600000
#define SZ_USB_OHCI1	0x10000

#define PA_IOP0			0x86300000
#define SZ_IOP0			0x1000
#define PA_IOP1			0xBF300000
#define SZ_IOP1			0x1000

#define PA_VXD			0x85000000
#define SZ_VXD			0x100000

#define PA_SGX			0x85100000
#define SZ_SGX			0x1000

#define PA_VENC			0x88000000
#define SZ_VENC			0x1000

#define PA_JPEG			0x88200000
#define SZ_JPEG			0x1000

#define PA_ISP0			0x88300000
#define SZ_ISP0			0xD6000
#define PA_ISP1			0x88100000
#define SZ_ISP1			0x1000

#define PA_SCALER		0x89300000
#define SZ_SCALER		0x1000

#define PA_CLCD0		0x89000000
#define SZ_CLCD0		0x7000
#define PA_CLCD1		0x89200000
#define SZ_CLCD1		0x2000

#define PA_MIPI_DSIM	0x89500000
#define SZ_MIPI_DSIM	0x1000

#define PA_SWI			0xBF600000
#define SZ_SWI			0x1000

#define PA_RGBOUT0		0x89100000
#define SZ_RGBOUT0		0x7000
#define PA_RGBOUT1		0x89600000
#define SZ_RGBOUT1		0x1000

#define PA_TVOUT		0x89400000
#define SZ_TVOUT		0x1000

#define PA_AMC0			0x84100000
#define SZ_AMC0			0x3000
#define PA_AMC1			0x84000000
#define SZ_AMC1			0x40000
#define PA_AMC2			0x84300000
#define SZ_AMC2			0x5000

#define PA_DISPLAYPORT	0x84900000
#define SZ_DISPLAYPORT	0x2000

// S3C mappings

#define S3C_PA_USB_HSOTG	PA_USB
#define S3C_PA_USB_HSPHY	PA_USB_PHY
#define S3C_VA_USB_HSPHY	(S3C_ADDR_CPU(0x700000))

#define S3C_PA_IIC			PA_I2C0
#define S3C_PA_IIC1			PA_I2C1

#define S3C_PA_HSMMC0		PA_SDIO

#define S5L_PA_UART0		PA_UART0
#define S5L_PA_UART1		PA_UART1
#define S5L_PA_UART2		PA_UART2
#define S5L_PA_UART3		PA_UART3
#define S5L_PA_UART4		PA_UART4
#define S5L_PA_UART5		PA_UART5
#define S5L_SZ_UART			SZ_UART

#endif //_S5L_8930_MAP_
