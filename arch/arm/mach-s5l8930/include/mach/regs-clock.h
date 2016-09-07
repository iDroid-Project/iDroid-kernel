/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef  _S5L8930_REGS_CLOCK_
#define  _S5L8930_REGS_CLOCK_

#include <mach/map.h>

#define CLOCK_BASE_HZ		48000000

#define S5L_APLL_CON0		(VA_PMGR0 + 0x00)
#define S5L_APLL_CON1		(VA_PMGR0 + 0x04)
#define S5L_MPLL_CON0		(VA_PMGR0 + 0x08)
#define S5L_MPLL_CON1		(VA_PMGR0 + 0x0c)
#define S5L_EPLL_CON0		(VA_PMGR0 + 0x10)
#define S5L_EPLL_CON1		(VA_PMGR0 + 0x14)
#define S5L_VPLL_CON0		(VA_PMGR0 + 0x20)
#define S5L_VPLL_CON1		(VA_PMGR0 + 0x24)

#define S5L_CLOCK_CON0		(VA_PMGR0 + 0x30)
#define S5L_CLOCK_CON1		(VA_PMGR0 + 0x5030)

#define CLOCK_CON1_UNSTABLE	(1 << 16)

#define PLLCON0_ENABLE		(1 << 31)
#define PLLCON0_LOCKED		(1 << 29)
#define PLLCON0_UPDATE		(1 << 27)
#define PLLCON0_M_SHIFT		3
#define PLLCON0_M_MASK		0x3FF
#define PLLCON0_M(x)		(((x) >> PLLCON0_M_SHIFT) & PLLCON0_M_MASK)
#define PLLCON0_P_SHIFT		14
#define PLLCON0_P_MASK		0x3F
#define PLLCON0_P(x)		(((x) >> PLLCON0_P_SHIFT) & PLLCON0_P_MASK)
#define PLLCON0_S_SHIFT		0
#define PLLCON0_S_MASK		0x7
#define PLLCON0_S(x)		(((x) >> PLLCON0_S_SHIFT) & PLLCON0_S_MASK)

#define PLLCON1_FSEL_SHIFT	17
#define PLLCON1_FSEL_0		0x5
#define PLLCON1_FSEL_1		0xD
#define PLLCON1_FSEL_2		0x1C

#define PLLCON1_ENABLE		0x960

#define S5L_CLOCK_PERF		((uint32_t *__iomem)(VA_PMGR0 + 0x50c0))
#define S5L_NUM_PERF		3

#define S5L_CLOCK_CON		((uint32_t *__iomem)(VA_PMGR0 + 0x40))
#define S5L_NUM_CLOCKS		48

#define S5L_CLOCK_GATE		((uint32_t *__iomem)(VA_PMGR0 + 0x1010))
#define S5L_NUM_GATES		64

#define CLOCK_CON_SRC_SHIFT	28
#define CLOCK_CON_SRC_MASK	0x3
#define CLOCK_CON_SRC(x)	(((x) >> CLOCK_CON_SRC_SHIFT) & CLOCK_CON_SRC_MASK)
#define CLOCK_CON_DIV(x, y)	({ const clock_divider_t *div = &clock_dividers[(y)]; (((x) & div->mask) >> div->shift); })


#define S5P_WAKEUP_STAT		(VA_PMGR0 + 0xC200)
#define S5P_CLK_DIV0		(VA_PMGR0 + 0x300)

#endif //_S5L_8930_REGS_CLOCK_
