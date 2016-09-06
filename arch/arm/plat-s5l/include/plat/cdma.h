/**
 * Copyright (c) 2011 Richard Ian Taylor.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef  __S5L_CDMA__
#define  __S5L_CDMA__

#include <linux/scatterlist.h>

#define CDMA_AES_128	(0 << 28)
#define CDMA_AES_192	(1 << 28)
#define CDMA_AES_256	(2 << 28)

#define CDMA_GID		(512)
#define CDMA_UID		(513)

typedef enum
{
	CDMA_TO_MEM = 0,
	CDMA_FROM_MEM = 1,
} cdma_dir_t;

struct cdma_aes
{
	unsigned decrypt: 1;
	u32 type;
	const u32 *key;
	size_t data_size;

	void (*gen_iv)(void *_param, u32 _seg, u32 *_iv);
	void *iv_param;
};

int cdma_begin(u32 _channel, cdma_dir_t _dir, struct scatterlist *_sg, size_t _sg_count, size_t _size, dma_addr_t _reg, size_t _burst, size_t _busw, u32 _pid);
int cdma_cancel(u32 _channel);

int cdma_wait(u32 _channel);
int cdma_wait_timeout(u32 _channel,int timeout);

int cdma_aes(u32 _channel, struct cdma_aes *_aes);
int aes_crypto_cmd(uint32_t _encrypt, void *_inBuf, void *_outBuf, uint32_t _size, uint32_t _type, void *_key, void *_iv);

#endif //__S5L_CDMA__

