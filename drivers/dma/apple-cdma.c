/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <linux/completion.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/err.h>

#include <plat/cdma.h>

//#define CDMA_DEBUG

#ifdef CDMA_DEBUG
#define cdma_dbg(state, args...) dev_info(&(state)->dev->dev, args)
#else
#define cdma_dbg(state, args...)
#endif

typedef struct _cmda_segment
{
	u32 next;	// Should be a DMA address
	u32 flags;
	u32 data;			// Should be a DMA address
	u32 length;
	u32 iv[4];
} __attribute__((packed)) __attribute__((aligned(4))) cdma_segment_t;

struct cdma_segment_tag
{
	cdma_segment_t *seg;
	dma_addr_t addr;

	struct list_head list;
};

// These first 3 are 32-bit blocks.
#define CDMA_ENABLE(x)			(0x0 + ((x)*0x4))
#define CDMA_DISABLE(x)			(0x8 + ((x)*0x4))
#define CDMA_STATUS(x)			(0x10 + ((x)*0x4))

#define DMA 					0x87000000
#define DMA_AES 				0x800000

#define CDMA_CHAN(x)			(0x1000*((x)+1))
#define CDMA_CSTATUS(x)			(CDMA_CHAN(x) + 0x0)
#define CDMA_CCONFIG(x)			(CDMA_CHAN(x) + 0x4)
#define CDMA_CREG(x)			(CDMA_CHAN(x) + 0x8)
#define CDMA_CSIZE(x)			(CDMA_CHAN(x) + 0xC)
#define CDMA_CSEGPTR(x)			(CDMA_CHAN(x) + 0x14)

#define CDMA_AES(x)				((x)*0x1000)
#define CDMA_AES_CONFIG(x)		(CDMA_AES(x) + 0x0)
#define CDMA_AES_KEY(x, y)		(CDMA_AES(x) + 0x20 + ((y)*4))

#define CSTATUS_ACTIVE			(1 << 0)
#define CSTATUS_SETUP			(1 << 1)
#define CSTATUS_CONT			(1 << 3)
#define CSTATUS_TXRDY			((1 << 17) | (1 << 16))
#define CSTATUS_INTERR			(1 << 18)
#define CSTATUS_INTCLR			(1 << 19)
#define CSTATUS_SPURCIR			(1 << 20)
#define CSTATUS_AES_SHIFT		(8)
#define CSTATUS_AES_MASK		(0xFF) // This is a guess
#define CSTATUS_AES(x)			(((x) & CSTATUS_AES_MASK) << CSTATUS_AES_SHIFT)

#define CCONFIG_DIR				(1 << 1)
#define CCONFIG_BURST_SHIFT		(2)
#define CCONFIG_BURST_MASK		(0x3)
#define CCONFIG_BURST(x)		(((x) & CCONFIG_BURST_MASK) << CCONFIG_BURST_SHIFT)
#define CCONFIG_WORDSIZE_SHIFT	(4)
#define CCONFIG_WORDSIZE_MASK	(0x7)
#define CCONFIG_WORDSIZE(x)		(((x) & CCONFIG_WORDSIZE_MASK) << CCONFIG_WORDSIZE_SHIFT)
#define CCONFIG_PERI_SHIFT		(16)
#define CCONFIG_PERI_MASK		(0x3F)
#define CCONFIG_PERIPHERAL(x)	(((x) & CCONFIG_PERI_MASK) << CCONFIG_PERI_SHIFT)

#define FLAG_DATA				(1 << 0)
#define FLAG_ENABLE				(1 << 1)
#define FLAG_LAST				(1 << 8)
#define FLAG_AES				(1 << 16)
#define FLAG_AES_START			(1 << 17)

#define AES_ENCRYPT			(1 << 16)
#define AES_ENABLED				(1 << 17)
#define AES_128					(0 << 18)
#define AES_192					(1 << 18)
#define AES_256					(2 << 18)
#define AES_KEY					(1 << 20)
#define AES_GID					(2 << 20)
#define AES_UID					(4 << 20)

#define CDMA_MAX_CHANNELS	37

struct cdma_channel_state
{
	unsigned active: 1;
	unsigned in_use: 1;

	struct scatterlist *sg;
	size_t sg_count;
	size_t sg_offset;

	size_t count;
	size_t done;
	int current_transfer;

	struct cdma_aes *aes;
	int aes_channel;

	struct list_head segments;
	struct completion completion;
};

struct cdma_state
{
	struct platform_device *dev;
	void *__iomem regs, *__iomem aes_regs;
	struct clk *clk;
	struct dma_pool *pool;
	int irq;
	int num_channels;

	u32 aes_bitmap;
	struct cdma_channel_state channels[CDMA_MAX_CHANNELS];
};

static struct cdma_state *cdma_state = NULL;
static DECLARE_COMPLETION(cdma_completion);

static cdma_segment_t *cdma_alloc_segment(struct cdma_state *_state, struct cdma_channel_state *_cstate, dma_addr_t *_addr)
{
	cdma_segment_t *ret = dma_pool_alloc(_state->pool, GFP_KERNEL, _addr);
	struct cdma_segment_tag *tag = kzalloc(sizeof(*tag), GFP_KERNEL);

	memset(ret, 0, sizeof(*ret));
	tag->seg = ret;
	tag->addr = *_addr;

	list_add_tail(&tag->list, &_cstate->segments);
	return ret;
}

static void cdma_free_segments(struct cdma_state *_state, struct cdma_channel_state *_cstate)
{
	struct list_head *pos;

	list_for_each(pos, &_cstate->segments)
	{
		struct cdma_segment_tag *tag = container_of(pos, struct cdma_segment_tag, list);
		dma_pool_free(_state->pool, tag->seg, tag->addr);
		kfree(tag);
	}

	INIT_LIST_HEAD(&_cstate->segments);
}

static inline void cdma_next_sg(struct cdma_channel_state *_cstate)
{
	_cstate->sg = sg_next(_cstate->sg);
	_cstate->sg_count--;
	_cstate->sg_offset = 0;
}

static int cdma_activate(struct cdma_state *_state, int _chan, int _enable)
{
	u32 status;
	int block = (_chan >> 5);
	u32 mask = 1 << ((_chan & 0x1f)+1);

	cdma_dbg(_state, "%s: %d %d -> %d %d.\n", __func__, _chan, _enable, block, mask);

	status = readl(_state->regs + CDMA_STATUS(block));
	if((status & mask) && !_enable)
	{
		// disable
		writel(mask, _state->regs + CDMA_DISABLE(block));
	}
	else if(!(status & mask) && _enable)
	{
		// enable
		writel(mask, _state->regs + CDMA_ENABLE(block));
	}

	cdma_dbg(_state, "%s: 0x%08x.\n", __func__, readl(_state->regs + CDMA_STATUS(block)));
	
	_state->channels[_chan].active = _enable;
	return (status & mask)? 1 : 0;
}

static int cdma_continue(struct cdma_state *_state, int _chan)
{
	struct cdma_channel_state *cstate = &_state->channels[_chan];
	dma_addr_t addr = 0;
	int segsleft = 32;
	int amt_done = 0;
	u32 flags;

	if(!cstate->count || !cstate->sg)
	{
		dev_err(&_state->dev->dev, "tried to transfer nothing.\n");
		return -EINVAL;
	}

	if(cstate->aes)
	{
		struct cdma_aes *aes = cstate->aes;
		cdma_segment_t *seg = cdma_alloc_segment(_state, cstate, &addr);

		while(segsleft > 0)
		{
			int aes_seg_size = aes->data_size;
			int aes_seg_offset = 0;

			seg->flags = FLAG_ENABLE;
			cstate->aes->gen_iv(cstate->aes->iv_param, cstate->current_transfer++, seg->iv);
			segsleft--;

			while(aes_seg_offset < aes_seg_size)
			{
				int len = cstate->sg->length - cstate->sg_offset;
				if(len > aes_seg_size)
					len = aes_seg_size;

				seg = cdma_alloc_segment(_state, cstate, &seg->next);
				if(!seg)
				{
					dev_err(&_state->dev->dev, "failed to allocate segment!\n");
					return -ENOMEM;
				}

				seg->data = sg_phys(cstate->sg) + cstate->sg_offset;
				seg->flags = FLAG_ENABLE | FLAG_DATA | FLAG_AES;
				if(!aes_seg_offset)
					seg->flags |= FLAG_AES_START;
				seg->length = len;
				segsleft--;

				amt_done += len;
					cstate->sg_offset += len;

				cdma_dbg(_state, "generated AES segment: (%p(%u) flags=0x%08x)\n",
						(void*)seg->data, seg->length, seg->flags);

				cdma_next_sg(cstate);

				if(!cstate->sg_count)
				{
					seg->flags |= FLAG_LAST;
					segsleft = 0;
					break;
				}

				aes_seg_offset += len;
			}

			// Empty last seg OR next seg.
			seg = cdma_alloc_segment(_state, cstate, &seg->next);
		}
	}
	else
	{
		cdma_segment_t *seg = cdma_alloc_segment(_state, cstate, &addr);

		while(segsleft > 0)
		{
			seg->flags = FLAG_ENABLE | FLAG_DATA;
			seg->length = cstate->sg->length;
			seg->data = sg_phys(cstate->sg);

			cdma_dbg(_state, "generated segment: (%p(%u) flags=0x%08x)\n",
					(void*)seg->data, seg->length, seg->flags);

			cdma_next_sg(cstate);

			if(!cstate->sg_count)
			{
				seg->flags |= FLAG_LAST;
				segsleft = 0;
			}

			// Empty last seg OR next seg
			seg = cdma_alloc_segment(_state, cstate, &seg->next);
		}
	}

	cstate->done += amt_done;
	writel(addr, _state->regs + CDMA_CSEGPTR(_chan));

	flags = 0x1C0009;
	if(cstate->aes_channel)
	{
		cdma_dbg(_state, "%s: AES!\n", __func__);
		flags |= (cstate->aes_channel << 8);
	}

	writel(flags, _state->regs + CDMA_CSTATUS(_chan));

	cdma_dbg(_state, "%s: %d 0x%08x.\n", __func__, _chan, readl(_state->regs + CDMA_CSTATUS(_chan)));
	return 0;
}

int cdma_begin(u32 _channel, cdma_dir_t _dir, struct scatterlist *_sg, size_t _sg_count, size_t _size, dma_addr_t _reg, size_t _burst, size_t _busw, u32 _pid)
{
	struct cdma_state *state;
	struct cdma_channel_state *cstate;
	u32 flags = 0;

	wait_for_completion(&cdma_completion);
	state = cdma_state; // TODO: somewhat hacky.
	cstate = &state->channels[_channel];

	if(_channel > state->num_channels)
	{
		dev_err(&state->dev->dev, "no such channel %d.\n", _channel);
		return -ENOENT;
	}

	switch(_burst)
	{
	case 1:
		flags |= (0 << 2);
		break;
	
	case 2:
		flags |= (1 << 2);
		break;

	case 4:
		flags |= (2 << 2);
		break;

	default:
		dev_err(&state->dev->dev, "invalid burst size %d.\n", _burst);
		return -EINVAL;
	}

	switch(_busw)
	{
	case 1:
		flags |= (0 << 4);
		break;

	case 2:
		flags |= (1 << 4);
		break;

	case 4:
		flags |= (2 << 4);
		break;

	case 8:
		flags |= (3 << 4);
		break;

	case 16:
		flags |= (4 << 4);
		break;

	case 32:
		flags |= (5 << 4);
		break;

	default:
		dev_err(&state->dev->dev, "invalid bus width %d.\n", _busw);
		return -EINVAL;
	}

	flags |= ((_pid & 0x3f) << 16) | ((_dir & 1) << 1);

	INIT_COMPLETION(cstate->completion);

	cstate->sg = _sg;
	cstate->sg_count = _sg_count;
	cstate->sg_offset = 0;

	cstate->count = _size;
	cstate->done = 0;
	cstate->current_transfer = 0;

	cdma_activate(state, _channel, 1);
	writel(2, state->regs + CDMA_CSTATUS(_channel));
	writel(flags, state->regs + CDMA_CCONFIG(_channel));
	writel(_reg, state->regs + CDMA_CREG(_channel));
	writel(_size, state->regs + CDMA_CSIZE(_channel));

	return cdma_continue(state, _channel);
}
EXPORT_SYMBOL_GPL(cdma_begin);

int cdma_cancel(u32 _channel)
{
	struct cdma_state *state;
	struct cdma_channel_state *cstate;

	wait_for_completion(&cdma_completion);
	state = cdma_state; // TODO: somewhat hacky.
                                                       	cstate = &state->channels[_channel];

	if(_channel > state->num_channels)
		return -ENOENT;

	cdma_activate(state, _channel, 1);

	if(((readl(state->regs + CDMA_CSTATUS(_channel)) >> 16) & 3) == 1)
	{
		int i;
		writel(2, state->regs + CDMA_STATUS(_channel));

		for(i = 0; (((readl(state->regs + CDMA_STATUS(_channel)) >> 16) & 3) == 1)
				&& i < 1000; i++)
		{
			udelay(10);
		}

		if(i == 1000)
		{
			dev_err(&state->dev->dev, "failed to cancel transaction\n");
			return -ETIMEDOUT;
		}
	}

	complete(&cstate->completion);
	cdma_aes(_channel, NULL);

	cdma_activate(state, _channel, 0);
	return 0;
}
EXPORT_SYMBOL_GPL(cdma_cancel);

int cdma_wait_timeout(u32 _channel,int timeout)
{
	struct cdma_state *state;
	struct cdma_channel_state *cstate;

	wait_for_completion_timeout(&cdma_completion,timeout);
	state = cdma_state; // TODO: somewhat hacky.
	cstate = &state->channels[_channel];

	if(_channel > state->num_channels)
		return -ENOENT;

	wait_for_completion_timeout(&cstate->completion,timeout);
	return 0;
}
EXPORT_SYMBOL_GPL(cdma_wait_timeout);

int cdma_wait(u32 _channel)
{
	struct cdma_state *state;
	struct cdma_channel_state *cstate;

	wait_for_completion(&cdma_completion);
	state = cdma_state; // TODO: somewhat hacky.
	cstate = &state->channels[_channel];

	if(_channel > state->num_channels)
		return -ENOENT;

	wait_for_completion(&cstate->completion);
	return 0;
}
EXPORT_SYMBOL_GPL(cdma_wait);


int cdma_aes(u32 _channel, struct cdma_aes *_aes)
{
	struct cdma_state *state;
	struct cdma_channel_state *cstate;
	int status;
	u32 cfg;
	u32 type, keytype;

	wait_for_completion(&cdma_completion);
	state = cdma_state; // TODO: somewhat hacky.
	cstate = &state->channels[_channel];

	if(_channel > state->num_channels)
		return -ENOENT;

	if(cstate->aes && !_aes)
	{
		state->aes_bitmap &=~ (1 << cstate->aes_channel);
		cstate->aes_channel = 0;
	}

	cstate->aes = _aes;
	if(!_aes)
		return 0;

	if(!cstate->aes_channel)
	{
		// TODO: lock this?

		int i;
		for(i = 2; i <= 9; i++)
		{
			if(state->aes_bitmap & (1 << i))
				continue;

			state->aes_bitmap |= (1 << i);
			cstate->aes_channel = i;
			break;
		}

		if(i == 8)
		{
			dev_err(&state->dev->dev, "no more AES channels!\n");
			return -ENOENT;
		}
	}

	cfg = AES_ENABLED | (((_channel+1) & 0xFF) << 8);

	if(!_aes->decrypt)
		cfg |= AES_ENCRYPT;

	type = _aes->type;
	keytype = (type >> 28) & 0xF;

	switch(keytype)
	{
	case 2:
		cfg |= AES_256;
		break;

	case 1:
		cfg |= AES_192;
		break;

	case 0:
		cfg |= AES_128;
		break;

	default:
		dev_err(&state->dev->dev, "invalid AES key type %d.\n", keytype);
		return -EINVAL;
	}

	status = cdma_activate(state, _channel, 1);

	switch(type & 0xFFF)
	{
	case CDMA_GID:
		cfg |= AES_GID;
		break;

	case CDMA_UID:
		cfg |= AES_UID;
		break;

	case 0:
		cfg |= AES_KEY;
		switch(keytype)
		{
		case 2: // AES-256
			writel(_aes->key[7], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 7));
			writel(_aes->key[6], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 6));

		case 1: // AES-192
			writel(_aes->key[5], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 5));
			writel(_aes->key[4], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 4));

		default: // AES-128
			writel(_aes->key[3], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 3));
			writel(_aes->key[2], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 2));
			writel(_aes->key[1], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 1));
			writel(_aes->key[0], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 0));
			break;
		}
		break;

	default:
		dev_err(&state->dev->dev, "invalid AES key 0x%012x.\n", type & 0xFFF);
		cdma_activate(state, _channel, status);
		return -EINVAL;
	}

	writel(cfg, state->aes_regs + CDMA_AES_CONFIG(cstate->aes_channel));
	cdma_activate(state, _channel, status);
	return 0;
}
EXPORT_SYMBOL_GPL(cdma_aes);

#define SET_REG(x,y)	writel(y,x)

typedef struct dmaAES_CRYPTO {
	uint32_t *unkn0;
	uint32_t unkn1;
	uint32_t *buffer;
	uint32_t size;
	uint32_t unkn4;
	uint32_t unkn5;
	uint32_t unkn6;
	uint32_t unkn7;
	uint32_t unkn8;
	uint32_t unkn9;
	uint32_t unkn10;
	uint32_t unkn11;
	uint32_t unkn12;
	uint32_t unkn13;
	uint32_t unkn14;
	uint32_t unkn15;
} __attribute__((packed)) dmaAES_CRYPTO;

static dmaAES_CRYPTO *aes_crypto = NULL; // first one is not used. lol.

#define DMA_SETTINGS 0x4
#define DMA_TXRX_REGISTER 0x8
#define DMA_SIZE 0xC
int aes_hw_crypto_operation(uint32_t _arg0, uint32_t _channel, uint32_t *_buffer, uint32_t _arg3, uint32_t _size, uint32_t _arg5, uint32_t _arg6, uint32_t _arg7) {
	uint32_t unkn0;
	uint32_t value = 0;
	uint32_t channel_reg = _channel << 12;

	SET_REG(DMA + channel_reg, 2);

	aes_crypto[_channel].buffer = _buffer;
	aes_crypto[_channel].unkn0 = &aes_crypto[_channel].unkn8;
	aes_crypto[_channel].unkn1 = (_arg7 ? 0x30103 : 0x103);
	aes_crypto[_channel].size = _size;
	aes_crypto[_channel].unkn9 = 0;

	if(_arg0) {
		switch(_arg5)
		{
			case 1:
				value |= (0 << 2);
				break;
			case 2:
				value |= (1 << 2);
				break;
			case 4:
				value |= (2 << 2);
				break;
			default:
				return -1;
		}

		switch(_arg6)
		{
			case 1:
				value |= (0 << 4);
				break;
			case 2:
				value |= (1 << 4);
				break;
			case 4:
				value |= (2 << 4);
				break;
			case 8:
				value |= (3 << 4);
				break;
			case 16:
				value |= (4 << 4);
				break;
			case 32:
				value |= (5 << 4);
				break;
			default:
				return -1;
		}
		if(_arg0 == 1)
			value |= 2;
		unkn0 = 0;
		SET_REG(DMA + channel_reg + DMA_SETTINGS, value);
		SET_REG(DMA + channel_reg + DMA_TXRX_REGISTER, _arg3);
	} else {
		unkn0 = 128;
	}
	//DataCacheOperation(1, (uint32_t)&aes_crypto[_channel], 0x20);
	SET_REG(DMA + channel_reg + DMA_SIZE, _size);
	SET_REG(DMA + channel_reg + 0x14, (uint32_t)&aes_crypto[_channel]);
	SET_REG(DMA + channel_reg, (unkn0 | (_arg7 << 8) | 0x1C0000));
	return 0;
}

//todo
int aes_hw_crypto_cmd(uint32_t _encrypt, uint32_t *_inBuf, uint32_t *_outBuf, uint32_t _size, uint32_t _type, uint32_t* _key, uint32_t *_iv) {


	struct cdma_state *state;
		struct cdma_channel_state *cstate;
		u32 type, keytype;
		wait_for_completion(&cdma_completion);
		state = cdma_state; // TODO: somewhat hacky.

		int value = 0;

	//clock_gate_switch(0x14, ON);
	cdma_activate(state, 1, 1);
	cdma_activate(state, 2, 1);

	uint32_t channel = 1;
	uint32_t channel_reg = channel << 12;
	value = (channel & 0xFF) << 8;

	if(_size & 0xF)
		panic("aes_hw_crypto_cmd: bad arguments\r\n");

	if(_encrypt & 0xF0) {
		if((_encrypt & 0xF0) != 0x10)
			panic("aes_hw_crypto_cmd: bad arguments\r\n");
		value |= (1 << 17);
	}

	if(_encrypt & 0xF) {
		if((_encrypt & 0xF) != 1)
			panic("aes_hw_crypto_cmd: bad arguments\r\n");
	} else {
		value |= (1 << 16);
	}

	if(_iv) {
		writel(_iv[0], 0x87801010);
		writel(_iv[1], 0x87801014);
		writel(_iv[2], 0x87801018);
		writel(_iv[3], 0x8780101C);
	} else {
		writel(0, 0x87801010);
		writel(0, 0x87801014);
		writel(0, 0x87801018);
		writel(0, 0x8780101C);
	}


	keytype = (type >> 28) & 0xF;

	uint32_t key_shift = 0;
	uint32_t key_set = 0;
	if ((_type & 0xFFF) == 0) {
		switch(keytype)
		{
			case 2:	// AES 256
				value |= (2 << 18);
				break;

			case 1:	// AES 192
				value |= (1 << 18);
				break;

			case 0:	// AES 128
				value |= (0 << 18);
				break;

			default:// Fail
				panic("aes_hw_crypto_cmd: bad arguments\r\n");
		}
		switch(keytype)
				{
				case 2: // AES-256
					writel(_key[7], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 7));
					writel(_key[6], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 6));

				case 1: // AES-192
					writel(_key[5], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 5));
					writel(_key[4], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 4));

				default: // AES-128
					writel(_key[3], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 3));
					writel(_key[2], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 2));
					writel(_key[1], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 1));
					writel(_key[0], state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 0));
					break;
				}
		key_set = 1;
	} else {
		if ((_type & 0xFFF) == 512) {
			key_shift = 1; // still broken
		} else if ((_type & 0xFFF) == 513) {
			key_shift = 0;
		} else {
			key_shift = 2; // wrong I guess but never used
		}
		// Key deactivated?
		if(readl(DMA + DMA_AES) & (1 << key_shift))
			return -1;
	}

	if(key_shift)
		value |= 1 << 19;

	value |= (key_set << 20) | (key_shift << 21);
	writel(value, state->aes_regs + CDMA_AES(cstate->aes_channel));

	if(aes_hw_crypto_operation(0, 1, _inBuf, 0, _size, 0, 0, 1) || aes_hw_crypto_operation(0, 2, _outBuf, 0, _size, 0, 0, 0))
		panic("aes_hw_crypto_cmd: bad arguments\r\n");

	//DataCacheOperation(1, (uint32_t)_inBuf, _size);
	//DataCacheOperation(3, (uint32_t)_outBuf, _size);
	writel(readl(DMA + (1 << 12)) | 1, DMA + (1 << 12));
	writel(readl(DMA + (2 << 12)) | 1, DMA + (2 << 12));

	cdma_wait(1);
	cdma_wait(2);

	if(key_set)
	{
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 0));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 1));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 2));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 3));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 4));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 5));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 6));
		writel(0, state->aes_regs + CDMA_AES_KEY(cstate->aes_channel, 7));
	}

	cdma_activate(state, 1, 0);
	cdma_activate(state, 2, 0);

	return 0;
}

int aes_crypto_cmd(uint32_t _encrypt, void *_inBuf, void *_outBuf, uint32_t _size, uint32_t _type, void *_key, void *_iv) {
	if(_size & 0xF || (!(_type & 0xFFF) && (_key == NULL)) || (_encrypt & 0xF) > 1) {
		printk("aes_crypto_cmd: bad arguments\r\n");
		return -1;
	}

	aes_crypto = kmalloc(sizeof(dmaAES_CRYPTO) * 3, GFP_DMA32);
	if (!aes_crypto) {
		printk("aes_crypto_cmd: out of memory\r\n");
		return -1;
	}

	if(aes_hw_crypto_cmd(_encrypt, (uint32_t*)_inBuf, (uint32_t*)_outBuf, _size, _type, (uint32_t*)_key, (uint32_t*)_iv)) {
		kfree(aes_crypto);
		return -1;
	}

	kfree(aes_crypto);
	return 0;
}
EXPORT_SYMBOL_GPL(aes_crypto_cmd);

static irqreturn_t cdma_irq_handler(int _irq, void *_token)
{
	struct cdma_state *state = _token;
	int chan = _irq - state->irq;
	struct cdma_channel_state *cstate = &state->channels[chan];
	u32 sz;
	int res = 0;

	u32 status = readl(state->regs + CDMA_CSTATUS(chan));

	cdma_dbg(state, "%s!\n", __func__);

	if(status & CSTATUS_INTERR)
	{
		dev_err(&state->dev->dev, "channel %d: error interrupt.\n", chan);
		res = -EIO;
	}

	if(status & CSTATUS_SPURCIR)
		dev_err(&state->dev->dev, "channel %d: spurious CIR.\n", chan);

	writel(CSTATUS_INTCLR, state->regs + CDMA_CSTATUS(chan));

	sz = readl(state->regs + CDMA_CSIZE(chan));
	if(!res && (cstate->count < cstate->done || sz))
	{
		if(status & CSTATUS_TXRDY)
			panic("TODO: %s, incomplete transfers.\n", __func__);
		
		cdma_continue(state, chan);
	}
	else
	{
		complete(&cstate->completion);
		cdma_activate(state, chan, 0);
		cdma_free_segments(state, cstate);
	}

	return IRQ_HANDLED;
}

static int cdma_probe(struct platform_device *_dev)
{
	struct cdma_state *state;
	struct resource *res;
	int i, ret = 0;

	if(cdma_state)
	{
		dev_err(&_dev->dev, "CDMA controller already registered.\n");
		return -EINVAL;
	}

	state = kzalloc(sizeof(struct cdma_state), GFP_KERNEL);
	if(!state)
	{
		dev_err(&_dev->dev, "failed to allocate state.\n");
		return -ENOMEM;
	}

	res = platform_get_resource(_dev, IORESOURCE_MEM, 0);
	if(!res)
	{
		dev_err(&_dev->dev, "failed to get register block.\n");
		ret = -EINVAL;
		goto err_state;
	}

	state->regs = ioremap(res->start, resource_size(res));
	if(!state->regs)
	{
		dev_err(&_dev->dev, "failed to remap register block.\n");
		ret = -EIO;
		goto err_state;
	}
	
	res = platform_get_resource(_dev, IORESOURCE_MEM, 1);
	if(!res)
	{
		dev_err(&_dev->dev, "failed to get AES block.\n");
		ret = -EINVAL;
		goto err_regs;
	}

	state->aes_regs = ioremap(res->start, resource_size(res));
	if(!state->aes_regs)
	{
		dev_err(&_dev->dev, "failed to map AES block.\n");
		ret = -EINVAL;
		goto err_regs;
	}

	res = platform_get_resource(_dev, IORESOURCE_IRQ, 0);
	if(!res)
	{
		dev_err(&_dev->dev, "failed to get irq base.\n");
		ret = -EINVAL;
		goto err_aes;
	}

	state->clk = clk_get(&_dev->dev, "cdma");
	if(state->clk && !IS_ERR(state->clk))
	{
		clk_enable(state->clk);
		dev_info(&_dev->dev, "enabling clock.\n");
	}

	state->dev = _dev;
	state->irq = res->start;
	state->num_channels = resource_size(res);
	
	for(i = res->start; i <= res->end; i++)
	{
		ret = request_irq(i, cdma_irq_handler, IRQF_SHARED, "apple-cdma", state);
		if(ret < 0)
		{
			dev_err(&_dev->dev, "failed to request irq %d.\n", i);
			goto err_aes;
		}
	}

	state->pool = dma_pool_create("apple-cdma", &_dev->dev, sizeof(cdma_segment_t),
			sizeof(cdma_segment_t),	0);
	if(!state->pool)
	{
		dev_err(&_dev->dev, "failed to create DMA pool.\n");
		ret = -ENOMEM;
		goto err_irqs;
	}

	for(i = 0; i < state->num_channels; i++)
	{
		struct cdma_channel_state *cstate = &state->channels[i];
		init_completion(&cstate->completion);
		INIT_LIST_HEAD(&cstate->segments);
	}

	dev_info(&state->dev->dev, "driver started.\n");
	cdma_state = state;
	complete_all(&cdma_completion);
	platform_set_drvdata(_dev, state);
	goto exit;

err_irqs:
	for(i = res->start; i <= res->end; i++)
		free_irq(i, state);

err_aes:
	iounmap(state->aes_regs);

err_regs:
	iounmap(state->regs);

err_state:
	kfree(state);

exit:
	return ret;
}

static int cdma_remove(struct platform_device *_dev)
{
	struct cdma_state *state = platform_get_drvdata(_dev);
	if(!state)
		return 0;

	if(state->pool)
		dma_pool_destroy(state->pool);

	if(state->irq)
	{
		int i;
		for(i = 0; i < state->num_channels; i++)
			free_irq(state->irq + i, state);
	}

	if(state->regs)
		iounmap(state->regs);

	if(state->aes_regs)
		iounmap(state->aes_regs);

	if(state->clk && !IS_ERR(state->clk))
	{
		clk_disable(state->clk);
		clk_put(state->clk);
	}

	kfree(state);
	return 0;
}

#ifdef CONFIG_PM
static int cdma_suspend(struct platform_device *_dev, pm_message_t _state)
{
	return 0;
}

static int cdma_resume(struct platform_device *_dev)
{
	return 0;
}
#else
#define cdma_suspend	NULL
#define cdma_resume		NULL
#endif

static struct platform_driver cdma_driver = {
	.driver = {
		.name = "apple-cdma",
	},

	.probe = cdma_probe,
	.remove = cdma_remove,
	.suspend = cdma_suspend,
	.resume = cdma_resume,
};

static int __init cdma_init(void)
{
	return platform_driver_register(&cdma_driver);
}
arch_initcall(cdma_init);

static void __exit cdma_exit(void)
{
	platform_driver_unregister(&cdma_driver);
}
module_exit(cdma_exit);

MODULE_DESCRIPTION("Apple CDMA");
MODULE_AUTHOR("Richard Ian Taylor");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:apple-cdma");

