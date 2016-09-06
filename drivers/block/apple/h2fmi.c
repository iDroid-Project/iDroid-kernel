/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mtd/nand.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/apple_flash.h>
#include <plat/h2fmi.h>
#include <plat/cdma.h>
#include <mach/clock.h>
#include <mach/regs-h2fmi.h>
#include <crypto/hash.h>

static uint32_t h2fmi_ftl_count = 0;
static uint32_t h2fmi_ftl_databuf = 0;
static uint32_t h2fmi_ftl_smth[2] = { 0, 0 };

#define H2FMI_MAX_CHIPS			8

struct h2fmi_timing_setup {
	uint64_t freq;
	uint32_t c;
	uint32_t d;
	uint32_t e;
	uint32_t f;
	uint32_t g;
	uint32_t h;
	uint32_t i;
	uint32_t j;
	uint32_t k;
	uint32_t l;
	uint32_t m;
	uint32_t n;
	uint32_t o;
	uint32_t p;
	uint32_t q;
	uint32_t r;
};

enum h2fmi_status {
	H2FMI_IDLE = 0, H2FMI_READ, H2FMI_WRITE, H2FMI_ERASE
};

enum h2fmi_read_state {
	H2FMI_READ_BEGIN = 0,
	H2FMI_READ_1,
	H2FMI_READ_2,
	H2FMI_READ_3,
	H2FMI_READ_4,
	H2FMI_READ_COMPLETE
};

enum h2fmi_write_state {
	H2FMI_WRITE_BEGIN = 0,
	H2FMI_WRITE_1,
	H2FMI_WRITE_2,
	H2FMI_WRITE_3,
	H2FMI_WRITE_4,
	H2FMI_WRITE_5,
	H2FMI_WRITE_COMPLETE
};

enum h2fmi_write_mode {
	H2FMI_WRITE_NORMAL = 0,
	H2FMI_WRITE_MODE_1,
	H2FMI_WRITE_MODE_2,
	H2FMI_WRITE_MODE_3,
	H2FMI_WRITE_MODE_4,
	H2FMI_WRITE_MODE_5,
};

struct h2fmi_transaction {
	size_t count;

	u16 *chips;
	u32 *pages;

	struct scatterlist *sg_data, *sg_oob;
	int sg_num_data, sg_num_oob;

	u8 *eccres;
	u8 *eccbuf;

	size_t num_failed;
	size_t num_ecc;
	size_t num_empty;

	size_t curr;
	int result;

	unsigned chip_mask;

	unsigned busy :1;
	unsigned new_chip :1;

	enum h2fmi_write_mode write_mode;
};

static struct h2fmi_chip_info h2fmi_chip_info[] = {
/* A4 */
// Micron
		{ 0x4604682c, 4096, 256, 4096, 224, 12, 0, 7, 1, 0, { 25, 12, 10, 25,
				12, 10, 20, 15 } }, { 0x4b04882c, 4096, 256, 8192, 448, 24, 0,
				7, 1, 0, { 20, 10, 7, 20, 10, 7, 16, 15 } }, { 0xc605882c, 8192,
				256, 4096, 224, 224, 0, 7, 2, 0,
				{ 20, 10, 7, 20, 10, 7, 16, 15 } }, { 0xcb05a82c, 8192, 256,
				8192, 448, 24, 0, 7, 2, 0, { 20, 10, 7, 20, 10, 7, 16, 15 } },

		// SanDisk
		{ 0x82942d45, 4096, 256, 8192, 640, 8, 0, 9, 1, 0, { 25, 12, 10, 25, 12,
				10, 20, 25 } }, { 0x32944845, 4096, 128, 8192, 448, 8, 0, 9, 1,
				0, { 25, 12, 10, 25, 12, 10, 20, 25 } }, { 0x82944d45, 4096,
				256, 8192, 640, 8, 0, 9, 1, 0,
				{ 25, 12, 10, 25, 12, 10, 20, 25 } }, { 0x32956845, 8192, 128,
				8192, 448, 8, 0, 9, 2, 0, { 25, 12, 10, 25, 12, 10, 20, 25 } }, //mychip
		{ 0x3295de45, 8192, 128, 8192, 376, 8, 0, 9, 2, 0, { 25, 12, 10, 25, 12,
				10, 20, 30 } },

		// Toshiba
		{ 0x3294d798, 4148, 128, 8192, 376, 8, 0, 1, 1, 0, { 25, 12, 10, 25, 12,
				10, 20, 25 } }, { 0x3295de98, 8296, 128, 8192, 376, 8, 0, 1, 2,
				0, { 25, 12, 10, 25, 12, 10, 20, 25 } }, { 0x3294e798, 4100,
				128, 8192, 448, 16, 0, 1, 1, 0,
				{ 25, 12, 10, 25, 12, 10, 20, 25 } }, { 0x3295ee98, 8200, 128,
				8192, 448, 24, 0, 1, 2, 0, { 25, 12, 10, 25, 12, 10, 20, 25 } },

		// Hynix
		{ 0xb614d5ad, 4096, 128, 4096, 128, 4, 0, 3, 1, 0, { 25, 12, 10, 25, 12,
				10, 20, 15 } }, { 0x2594d7ad, 8192, 128, 4096, 224, 12, 0, 3, 1,
				0, { 25, 12, 10, 25, 12, 10, 20, 15 } }, { 0x9a94d7ad, 2048,
				256, 8192, 448, 24, 0, 8, 1, 0,
				{ 25, 12, 10, 25, 12, 10, 20, 15 } }, { 0x9a95dead, 4096, 256,
				8192, 448, 24, 0, 8, 2, 0, { 25, 12, 10, 25, 12, 10, 20, 15 } },

		// Samsung
		{ 0x7294d7ec, 4152, 128, 8192, 436, 12, 0, 8, 1, 0, { 30, 15, 10, 30,
				15, 10, 25, 15 } }, { 0x7a94d7ec, 4152, 128, 8192, 640, 16, 0,
				8, 1, 0, { 25, 12, 10, 25, 12, 10, 20, 15 } }, { 0x29d5d7ec,
				8192, 128, 4096, 218, 8, 0, 2, 2, 0, { 30, 15, 10, 30, 15, 10,
						20, 15 } }, { 0x72d5deec, 8304, 128, 8192, 436, 12, 0,
				8, 2, 0, { 30, 15, 10, 30, 15, 10, 25, 15 } }, { 0x7ad5deec,
				8304, 128, 8192, 640, 16, 0, 8, 2, 0, { 25, 12, 10, 25, 12, 10,
						20, 15 } }, };

static uint32_t h2fmi_hash_table[256];

struct h2fmi_geometry {
	uint16_t num_ce;
	uint16_t blocks_per_ce;
	uint32_t pages_per_ce;
	uint16_t pages_per_block;
	uint32_t block_address_space;
	uint16_t bytes_per_page;
	uint16_t ecc_steps;
	uint16_t bytes_per_spare;
	uint16_t banks_per_ce_vfl;
	uint16_t banks_per_ce;
	uint16_t blocks_per_bank;
	uint16_t bank_address_space;
	uint16_t total_block_space;

	int chip_shift, page_shift;
	int pagemask, pageoffmask;

	uint64_t chip_size;

	u32 oob_size, oob_alloc_size, oobsize;
};

struct h2fmi_state {
	struct platform_device *dev;
	struct h2fmi_platform_data *pdata;

	struct apple_nand nand;

	struct h2fmi_chip_info *chip_info;

	struct nand_ecclayout ecc;
	struct h2fmi_geometry geo;
	struct cdma_aes aes;

	enum h2fmi_status state;
	enum h2fmi_read_state read_state;
	enum h2fmi_write_state write_state;
	struct h2fmi_transaction transaction;

	spinlock_t lock;

	void *__iomem base_regs;
	void *__iomem flash_regs;
	void *__iomem ecc_regs;

	dma_addr_t base_dma, flash_dma, ecc_dma;

	struct clk *clk, *clk_bch;

	int irq;

	uint32_t read_buf;
	int read_buf_left;

	int num_chips;
	unsigned int bitmap;
	int chip_map[H2FMI_MAX_CHIPS];
	u8 *bbt[H2FMI_MAX_CHIPS];

	uint32_t timing;

	int ecc_step_shift;

	unsigned whitening_disabled :1;

	u32 page_fmt;
	u32 ecc_fmt;
	uint8_t ecc_bits;
	uint8_t ecc_tag;
};

static struct h2fmi_chip_info *h2fmi_find_chip_info(uint8_t _id[8]) {
	int i;
	for (i = 0; i < ARRAY_SIZE(h2fmi_chip_info); i++) {
		if (memcmp(_id, &h2fmi_chip_info[i].id, sizeof(h2fmi_chip_info[i].id))
				== 0)
			return &h2fmi_chip_info[i];
	}

	return NULL;
}

static inline void h2fmi_set_timing_mode(struct h2fmi_state *_state, int _en) {
	u32 val = readl(_state->flash_regs + H2FMI_TIMING) & ~(1 << 21);

	if (_en)
		val |= (1 << 21);

	writel(val, _state->flash_regs + H2FMI_TIMING);
}

static inline void h2fmi_reset_timing(struct h2fmi_state *_state) {
	writel(_state->timing, _state->flash_regs + H2FMI_TIMING);
}

static void h2fmi_clear_interrupt(struct h2fmi_state *_state) {
	writel(0, _state->flash_regs + H2FMI_UNK440);
	writel(0, _state->base_regs + H2FMI_CREQ);
	writel(0x31FFFF, _state->flash_regs + H2FMI_NSTS);
	writel(0xF, _state->base_regs + H2FMI_CSTS);
}

static void h2fmi_reset(struct h2fmi_state *_state) {
	s5l_clock_gate_reset(_state->clk);

	writel(6, _state->base_regs + H2FMI_CCMD);
	writel(1, _state->flash_regs + H2FMI_RESET);
	writel(_state->timing, _state->flash_regs + H2FMI_TIMING);

	_state->read_buf_left = 0;
}

static void h2fmi_init(struct h2fmi_state *_state) {
	_state->timing = 0xFFFFF;
	writel(_state->timing, _state->flash_regs + H2FMI_TIMING);

	h2fmi_reset(_state);
}

static inline void h2fmi_enable_chip(struct h2fmi_state *_state, int _chip) {
	//printk("h2fmi: ENABLE: %x , CHIP :%x\n",readl(_state->flash_regs + H2FMI_CHIP_MASK) | (1 << _chip),_chip);
	writel(
			readl(_state->flash_regs + H2FMI_CHIP_MASK) |
			(1 << _chip),
			_state->flash_regs + H2FMI_CHIP_MASK);
}

static inline void h2fmi_disable_chip(struct h2fmi_state *_state, int _chip) {

	writel(readl(_state->flash_regs + H2FMI_CHIP_MASK) &~ (1 << _chip),
			_state->flash_regs + H2FMI_CHIP_MASK);
}

static inline void h2fmi_disable_bus(struct h2fmi_state *_state) {
	writel(0, _state->flash_regs + H2FMI_CHIP_MASK);
}

static int h2fmi_wait(struct h2fmi_state *_state, void *__iomem _reg,
		uint32_t _mask, uint32_t _val) {
	int i;

	for (i = 0; i < 10 && ((readl(_reg) & _mask) != _val); i++)
		msleep(10);
	//FIXME:Make it faster like OIB
	//while(!(readl(_reg) & _mask));
	//!= _val)){

	//}

	if (i == 10)
		return -ETIMEDOUT;

	writel(_val, _reg);
	return 0;
}

static int h2fmi_reset_this(struct h2fmi_state *_state) {
	writel(NAND_CMD_RESET, _state->flash_regs + H2FMI_NCMD);
	writel(1, _state->flash_regs + H2FMI_NREQ);

	return h2fmi_wait(_state, _state->flash_regs + H2FMI_NSTS, 1, 1);
}

static int h2fmi_reset_chip(struct h2fmi_state *_state, int _chip) {
	int ret;
	h2fmi_enable_chip(_state, _chip);
	ret = h2fmi_reset_this(_state);
	h2fmi_disable_chip(_state, _chip);
	return ret;
}

static int h2fmi_reset_all(struct h2fmi_state *_state) {
	int ret;
	int i;

	for (i = 0; i < H2FMI_MAX_CHIPS; i++) {
		ret = h2fmi_reset_chip(_state, i);
		if (ret)
			return ret;
	}

	msleep(50);
	return ret;
}

static inline int h2fmi_wait_req(struct h2fmi_state *_state, u32 _bits) {
	writel(_bits, _state->flash_regs + H2FMI_NREQ);
	return h2fmi_wait(_state, _state->flash_regs + H2FMI_NSTS, _bits, _bits);
}

static inline void h2fmi_wait_req_clear(struct h2fmi_state *_state) {
	writel(0, _state->flash_regs + H2FMI_NREQ);
}

static inline int h2fmi_send_cmd(struct h2fmi_state *_state, u32 _cmd,
		u32 _mask) {
	writel(_cmd, _state->flash_regs + H2FMI_NCMD);
	if (!_mask)
		return 0;

	return h2fmi_wait_req(_state, _mask);
}

static void h2fmi_set_address(struct h2fmi_state *_state, uint32_t _addr) {
	writel((_addr >> 16) & 0xFF, _state->flash_regs + H2FMI_ADDR1);
	writel(((_addr & 0xFF) << 16) | ((_addr >> 8) << 24),
			_state->flash_regs + H2FMI_ADDR0);
	writel(4, _state->flash_regs + H2FMI_ADDRMODE);

}

static void h2fmi_set_block_address(struct h2fmi_state *_state, uint32_t _addr) {
	writel(_addr & 0xFFFFFF, _state->flash_regs + H2FMI_ADDR0);
	writel(2, _state->flash_regs + H2FMI_ADDRMODE);
}

static int h2fmi_readid(struct h2fmi_state *_state, int _id) {
	int ret;
	writel(NAND_CMD_READID, _state->flash_regs + H2FMI_NCMD);
	writel(_id, _state->flash_regs + H2FMI_ADDR0);
	writel(0, _state->flash_regs + H2FMI_ADDRMODE);
	writel(9, _state->flash_regs + H2FMI_NREQ);

	ret = h2fmi_wait(_state, _state->flash_regs + H2FMI_NSTS, 9, 9);
	if (ret)
		return ret;

	writel(0x801, _state->base_regs + H2FMI_PAGEFMT);
	writel(3, _state->base_regs + H2FMI_CCMD);
	ret = h2fmi_wait(_state, _state->base_regs + H2FMI_CSTS, 2, 2);
	return ret;
}

static void h2fmi_read(struct h2fmi_state *_state, void *_buff, size_t _sz) {
	uint32_t *u32b;
	uint8_t *u8b;
	int u32amt;
	int i;
	int left = _sz;

	if (_state->read_buf_left) {
		uint32_t amt = min(_sz, (size_t )_state->read_buf_left);
		memcpy(_buff, &_state->read_buf, amt);
		_state->read_buf >>= (8 * amt);

		_state->read_buf_left -= amt;
		_buff += amt;
		left -= amt;
	}

	if (!left)
		return;

	u32b = _buff;
	u32amt = left >> 2;
	left -= u32amt * sizeof(u32);

	for (i = 0; i < u32amt; i++) {
		u32b[i] = readl(_state->base_regs + H2FMI_DATA0);
		//printk("%x \n",u32b[i]);
	}
	if (!left)
		return;

	u8b = _buff + (sizeof(u32) * u32amt);
	_state->read_buf = readl(_state->base_regs + H2FMI_DATA0);
	_state->read_buf_left = 4 - left;
	memcpy(u8b, &_state->read_buf, left);
	_state->read_buf >>= left * 8;

	return;
}

static void h2fmi_write(struct h2fmi_state *_state, const void *_buffer,
		size_t _sz) {
	const u8 *u8b;
	const uint32_t *u32b = _buffer;
	int u32amt = _sz >> 2;
	int i;
	u32 left;

	_sz -= (u32amt * sizeof(u32));

	for (i = 0; i < u32amt; i++)
		writel(u32b, _state->base_regs + H2FMI_DATA0);

	if (!_sz)
		return;

	u8b = _buffer + (sizeof(u32) * u32amt);
	left = 0;
	for (i = 0; i < _sz; i++)
		left |= (u8b[i] << (i * 8));
	writel(left, _state->base_regs + H2FMI_DATA0);
}

static inline void h2fmi_clear_ecc_sts(struct h2fmi_state *_state) {
	writel(0x1a8, _state->ecc_regs + H2FMI_ECCSTS);
}

static inline void h2fmi_clear_ecc_buf(struct h2fmi_state *_state) {
	writel(1, _state->ecc_regs + H2FMI_ECCBUF);
	h2fmi_clear_ecc_sts(_state);
}

static void h2fmi_set_format(struct h2fmi_state *_state, u32 _bits) {
	writel(_state->ecc_fmt, _state->base_regs + H2FMI_ECCFMT);
	writel(_state->page_fmt, _state->base_regs + H2FMI_PAGEFMT);
	writel(((_bits & 0x1f) << 8) | 0x20000, _state->ecc_regs + H2FMI_ECCCFG);
}

static int h2fmi_check_ecc(struct h2fmi_state *_state) {
	int num_empty = 0;
	int num_failed = 0;
	int len = _state->geo.ecc_steps;

	int i;
	u8 *eptr = _state->transaction.eccbuf
			+ (_state->transaction.curr * _state->geo.ecc_steps);

	int eccsts = readl(_state->ecc_regs + H2FMI_ECCSTS);
	writel(eccsts, _state->ecc_regs + H2FMI_ECCSTS);

	//dev_info(&_state->dev->dev, "ecc-sts: 0x%08x.\n", eccsts);

	if (_state->transaction.eccres)
		_state->transaction.eccres[_state->transaction.curr - 1] =
				(eccsts >> 16) & 0x1f;

	for (i = 0; i < len; i++) {
		u32 buf = readl(_state->ecc_regs + H2FMI_ECCBUF);
		u8 val;

		if (buf & 2) {
			val = 0xFE;
			num_empty++;
		} else if (buf & 4) {
			val = 0xFF;
			num_failed++;
		} else if (buf & 1)
			val = 0xFF;
		else
			val = (buf >> 16) & 0x1F;

		if (_state->transaction.eccbuf)
			eptr[i] = val;
	}

	writel(readl(_state->base_regs + H2FMI_CCMD) &~ (1 << 7),
			_state->base_regs + H2FMI_CCMD);

	if (num_empty == len) {
		_state->transaction.num_empty++;
		return 1;
	}

	if (num_failed == len) {
		_state->transaction.num_failed++;
		return -EIO;
	}

	if (eccsts & 8) {
		_state->transaction.num_ecc++;
		return -EUCLEAN;
	}

	return 0;
}

static int h2fmi_prepare(struct h2fmi_state *_state, u8 _a, u8 _b) {
	int ret;

	writel(readl(_state->flash_regs + H2FMI_TIMING) &~ 0x100000,
			_state->flash_regs + H2FMI_TIMING);
	writel(_a | (_b << 8), _state->flash_regs + H2FMI_UNK44C);

	ret = h2fmi_send_cmd(_state, NAND_CMD_STATUS, 1);
	if (ret)
		return ret;

	h2fmi_clear_interrupt(_state);

	writel(0, _state->flash_regs + H2FMI_UNKREG8);
	writel(0x50, _state->flash_regs + H2FMI_NREQ);

	return 0;
}

static int h2fmi_setup_transfer(struct h2fmi_state *_state) {

	if (_state->state != H2FMI_READ) {
		h2fmi_set_format(_state, _state->ecc_bits + 1);
		h2fmi_clear_ecc_sts(_state);
	} else
		h2fmi_set_format(_state, 0xF);

	return 0;
}

static int h2fmi_prepare_transfer(struct h2fmi_state *_state) {
	int ret = h2fmi_prepare(_state, 0x40, 0x40);
	if (ret)
		return ret;

	writel(0x20, _state->flash_regs + H2FMI_UNK440);
	writel(0x100, _state->base_regs + H2FMI_CREQ);
	return 0;
}

static int h2fmi_transfer_ready(struct h2fmi_state *_state) {
	if (readl(_state->base_regs + H2FMI_CSTS) & 0x100)
		return 1;
	else {
		//if(timer_get_system_microtime() - _fmi->last_action_time > _fmi->time_interval)
		//	return -ETIMEDOUT;

		return 0;
	}
}

static irqreturn_t h2fmi_irq_handler(int _irq, void *_dev) {
	struct h2fmi_state *state = _dev;
	dev_info(&state->dev->dev, "irq!\n");
	return IRQ_HANDLED;
}

static u8 h2fmi_calculate_ecc_bits(struct h2fmi_state *_state) {
	uint32_t ecc_block_size = (_state->geo.bytes_per_spare
			- _state->geo.oob_size)
			/ (_state->geo.bytes_per_page >> _state->ecc_step_shift);
	static uint8_t some_array[] = { 0x35, 0x1E, 0x33, 0x1D, 0x2C, 0x19, 0x1C,
			0x10, 0x1B, 0xF };

	int i;
	for (i = 0; i < sizeof(some_array) / sizeof(uint8_t); i += 2) {
		if (ecc_block_size >= some_array[i])
			return some_array[i + 1];
	}

	dev_err(&_state->dev->dev,
			"calculating ecc bits failed (0x%08x, 0x%08x, 0x%08x) -> 0x%08x.\r\n",
			_state->geo.bytes_per_spare, _state->geo.oob_size,
			_state->geo.bytes_per_page, ecc_block_size);
	return 0;
}

static inline u32 h2fmi_round_down(u32 _a, u32 _b) {
	u32 ret = (_b + _a - 1) / _a;

	if (ret == 0)
		return 0;

	return ret - 1;
}

static int h2fmi_setup_timing(struct h2fmi_timing_setup *_timing, u8 *_buffer) {
	u32 some_time, lr, r4, r3;

	int64_t smth = div64_s64(
			div64_s64(1000000000L, div64_s64((int64_t) _timing->freq, 1000)),
			1000);
	uint8_t r1 = (uint8_t) (_timing->q + _timing->g);

	uint32_t var_28 = h2fmi_round_down(smth, _timing->q + _timing->g);

	uint32_t var_2C;
	if (_timing->p <= var_28 * smth)
		var_2C = 0;
	else
		var_2C = _timing->p - (var_28 * smth);

	_buffer[0] = h2fmi_round_down(smth, _timing->k + _timing->g);

	some_time = (_buffer[0] + 1) * smth;

	r3 = _timing->m + _timing->h + _timing->g;
	r1 = max(_timing->j, r3);

	lr = (some_time < r1) ? r1 - some_time : 0;
	r4 = (some_time < r3) ? r1 - some_time : 0;

	_buffer[1] = h2fmi_round_down(smth, max(_timing->l + _timing->f, lr));
	_buffer[2] = div64_s64((div64_s64((smth + r4 - 1), smth) * smth), smth);
	_buffer[3] = var_28;
	_buffer[4] = h2fmi_round_down(smth, max(_timing->f + _timing->r, var_2C));

	return 0;
}

static void h2fmi_aes_gen_iv(void *_param, u32 _segment, u32 *_iv) {
	struct h2fmi_state *state = _param;
	u32 val = state->transaction.pages[state->transaction.curr];
	int i;

	for (i = 0; i < 4; i++) {
		if (val & 1)
			val = (val >> 1) ^ 0x80000061;
		else
			val >>= 1;

		_iv[i] = val;
	}
}

static u32 h2fmi_aes_key[] = { 0xAB42A792, 0xBF69C908, 0x12946C00, 0xA579CCD3, };
static uint32_t bpp = 0;
static void h2fmi_aes_handler_1(void *_param, u32 _segment, u32 *_iv) {
	//bufferPrintf("fmi: aes_handler_1.\r\n");
	//struct h2fmi_state *state = _param;
	int param = _param;
	int val = ((param - h2fmi_ftl_databuf) / bpp) + h2fmi_ftl_smth[0];
	int i;
	for (i = 0; i < 4; i++) {
		if (val & 1)
			val = (val >> 1) ^ 0x80000061;
		else
			val = (val >> 1);

		_iv[i] = val;
	}
}

static u32 h2fmi_aes_key_1[] = { 0x95AE5DF6, 0x426C900E, 0x58CC54B2, 0xCEEE78FC, };

uint8_t DKey[32];
uint8_t EMF[32];
int h2fmi_emf = 0;
int h2fmi_emf_iv_input = 0;
int h2fmi_emf_iv_offset = 0;
int h2fmi_emf_sha = 0;
static int* h2fmi_key = (int*) EMF;
static int h2fmi_keylength = CDMA_AES_256;

void h2fmi_set_emf(uint32_t enable, uint32_t iv_input) {
	h2fmi_emf = enable;
	if (iv_input)
		h2fmi_emf_iv_input = iv_input;
}

uint32_t h2fmi_get_emf() {
	return h2fmi_emf;
}

static void null_iv(uint32_t _param, uint32_t _segment, uint32_t* _iv) {
	_iv=NULL;
}

static void h2fmi_aes_handler_emf(uint32_t _param, uint32_t _segment,
		uint32_t* _iv) {
	uint32_t val = h2fmi_emf_iv_input;
	if (h2fmi_emf_sha)
		val = h2fmi_emf_iv_offset;

	uint32_t i;
	for (i = 0; i < 4; i++) {
		if (val & 1)
			val = (val >> 1) ^ 0x80000061;
		else
			val = (val >> 1);

		_iv[i] = val;
	}

	 if(h2fmi_emf_sha)
		//panic("h2fmi_emf_sha not implemented yet.\n");
	 {

	 struct shash_alg *sha1;
	 struct shash_desc *context;
	 u8 sha1_hash[20];
	 sha1->init(&context);
	 sha1->update(&context, (unsigned char*)h2fmi_key, 32);
	 sha1->final(&context,sha1_hash);

	 struct cdma_aes *_aes;
	 _aes->data_size = 16;
	 _aes->key = sha1_hash;
	 _aes->decrypt = 0;
	 _aes->type = 0;
	 _aes->iv_param = NULL;
	 _aes->gen_iv = null_iv;
	 uint8_t* buff = NULL;

	 memcpy(buff, _iv, 16);
	 aes_crypto_cmd(0x10, buff, buff, 16, 0 << 28, (void*)sha1_hash, (void*)NULL);
	 memcpy(_iv, buff, 16);

	// aes_hw_crypto_cmd(0x10,buff,buff,);
	 //_iv, 16, AESCustom, sha1_hash, CDMA_AES_128, NULL); //FIXME by CDMA AES -erfanoabdi
	 h2fmi_emf_iv_offset += 0x1000;
	 }
}

void h2fmi_set_key(uint32_t enable, void* key, int keyLen, uint32_t sha,
		uint32_t offset) {
	if (enable) {
		h2fmi_key = (uint32_t*) key;
		h2fmi_keylength = keyLen;
		h2fmi_emf_sha = sha;
		h2fmi_emf_iv_offset = offset;
	} else {
		h2fmi_key = (uint32_t*) EMF;
		h2fmi_keylength = CDMA_AES_256;
		h2fmi_emf_sha = 0;
		h2fmi_emf_iv_offset = 0;
	}
}

static int h2fmi_default_aes(struct h2fmi_state *_state, struct cdma_aes *_aes,
		int _decrypt) {
	_aes->data_size = _state->geo.bytes_per_page;
	_aes->iv_param = (void*) _state;
	_aes->gen_iv = h2fmi_aes_gen_iv;
	_aes->key = h2fmi_aes_key;
	_aes->decrypt = _decrypt;
	_aes->type = CDMA_AES_128;
	return 0;
}

static void h2fmi_setup_aes(struct h2fmi_state *_state, int _enabled,
		int _encrypt, int _offset) {
	struct h2fmi_state *state = container_of(_state, struct h2fmi_state, nand);
	if (_enabled) {
		if (h2fmi_ftl_databuf <= _offset
				&& _offset
						< (state->geo.bytes_per_page * h2fmi_ftl_count)
								+ h2fmi_ftl_databuf) {
			// FTL
			state->aes.data_size = state->geo.bytes_per_page;
			state->aes.iv_param = _offset;
			bpp = state->geo.bytes_per_page;
			state->aes.gen_iv = h2fmi_aes_handler_1;
			state->aes.key = h2fmi_aes_key_1;
			state->aes.decrypt = !_encrypt;
			state->aes.type = CDMA_AES_128; // AES-128
			if (h2fmi_emf) {
				state->aes.data_size = 0x1000;
				state->aes.key = h2fmi_key;
				state->aes.gen_iv = h2fmi_aes_handler_emf;
				switch (h2fmi_keylength) {
				case CDMA_AES_128:
					state->aes.type = 0 << 28;
					break;
				case CDMA_AES_192:
					state->aes.type = 1 << 28;
					break;
				case CDMA_AES_256:
					state->aes.type = 2 << 28;
					break;
				default:
					break;
				}
			}
		} else {
			h2fmi_default_aes(state, &state->aes, !_encrypt);
		}
		cdma_aes(state->pdata->dma0, &state->aes);
	} else
		cdma_aes(state->pdata->dma0, NULL);
}

static int h2fmi_is_block_bad(struct h2fmi_state *_state, int _ce, int _block) {
	int bb = _block >> 3;
	int bi = _block & 0x7;

	if (!_state->bbt[_ce])
		return -ENOENT;

	return (_state->bbt[_ce][bb] & (1 << bi)) ? 1 : 0;
}

static void h2fmi_mark_block_bad(struct h2fmi_state *_state, int _ce,
		int _block) {
	int bb = _block >> 3;
	int bi = _block & 0x7;

	if (_state->bbt[_ce])
		_state->bbt[_ce][bb] |= (1 << bi);
}

static int h2fmi_rw_large_page(struct h2fmi_state *_state) {
	int ret;

	cdma_dir_t dir = (_state->state == H2FMI_READ) ? 2 : 1;

	ret = cdma_begin(_state->pdata->dma0, dir, _state->transaction.sg_data,
			_state->transaction.sg_num_data,
			_state->geo.bytes_per_page * _state->transaction.count,
			_state->base_dma + H2FMI_DATA0, 4, 8, _state->pdata->pid0);
	if (ret)
		return ret;

	ret = cdma_begin(_state->pdata->dma1, dir, _state->transaction.sg_oob,
			_state->transaction.sg_num_oob,
			_state->geo.oob_size * _state->transaction.count,
			_state->base_dma + H2FMI_DATA1, 1, 1, _state->pdata->pid1);
	if (ret) {
		cdma_cancel(_state->pdata->dma0);
		return ret;
	}

	return 0;
}

static int h2fmi_read_complete(struct h2fmi_state *_state) {
	//printk(KERN_INFO "h2fmi_read_complete\n");

	h2fmi_clear_interrupt(_state);
	h2fmi_disable_bus(_state);
	//printk(KERN_INFO "h2fmi_read_complete_done\n");

	return 0;
}

static int h2fmi_read_state_2(struct h2fmi_state *_state) {
	//printk(KERN_INFO "h2fmi_read_state_2\n");

	int val = h2fmi_transfer_ready(_state);
	if (val < 0) {
		writel(0, _state->flash_regs + H2FMI_NREQ);
		h2fmi_disable_bus(_state);
		_state->transaction.result = -ETIMEDOUT;
		_state->read_state = H2FMI_READ_COMPLETE;
		return h2fmi_read_complete(_state);
	} else if (val) {
		h2fmi_clear_interrupt(_state);
		h2fmi_send_cmd(_state, NAND_CMD_READ0, 1);
		writel(2, _state->base_regs + H2FMI_CREQ);

		_state->read_state = H2FMI_READ_3;
		if (_state->transaction.curr == 0) {
			writel(3, _state->base_regs + H2FMI_CCMD);
			h2fmi_rw_large_page(_state);
			//_fmi->last_action_time = timer_get_system_microtime();
		} else {
			writel(0x82, _state->base_regs + H2FMI_CCMD);
			writel(0x3, _state->base_regs + H2FMI_CCMD);
			//_fmi->last_action_time = timer_get_system_microtime();
			h2fmi_check_ecc(_state);
		}
	}

	return 0;
}

static int h2fmi_read_state_4(struct h2fmi_state *_state) {
	//printk(KERN_INFO "h2fmi_read_state_4\n");

	if (readl(_state->base_regs + H2FMI_UNK8) & 4) {
		/*if(timer_get_system_microtime() - _fmi->last_action_time > _fmi->stage_time_interval)
		 {
		 _fmi->current_status = 0;
		 _fmi->failure_details.overall_status = 0x8000001F;
		 }
		 else*/
		return 0;
	} else {
		if (_state->transaction.curr < _state->transaction.count) {
			h2fmi_prepare_transfer(_state);
			//_fmi->last_action_time = timer_get_system_microtime();
			_state->read_state = H2FMI_READ_2;
			return h2fmi_read_state_2(_state);
		} else
			h2fmi_check_ecc(_state);
	}

	_state->read_state = H2FMI_READ_COMPLETE;
	return h2fmi_read_complete(_state);
}

static int h2fmi_read_state_1(struct h2fmi_state *_state) {
	//printk(KERN_INFO "h2fmi_read_state_1\n");

	int reset_chip = 0;
	if (_state->transaction.new_chip) {
		_state->transaction.new_chip = 0;
		//printk(KERN_INFO "h2fmi_read_state_1_1:%x\n",	_state->transaction.curr);
		h2fmi_enable_chip(_state,
				_state->transaction.chips[_state->transaction.curr]);
		h2fmi_set_address(_state,
				_state->transaction.pages[_state->transaction.curr]);
		h2fmi_send_cmd(_state, NAND_CMD_READSTART << 8, 0xb);
	} else
		reset_chip =
				(_state->transaction.curr >= _state->transaction.count) ? 0 : 1;

	if (_state->transaction.curr + 1 < _state->transaction.count) {
		if (_state->transaction.chips[_state->transaction.curr + 1]
				== _state->transaction.chips[_state->transaction.curr]) {
			_state->transaction.new_chip = 1;
		} else {
			// It's a different chip, so we can set it up now,
			// and then next stage_1, we won't have to do it!
			_state->transaction.new_chip = 0;
			reset_chip = 1;
			printk(KERN_INFO "h2fmi_read_state_1_2\n");

			h2fmi_enable_chip(_state,
					_state->transaction.chips[_state->transaction.curr + 1]);
			h2fmi_set_address(_state,
					_state->transaction.pages[_state->transaction.curr + 1]);
			h2fmi_send_cmd(_state, NAND_CMD_READSTART << 8, 0xb);
		}
	}

	if (reset_chip)
		h2fmi_enable_chip(_state,
				_state->transaction.chips[_state->transaction.curr]);

	writel(0x100000, _state->base_regs + H2FMI_CREQ);
	_state->read_state = H2FMI_READ_4;
	//_fmi->last_action_time = timer_get_system_microtime();
	return h2fmi_read_state_4(_state);
}

static int h2fmi_read_state_3(struct h2fmi_state *_state) {
	//printk(KERN_INFO "h2fmi_read_state_3\n");

	if ((readl(_state->base_regs + H2FMI_CSTS) & 2) == 0) {
		/*if(timer_get_system_microtime() - _fmi->last_action_time > _fmi->time_interval)
		 {
		 _fmi->current_status = 0;
		 _fmi->failure_details.overall_status = ETIMEDOUT;
		 _fmi->state.read_state = H2FMI_READ_DONE;
		 return h2fmi_read_complete_handler(_fmi);
		 }*/
	} else {
		writel(0, _state->base_regs + H2FMI_CREQ);
		_state->transaction.curr++;
		_state->read_state = H2FMI_READ_1;
		return h2fmi_read_state_1(_state);
	}

	return 0;
}

static int h2fmi_read_begin(struct h2fmi_state *_state) {
	_state->transaction.curr = 0;
	_state->transaction.new_chip = 1;
	_state->transaction.busy = 1;

	h2fmi_setup_transfer(_state);

	_state->read_state = H2FMI_READ_1;
	//printk(KERN_INFO "h2fmi_read_begin\n");
	return h2fmi_read_state_1(_state);
}

static int h2fmi_read_state_machine(struct h2fmi_state *_state) {
	static int (*fns[])(struct h2fmi_state*) = {
		h2fmi_read_begin,
		h2fmi_read_state_1,
		h2fmi_read_state_2,
		h2fmi_read_state_3,
		h2fmi_read_state_4,
		h2fmi_read_complete,
	};

	if (_state->state != H2FMI_READ) {
		dev_err(&_state->dev->dev,
				"read_state_machine called whilst not reading!\n");
		return -EINVAL;
	}

	if (_state->read_state > ARRAY_SIZE(fns)) {
		dev_err(&_state->dev->dev, "invalid read state %d.\n",
				_state->read_state);
		return -EINVAL;
	}

	return fns[_state->read_state](_state);
}

// TODO: convert to scatterlist
static int h2fmi_read_pages(struct h2fmi_state *_state, int _count, u16 *_ces,
		u32 *_pages, struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob, u8 *_eccres,
		u8 *_eccbuf) {
	int i, j;

	if (_state->state != H2FMI_IDLE)
		return -EBUSY;

	memset(&_state->transaction, 0, sizeof(_state->transaction));

	_state->transaction.count = _count;
	_state->transaction.chips = _ces;
	_state->transaction.pages = _pages;
	_state->transaction.sg_data = _sg_data;
	_state->transaction.sg_num_data = _sg_num_data;

	_state->transaction.sg_oob = _sg_oob;
	_state->transaction.sg_num_oob = _sg_num_oob;

	_state->transaction.eccres = _eccres;
	_state->transaction.eccbuf = _eccbuf;

	spin_lock(&_state->lock);
	_state->state = H2FMI_READ;
	_state->read_state = H2FMI_READ_BEGIN;

	h2fmi_reset(_state);
	h2fmi_clear_ecc_buf(_state);

	while (_state->read_state != H2FMI_READ_COMPLETE)
		h2fmi_read_state_machine(_state);

	if (_state->transaction.busy) {
		if (cdma_wait(_state->pdata->dma1) || cdma_wait(_state->pdata->dma0)) {
			dev_err(&_state->dev->dev, "failed to wait for DMA completion.\n");
			_state->transaction.result = -ETIMEDOUT;
		} else
			_state->transaction.result = 0;
	}

	cdma_cancel(_state->pdata->dma0);
	cdma_cancel(_state->pdata->dma1);

	_state->state = H2FMI_IDLE;
	spin_unlock(&_state->lock);
	h2fmi_clear_interrupt(_state);

	// metadata whitening
	{
		struct scatterlist *sg = _state->transaction.sg_oob;
		int count = _state->transaction.sg_num_oob;
		size_t sg_off = 0;

		for (i = 0; i < _count; i++) {
			u8 *sg_ptr, *ptr;
			u32 *p;

			if (!count || !sg) {
				printk(KERN_WARNING "h2fmi: not enough SGs for metadata!\n");
				break;
			}

			sg_ptr = sg_virt(sg);
			ptr = sg_ptr + sg_off;
			p = (u32*) ptr;
			print_hex_dump(KERN_INFO, "SB1: ", DUMP_PREFIX_OFFSET, 16, 1, ptr,12, false);

			if (!sg_ptr) {
				if (!i)
					break;

				panic("Not enough SGs for metadata! Not caught earlier!\n");
			}

			if (sg->length - sg_off < _state->geo.oob_alloc_size)
				panic("SG too small for metadata whitening. %d-%d.\n",
						sg->length, sg_off);

			if (!_state->whitening_disabled)
				for (j = 0; j < 3; j++)
					((u32*) ptr)[j] ^= h2fmi_hash_table[(j + _pages[i])
							% ARRAY_SIZE(h2fmi_hash_table)];

			for (j = _state->geo.oob_size; j < _state->geo.oob_alloc_size; j++)
				ptr[j] = 0xFF;
			print_hex_dump(KERN_INFO, "SB3: ", DUMP_PREFIX_OFFSET, 16, 1, ptr,12, false);
			sg_off += _state->geo.oob_alloc_size;
			if (sg_off >= sg->length) {
				if (count == 0)
					continue;

				sg = sg_next(sg);
				sg_off = 0;
				count--;
			}
		}
	}

	if (_state->transaction.result)
		return _state->transaction.result;
	if (_state->transaction.num_failed)
		return -EIO;
	if (_state->transaction.num_ecc)
		return -EUCLEAN;
	if (_state->transaction.num_empty)
		return -ENOENT;

	return 0;
}

static int h2fmi_write_prepare_ce(struct h2fmi_state *_state) {
	int ret;

	u16 chip = _state->transaction.chips[_state->transaction.curr];
	if (!(_state->transaction.chip_mask & (1 << chip))) // chip is unused
		return -EBUSY;

	ret = h2fmi_prepare_transfer(_state);
	if (ret)
		return ret;

	_state->transaction.chip_mask &= ~(1 << chip);
	return 0;
}

static int h2fmi_write_ready(struct h2fmi_state *_state) {
	int ret = h2fmi_transfer_ready(_state);
	if (ret)
		return ret;

	if (!(readl(_state->flash_regs + H2FMI_STATUS) & 0x80)) {
		dev_err(&_state->dev->dev, "failed to ready CE for write.\n");
		return -EIO;
	}

	return 0;
}

static int h2fmi_erase_prepare(struct h2fmi_state *_state) {
	int ret = h2fmi_prepare_transfer(_state);
	if (ret)
		return ret;

	while (1) // TODO: use the interrupts for this? : <
	{
		ret = h2fmi_write_ready(_state);
		if (ret == 0)
			continue;

		break;
	}

	return ret;
}

static inline int h2fmi_write_seqin(struct h2fmi_state *_state, int _page) {
	h2fmi_set_address(_state, _page);

	return h2fmi_send_cmd(_state, NAND_CMD_SEQIN, 0x9);
}

static inline int h2fmi_write_pageprog(struct h2fmi_state *_state) {
	return h2fmi_send_cmd(_state, NAND_CMD_PAGEPROG << 8, 0);
}

static void h2fmi_do_write_page(struct h2fmi_state *_state) {
	_state->transaction.chip_mask |= (1
			<< _state->transaction.chips[_state->transaction.curr]);

	h2fmi_write_seqin(_state,
			_state->transaction.pages[_state->transaction.curr]);

	writel(2, _state->base_regs + H2FMI_CREQ);
	writel(5, _state->base_regs + H2FMI_CCMD);

	//_fmi->last_action_time = timer_get_system_microtime();

	h2fmi_write_pageprog(_state);
}

static int h2fmi_write_complete(struct h2fmi_state *_state) {
	h2fmi_clear_interrupt(_state);
	h2fmi_disable_bus(_state);
	return 0;
}

static int h2fmi_write_state_4(struct h2fmi_state *_state) {
	if (!_state->transaction.chip_mask) {
		h2fmi_disable_bus(_state);
		_state->write_state = H2FMI_WRITE_COMPLETE;
		return 0;
	}

	h2fmi_enable_chip(_state, ffs(_state->transaction.chip_mask) - 1);

	/*if(write_setting == 4 || 5) ...*/

	// last action = ...
	_state->write_state = H2FMI_WRITE_5;
	return h2fmi_prepare_transfer(_state);
}

static int h2fmi_write_state_5(struct h2fmi_state *_state) {
	int rdy = h2fmi_write_ready(_state);

	if (rdy <= 0) {
		if (!rdy)
			return 0;

		_state->transaction.result = rdy;
		_state->transaction.chip_mask &= ~(1
				<< _state->transaction.chips[_state->transaction.curr]);
		_state->write_state = H2FMI_WRITE_5;
		return rdy;
	} else {
		h2fmi_clear_interrupt(_state);

		if (!(readl(_state->flash_regs + H2FMI_STATUS) & 1))
			_state->transaction.result = -EIO;

		writel(0, _state->flash_regs + H2FMI_NREQ);

		// TODO: some write_mode stuff.

		if (_state->transaction.new_chip)
			_state->transaction.new_chip = 0;
		else {
			_state->transaction.chip_mask &= ~(1
					<< (ffs(_state->transaction.chip_mask) - 1));
		}

		_state->transaction.curr++;
	}

	_state->write_state = H2FMI_WRITE_4;
	return h2fmi_write_state_4(_state);
}

static int h2fmi_write_state_2(struct h2fmi_state *_state) {
	int rdy = h2fmi_write_ready(_state);

	if (rdy <= 0) {
		if (!rdy)
			return 0;

		_state->transaction.result = rdy;
		_state->transaction.chip_mask &= ~(1
				<< _state->transaction.chips[_state->transaction.curr]);
		_state->write_state = H2FMI_WRITE_5;
		return rdy;
	} else {
		int val;

		h2fmi_clear_interrupt(_state);

		// TODO: write mode

		val = readl(_state->flash_regs + H2FMI_STATUS) & 1;
		writel(0, _state->flash_regs + H2FMI_NREQ);

		if (!val) {
			_state->write_state = H2FMI_WRITE_3;
			h2fmi_do_write_page(_state);
			return 0;
		}

		// TODO: store first chip with fail.
		_state->transaction.result = -EIO;
		_state->write_state = H2FMI_WRITE_4;
		return h2fmi_write_state_4(_state);
	}
}

static int h2fmi_write_state_3(struct h2fmi_state *_state) {
	if (readl(_state->base_regs + H2FMI_CSTS) & 2) {
		h2fmi_clear_interrupt(_state);
		h2fmi_wait_req(_state, 2);

		_state->transaction.curr++;
		if (_state->transaction.curr >= _state->transaction.count) {
			_state->write_state = H2FMI_WRITE_4;
			return h2fmi_write_state_4(_state);
		} else {
			if (h2fmi_write_prepare_ce(_state) < 0)
				h2fmi_do_write_page(_state);
			else {
				_state->write_state = H2FMI_WRITE_2;
				// update last active time
				return 0;
			}
		}
	} else {
		/*uint64_t current_time = timer_get_system_microtime();
		 if ((current_time - _fmi->last_action_time) > _fmi->time_interval) // _fmi->time_interval rename to _fmi->time_interval
		 {
		 _fmi->current_status = setting;
		 _fmi->state.write_state = H2FMI_WRITE_4;
		 return h2fmi_write_state_4_handler(_fmi);
		 }*/
	}

	return 0;
}

static int h2fmi_write_state_1(struct h2fmi_state *_state) {
	if (!h2fmi_write_prepare_ce(_state)) {
		//_fmi->last_action_time = timer_get_system_microtime();
		_state->write_state = H2FMI_WRITE_2;
		return 0;
	} else {
		h2fmi_do_write_page(_state);
		_state->write_state = H2FMI_WRITE_3;
		return h2fmi_write_state_3(_state);
	}
}

static int h2fmi_write_begin(struct h2fmi_state *_state) {
	_state->transaction.curr = 0;
	_state->transaction.busy = 1;
	_state->transaction.new_chip = 1;

	h2fmi_setup_transfer(_state);
	h2fmi_rw_large_page(_state);

	_state->write_state = H2FMI_WRITE_1;
	return h2fmi_write_state_1(_state);
}

static int h2fmi_write_state_machine(struct h2fmi_state *_state) {
	static int (*fns[])(struct h2fmi_state*) = {
		h2fmi_write_begin,
		h2fmi_write_state_1,
		h2fmi_write_state_2,
		h2fmi_write_state_3,
		h2fmi_write_state_4,
		h2fmi_write_state_5,
		h2fmi_write_complete,
	};

	if (_state->state != H2FMI_READ) {
		dev_err(&_state->dev->dev,
				"write_state_machine called whilst not writeing!\n");
		return -EINVAL;
	}

	if (_state->write_state > ARRAY_SIZE(fns)) {
		dev_err(&_state->dev->dev, "invalid write state %d.\n",
				_state->write_state);
		return -EINVAL;
	}

	return fns[_state->write_state](_state);
}

static int h2fmi_write_pages(struct h2fmi_state *_state, int _count,
		u16 *_chips, u32 *_pages, struct scatterlist *_sg_data,
		size_t _sg_num_data, struct scatterlist *_sg_oob, size_t _sg_num_oob,
		enum h2fmi_write_mode _mode) {
	if (_state->state != H2FMI_IDLE)
		return -EBUSY;

	memset(&_state->transaction, 0, sizeof(_state->transaction));

	_state->transaction.count = _count;
	_state->transaction.chips = _chips;
	_state->transaction.pages = _pages;

	_state->transaction.sg_data = _sg_data;
	_state->transaction.sg_num_data = _sg_num_data;

	_state->transaction.sg_oob = _sg_oob;
	_state->transaction.sg_num_oob = _sg_num_oob;

	_state->transaction.write_mode = _mode;

	spin_lock(&_state->lock);
	_state->state = H2FMI_WRITE;
	_state->write_state = H2FMI_WRITE_BEGIN;

	// TODO: data whitening?

	/*{
		int i, j;

			struct scatterlist *sg = _state->transaction.sg_oob;
			int count = _state->transaction.sg_num_oob;
			size_t sg_off = 0;

			for (i = 0; i < _count; i++) {
				u8 *sg_ptr, *ptr;
				u32 *p;

				if (!count || !sg) {
					printk(KERN_WARNING "h2fmi: not enough SGs for metadata!\n");
					break;
				}

				sg_ptr = sg_virt(sg);
				ptr = sg_ptr + sg_off;
				p = (u32*) ptr;
				print_hex_dump(KERN_INFO, "SB1: ", DUMP_PREFIX_OFFSET, 16, 1, ptr,12, false);

				if (!sg_ptr) {
					if (!i)
						break;

					panic("Not enough SGs for metadata! Not caught earlier!\n");
				}

				if (sg->length - sg_off < _state->geo.oob_alloc_size)
					panic("SG too small for metadata whitening. %d-%d.\n",
							sg->length, sg_off);

				if (!_state->whitening_disabled)
					for (j = 0; j < 3; j++)
						((u32*) ptr)[j] ^= h2fmi_hash_table[(j + _pages[i])
								% ARRAY_SIZE(h2fmi_hash_table)];

				print_hex_dump(KERN_INFO, "SB3: ", DUMP_PREFIX_OFFSET, 16, 1, ptr,12, false);
				sg_off += _state->geo.oob_alloc_size;
				if (sg_off >= sg->length) {
					if (count == 0)
						continue;

					sg = sg_next(sg);
					sg_off = 0;
					count--;
				}
			}
		}
*/

	h2fmi_reset(_state);
	h2fmi_set_timing_mode(_state, 1);

	while (_state->write_state != H2FMI_WRITE_COMPLETE)
		h2fmi_write_state_machine(_state);

	if (_state->transaction.busy) {
		if (cdma_wait(_state->pdata->dma0) || cdma_wait(_state->pdata->dma1)) {
			dev_err(&_state->dev->dev, "failed to wait for DMA completion.\n");
			_state->transaction.result = -ETIMEDOUT;
		} else
			_state->transaction.result = 0;
	}

	cdma_cancel(_state->pdata->dma0);
	cdma_cancel(_state->pdata->dma1);

	h2fmi_reset_timing(_state);

	_state->state = H2FMI_IDLE;
	spin_unlock(&_state->lock);
	h2fmi_clear_interrupt(_state);

	if (_state->transaction.result)
		return _state->transaction.result;

	if (_state->transaction.num_failed)
		return -EIO;

	return 0;
}

static int h2fmi_erase_blocks(struct h2fmi_state *_state, int _num, u16 *_ces,
		u32 *_pages) {
	int i;
	int eraseOK = 1;

	if (_state->state != H2FMI_IDLE)
		return -EBUSY;

	memset(&_state->transaction, 0, sizeof(_state->transaction));

	_state->transaction.count = _num;
	_state->transaction.chips = _ces;
	_state->transaction.pages = _pages;

	spin_lock(&_state->lock);
	_state->state = H2FMI_ERASE;

	h2fmi_reset(_state);
	h2fmi_set_timing_mode(_state, 1);

	while (_state->transaction.curr < _state->transaction.count) {
		int chip = _state->transaction.chips[_state->transaction.curr];
		int ret;

		if (_state->transaction.chip_mask & (1 << chip)) {
			h2fmi_enable_chip(_state, chip);
			_state->transaction.curr++;

			ret = h2fmi_erase_prepare(_state);

			if (ret < 0) {
				dev_err(&_state->dev->dev,
						"failed to erase block at %d, err %d.\n",
						_state->transaction.pages[_state->transaction.curr],
						ret);

				// TODO: store which chip failed?

				eraseOK = 0;
				break;
			} else
				_state->transaction.chip_mask &= ~(1 << chip);
		} else {
			int i;
			for (i = _state->transaction.curr; i < _state->transaction.count;
					i++) {
				int page;
				int chip = _state->transaction.chips[i];
				if (_state->transaction.chip_mask & (1 << chip))
					break;

				page = _state->transaction.pages[i];

				h2fmi_enable_chip(_state, chip);
				h2fmi_set_block_address(_state, page);
				h2fmi_send_cmd(_state,
				NAND_CMD_ERASE1 | (NAND_CMD_ERASE2 << 8), 0xb);

				_state->transaction.chip_mask |= (1 << chip);
			}
		}
	}

	// flush all of the chips used.
	for (i = 0; i < _state->transaction.count; i++) {
		int chip = _state->transaction.chips[i];
		int ret;

		if (!(_state->transaction.chip_mask & (1 << chip)))
			continue;

		h2fmi_enable_chip(_state, chip);

		ret = h2fmi_erase_prepare(_state);

		if (ret < 0) {
			dev_err(&_state->dev->dev, "failed to erase block at %d, err %d.\n",
					_state->transaction.pages[_state->transaction.curr], ret);

			// TODO: store which chip failed?

			eraseOK = 0;
		} else
			_state->transaction.chip_mask &= ~(1 << chip);
	}

	h2fmi_reset_timing(_state);
	h2fmi_disable_bus(_state);

	_state->state = H2FMI_IDLE;
	spin_unlock(&_state->lock);

	if (!eraseOK)
		return -EIO;

	return 0;
}

static int h2fmi_read_single_page(struct h2fmi_state *_state, u16 _ce,
		int _page, u8 *_buffer, int _raw) {
	struct scatterlist sg_buf, sg_oob;
	u8 *oobbuf = kmalloc(_state->geo.oob_size, GFP_KERNEL);
	u16 chip = _state->chip_map[_ce];
	u32 page = _page;
	int ret;

	sg_init_one(&sg_buf, _buffer, _state->geo.bytes_per_page);
	sg_init_one(&sg_oob, oobbuf, _state->geo.oob_size);

	h2fmi_setup_aes(_state, !_raw, 0, _buffer);
	ret = h2fmi_read_pages(_state, 1, &chip, &page, &sg_buf, _buffer ? 1 : 0,
			&sg_oob, oobbuf ? 1 : 0, NULL, NULL);

	kfree(oobbuf);
	return ret;
}

// NAND interface implementation

static int h2fmi_nand_default_aes(struct apple_nand *_nd, struct cdma_aes *_aes,
		int _dec) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);
	return h2fmi_default_aes(state, _aes, _dec);
}

static int h2fmi_nand_aes(struct apple_nand *_nd, struct cdma_aes *_aes) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);
	cdma_aes(state->pdata->dma0, _aes);
	return 0;
}

static int h2fmi_nand_read(struct apple_nand *_nd, size_t _count, u16 *_chips,
		page_t *_pages, struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);

	return h2fmi_read_pages(state, _count, _chips, _pages, _sg_data,
			_sg_num_data, _sg_oob, _sg_num_oob, NULL, NULL);
}

static int h2fmi_nand_write(struct apple_nand *_nd, size_t _count, u16 *_chips,
		page_t *_pages, struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);
	return h2fmi_write_pages(state, _count, _chips, _pages, _sg_data,
			_sg_num_data, _sg_oob, _sg_num_oob, H2FMI_WRITE_NORMAL);
}

static int h2fmi_nand_erase(struct apple_nand *_nd, size_t _count, u16 *_chips,
		page_t *_blocks) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);
	return h2fmi_erase_blocks(state, _count, _chips, _blocks);
}

static int h2fmi_nand_get(struct apple_nand *_nd, int _info) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);

	switch (_info) {
	case NAND_NUM_CE:
		return state->geo.num_ce;

	case NAND_BITMAP:
		return state->bitmap;

	case NAND_BLOCKS_PER_CE:
		return state->geo.blocks_per_ce;

	case NAND_PAGES_PER_CE:
		return state->geo.pages_per_ce;

	case NAND_PAGES_PER_BLOCK:
		return state->geo.pages_per_block;

	case NAND_BLOCK_ADDRESS_SPACE:
		return state->geo.block_address_space;

	case NAND_ECC_STEPS:
		return state->geo.ecc_steps;

	case NAND_ECC_BITS:
		return state->ecc_bits;

	case NAND_BYTES_PER_SPARE:
		return state->geo.bytes_per_spare;

	case NAND_BANKS_PER_CE_VFL:
		return state->geo.banks_per_ce_vfl;

	case NAND_BANKS_PER_CE:
		return state->geo.banks_per_ce;

	case NAND_BLOCKS_PER_BANK:
		return state->geo.blocks_per_bank;

	case NAND_BANK_ADDRESS_SPACE:
		return state->geo.bank_address_space;

	case NAND_TOTAL_BLOCK_SPACE:
		return state->geo.total_block_space;

	case NAND_PAGE_SIZE:
		return state->geo.bytes_per_page;

	case NAND_OOB_SIZE:
		return state->geo.oob_size;

	case NAND_OOB_ALLOC:
		return state->geo.oob_alloc_size;

	default:
		return 0;
	}
}

static void h2fmi_device_set_ftl_region(uint32_t _lpn, uint32_t _a2,
		uint32_t _count, void* _buf) {
	h2fmi_ftl_count = _count;
	h2fmi_ftl_databuf = (uint32_t) _buf;
	h2fmi_ftl_smth[0] = _lpn;
	h2fmi_ftl_smth[1] = _a2;
}

static int h2fmi_nand_set(struct apple_nand *_nd, int _info, int _val) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);

	switch (_info) {
	case NAND_BANKS_PER_CE_VFL:
		state->geo.banks_per_ce_vfl = _val;
		return 0;

	default:
		return -EPERM;
	}
}

static int h2fmi_nand_is_bad(struct apple_nand *_nd, u16 _ce, page_t _page) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);
	return h2fmi_is_block_bad(state, _ce, _page / state->geo.pages_per_block);
}

static void h2fmi_nand_set_bad(struct apple_nand *_nd, u16 _ce, page_t _page) {
	struct h2fmi_state *state = container_of(_nd, struct h2fmi_state, nand);
	h2fmi_mark_block_bad(state, _ce, _page / state->geo.pages_per_block);
}

static int h2fmi_scan_bbt(struct h2fmi_state *_state) {
	size_t sz = DIV_ROUND_UP(_state->geo.blocks_per_ce, 8);
	int i, ret;
	for (i = 0; i < _state->num_chips; i++) {
		_state->bbt[i] = kzalloc(sz, GFP_KERNEL);
		ret = apple_nand_special_page(&_state->nand, i, "DEVICEINFOBBT\0\0\0",
				_state->bbt[i], sz);
		if (ret)
			return ret;

		printk(KERN_INFO "%s: scanned one chip.\n", __func__);
	}

	printk(KERN_INFO "%s: bbt scanned!\n", __func__);
	return 0;
}

static int h2fmi_detect_nand(struct h2fmi_state *_state) {
	char goodbuf[8];
	int ret;
	int i;
	int count = 0;
	int have_good = 0;
	struct clk *clk;
	struct h2fmi_timing_setup timing_setup;
	u8 timing_info[5];

	_state->num_chips = 0;
	_state->bitmap = 0;

	ret = h2fmi_reset_all(_state);
	if (ret)
		return ret;

	for (i = 0; i < H2FMI_MAX_CHIPS; i++) {

		h2fmi_enable_chip(_state, i);

		ret = h2fmi_readid(_state, 0);

		char buf[8];
		h2fmi_read(_state, buf, sizeof(buf));
		if (ret) {
			h2fmi_disable_chip(_state, i);
			return ret;
		}
		if (!memcmp(buf, "\0\0\0\0\0\0\0\0", 6) // 6 is not an error
		|| !memcmp(buf, "\xff\xff\xff\xff\xff\xff\xff\xff", 6)) {
			h2fmi_disable_chip(_state, i);
			h2fmi_reset(_state);
			continue;
		}

		if (!have_good || memcmp(goodbuf, buf, sizeof(goodbuf)) == 0) {
			// We found another identical chip.
			int id = count++;

			_state->num_chips++;
			_state->bitmap |= (1 << i);
			printk( "%xbitmap!\n", _state->bitmap);

			_state->chip_map[id] = i;
			dev_info(&_state->dev->dev,
					"found chip on ce%d: %02x %02x %02x %02x %02x %02x %02x %02x.\n",
					i, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
					buf[7]);

			if (!have_good) {
				have_good = 1;
				memcpy(goodbuf, buf, sizeof(goodbuf));
			}
		} else {
			// Weird chip.
			dev_warn(&_state->dev->dev,
					"weird chip on ce%d: %02x %02x %02x %02x %02x %02x %02x %02x.\n",
					i, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
					buf[7]);
		}

		h2fmi_disable_chip(_state, i);
		h2fmi_reset(_state);
	}

	_state->chip_info = h2fmi_find_chip_info(goodbuf);
	if (!_state->chip_info) {
		dev_err(&_state->dev->dev, "failed to find device info, bailing.\n");
		return -EINVAL;
	}

	// geometry
	_state->ecc_step_shift = _state->pdata->ecc_step_shift;
	_state->geo.num_ce = count;
	_state->geo.bytes_per_page = _state->chip_info->bytes_per_page;
	_state->geo.pages_per_block = _state->chip_info->pages_per_block;
	_state->geo.block_address_space = roundup_pow_of_two(
			_state->geo.pages_per_block);
	_state->geo.blocks_per_ce = _state->chip_info->blocks_per_ce;
	_state->geo.pages_per_ce = _state->geo.pages_per_block
			* _state->geo.blocks_per_ce;
	_state->geo.ecc_steps = _state->chip_info->bytes_per_page
			>> _state->ecc_step_shift;
	_state->geo.bytes_per_spare = _state->chip_info->bytes_per_spare;
	_state->geo.banks_per_ce = _state->chip_info->banks_per_ce;
	_state->geo.blocks_per_bank = _state->geo.blocks_per_ce
			/ _state->geo.banks_per_ce;
	_state->geo.oobsize = _state->geo.bytes_per_spare;
	_state->geo.oob_alloc_size = 0xC;
	_state->geo.oob_size = 0xA;

	_state->geo.chip_size = _state->geo.bytes_per_page
			* _state->geo.pages_per_block * _state->geo.total_block_space;

	_state->geo.chip_shift = ffs(_state->geo.chip_size) - 1;
	_state->geo.page_shift = ffs(_state->geo.bytes_per_page) - 1;
	_state->geo.pagemask = _state->geo.chip_shift - 1;
	_state->geo.pageoffmask = _state->geo.page_shift - 1;

	// Check for power-of-two
	if (!(_state->geo.blocks_per_ce & (_state->geo.blocks_per_ce - 1))) {
		_state->geo.bank_address_space = _state->geo.blocks_per_bank;
		_state->geo.total_block_space = _state->geo.blocks_per_ce;
	} else {
		u32 bas = roundup_pow_of_two(_state->geo.blocks_per_bank);
		_state->geo.bank_address_space = bas;
		_state->geo.total_block_space = ((_state->geo.banks_per_ce - 1) * bas)
				+ _state->geo.blocks_per_bank;
	}

	_state->ecc_bits = h2fmi_calculate_ecc_bits(_state);

	if (_state->ecc_bits > 8) {
		_state->ecc_tag = (_state->ecc_bits * 8) / 10;
	} else
		_state->ecc_tag = 8;

	_state->ecc_fmt = ((_state->ecc_bits & 0x1F) << 3) | 5;
	_state->page_fmt = _state->geo.ecc_steps | 0x40000
			| ((_state->geo.oob_size & 0x3F) << 25)
			| ((_state->geo.oob_size & 0x3F) << 19);

	dev_info(&_state->dev->dev, "ecc-fmt=0x%08x, page-fmt=0x%08x.\n",
			_state->ecc_fmt, _state->page_fmt);

	// timing
	memset(&timing_setup, 0, sizeof(timing_setup));

	clk = clk_get(&_state->dev->dev, "lperf1");
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	timing_setup.freq = clk_get_rate(clk);

	clk_put(clk);
	//printk("h2fmi: timing_setup.freq : %x.\n",clk);
	timing_setup.f = _state->pdata->smth->some_array[3];

	timing_setup.g = _state->pdata->smth->some_array[4];
	timing_setup.h = _state->pdata->smth->some_array[5];
	timing_setup.i = _state->pdata->smth->some_array[6];
	timing_setup.j = _state->chip_info->timing.unk4;
	timing_setup.k = _state->chip_info->timing.unk5;
	timing_setup.l = _state->chip_info->timing.unk6;
	timing_setup.m = _state->chip_info->timing.unk7;
	timing_setup.n = _state->chip_info->timing.unk8;

	timing_setup.o = 0;

	timing_setup.p = _state->chip_info->timing.unk1;
	timing_setup.q = _state->chip_info->timing.unk2;
	timing_setup.r = _state->chip_info->timing.unk3;

	ret = h2fmi_setup_timing(&timing_setup, timing_info);
	if (ret)
		return ret;

	_state->timing = (timing_info[4] & 0xF) | ((timing_info[3] & 0xF) << 4)
			| ((timing_info[1] & 0xF) << 8) | ((timing_info[0] & 0xF) << 12)
			| ((timing_info[2] & 0xF) << 16);
	writel(_state->timing, _state->flash_regs + H2FMI_TIMING);
	dev_info(&_state->dev->dev, "timing 0x%08x.\n", _state->timing);

	printk("h2fmi: scanning bbt...\n");

	ret = h2fmi_scan_bbt(_state);
	if (ret)
		return ret;

	return apple_nand_register(&_state->nand, _state->pdata->vfl,
			&_state->dev->dev);
}

int h2fmi_probe(struct platform_device *_dev) {
	int ret = 0;
	struct h2fmi_state *state;
	struct resource *res;

	state = kzalloc(sizeof(*state), GFP_KERNEL);
	if (!state) {
		dev_err(&_dev->dev, "failed to allocate state.\n");
		return ENOMEM;
	}

	state->dev = _dev;
	state->pdata = _dev->dev.platform_data;
	spin_lock_init(&state->lock);

	// Setup interface
	{
		state->nand.default_aes = h2fmi_nand_default_aes;
		state->nand.aes = h2fmi_nand_aes;
		state->nand.setup_aes = h2fmi_setup_aes;
		state->nand.read = h2fmi_nand_read;
		state->nand.write = h2fmi_nand_write;
		state->nand.erase = h2fmi_nand_erase;
		state->nand.get = h2fmi_nand_get;
		state->nand.set = h2fmi_nand_set;
		state->nand.is_bad = h2fmi_nand_is_bad;
		state->nand.set_bad = h2fmi_nand_set_bad;
		state->nand.set_ftl_region = h2fmi_device_set_ftl_region;
		state->nand.h2fmi_set_emf = h2fmi_set_emf;
		state->nand.h2fmi_get_emf = h2fmi_get_emf;
	}

	state->clk = clk_get(&_dev->dev, "fmi");
	if (IS_ERR(state->clk)) {
		dev_err(&_dev->dev, "failed to get main clock.\n");
		goto err_state;
	}

	state->clk_bch = clk_get(&_dev->dev, "fmi-bch");
	if (IS_ERR(state->clk_bch)) {
		dev_err(&_dev->dev, "failed to get BCH clock.\n");
		goto err_clk;
	}

	res = platform_get_resource(_dev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&_dev->dev, "failed to find base IO memory.\n");
		goto err_clk_bch;
	}

	state->base_dma = res->start;
	state->base_regs = ioremap(res->start, resource_size(res));
	if (!state->base_regs) {
		dev_err(&_dev->dev, "failed to map base IO memory.\n");
		goto err_clk_bch;
	}

	res = platform_get_resource(_dev, IORESOURCE_MEM, 1);
	if (!res) {
		dev_err(&_dev->dev, "failed to find flash IO memory.\n");
		goto err_base_regs;
	}

	state->flash_dma = res->start;
	state->flash_regs = ioremap(res->start, resource_size(res));
	if (!state->flash_regs) {
		dev_err(&_dev->dev, "failed to map flash IO memory.\n");
		goto err_base_regs;
	}

	res = platform_get_resource(_dev, IORESOURCE_MEM, 2);
	if (!res) {
		dev_err(&_dev->dev, "failed to find bch IO memory.\n");
		goto err_flash_regs;
	}

	state->ecc_dma = res->start;
	state->ecc_regs = ioremap(res->start, resource_size(res));
	if (!state->ecc_regs) {
		dev_err(&_dev->dev, "failed to map bch IO memory.\n");
		goto err_flash_regs;
	}

	res = platform_get_resource(_dev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&_dev->dev, "failed to find IRQ.\n");
		goto err_ecc_regs;
	}

	state->irq = res->start;
	/*if(request_irq(state->irq, h2fmi_irq_handler,
	 IRQF_SHARED, "h2fmi", state) < 0)
	 {
	 dev_err(&_dev->dev, "failed to request IRQ.\n");
	 goto err_ecc_regs;
	 }*/

	clk_enable(state->clk);
	clk_enable(state->clk_bch);

	h2fmi_init(state);
	platform_set_drvdata(_dev, state);

	ret = h2fmi_detect_nand(state);
	goto done;

	err_ecc_regs:
	iounmap(state->ecc_regs);

	err_flash_regs:
	iounmap(state->flash_regs);

	err_base_regs:
	iounmap(state->base_regs);

	err_clk_bch: clk_put(state->clk_bch);

	err_clk: clk_put(state->clk);

	err_state: kfree(state);

	done: return ret;
}

int h2fmi_remove(struct platform_device *_dev) {
	struct h2fmi_state *state = platform_get_drvdata(_dev);

	free_irq(state->irq, state);

	clk_disable(state->clk_bch);
	clk_put(state->clk_bch);

	clk_disable(state->clk);
	clk_put(state->clk);

	kfree(state);
	return 0;
}

void h2fmi_shutdown(struct platform_device *_dev) {
}

#ifdef CONFIG_PM
int h2fmi_suspend(struct platform_device *_dev, pm_message_t _state)
{
	return 0;
}

int h2fmi_resume(struct platform_device *_dev)
{
	return 0;
}
#else
#define h2fmi_suspend	NULL
#define h2fmi_resume	NULL
#endif

static struct platform_driver h2fmi_driver = { .driver =
		{ .name = "apple-h2fmi", },

.probe = h2fmi_probe, .remove = h2fmi_remove, .shutdown = h2fmi_shutdown,
		.suspend = h2fmi_suspend, .resume = h2fmi_resume, };

int __init h2fmi_mod_init(void) {
	int i;
	uint32_t val = 0x50F4546A;

	for (i = 0; i < ARRAY_SIZE(h2fmi_hash_table); i++) {
		int j;
		val = (0x19660D * val) + 0x3C6EF35F;

		for (j = 1; j < 763; j++) {
			val = (0x19660D * val) + 0x3C6EF35F;
		}

		h2fmi_hash_table[i] = val;
	}

	return platform_driver_register(&h2fmi_driver);
}
module_init(h2fmi_mod_init);

void __exit h2fmi_mod_exit(void) {
	platform_driver_unregister(&h2fmi_driver);
}
module_exit(h2fmi_mod_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Richard Ian Taylor");
MODULE_DESCRIPTION("Apple H2FMI NAND Driver");
