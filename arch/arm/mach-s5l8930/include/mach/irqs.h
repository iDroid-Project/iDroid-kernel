/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef  _S5L8930_IRQS_
#define  _S5L8930_IRQS_

#include <plat/irqs.h>

#define IRQ_TIMER0_VIC	300
#define IRQ_TIMER1_VIC	301
#define IRQ_TIMER2_VIC	302
#define IRQ_TIMER3_VIC	303
#define IRQ_TIMER4_VIC	304

#define IRQ_CDMA0	0x31
#define IRQ_CDMA36	0x55

#define IRQ_IIC		0x13
#define IRQ_IIC1	0x14
#define IRQ_IIC2	0x15

#define IRQ_OTG		0xD

#define IRQ_GPIO	0x74

#define IRQ_UART0		S5L_IRQ_VIC0(22)
#define IRQ_UART1		S5L_IRQ_VIC0(23)
#define IRQ_UART2		S5L_IRQ_VIC0(24)
#define IRQ_UART3		S5L_IRQ_VIC0(25)

#define IRQ_HSMMC0	0x26

#define IRQ_MIPI_DSIM	0x28
#define IRQ_CLCD0		0x29
#define IRQ_CLCD1		0x30
#define IRQ_RGBOUT0		0x31
#define IRQ_RGBOUT1		0x32
#define IRQ_3D			S5L_IRQ_VIC2(10)

#define IRQ_FMI0 0x22
#define IRQ_FMI1 0x23

#define IRQ_SPI0 0x1D
#define IRQ_SPI1 0x1E

#define S5L_SUBIRQ_START	(32*4) + 176
#define S5L_SUBIRQ(x)		(S5L_SUBIRQ_START + (x))

#define NR_IRQS				S5L_SUBIRQ_START + 100 // 100 'spare' IRQs for software demuxing

#define IRQ_EINT(x)			((32*4) + (x))
#define EINT_OFFSET(irq)	((irq)/32)
#define IRQ_EINT_BIT(x)		((x)&0x1f)

#endif //_S5L8930_IRQS_
