/**
 * Copyright (c) 2011 Richard Ian Taylor.
 * Copyright (c) 2016 Erfan Abdi.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/apple_flash.h>
#include <linux/slab.h>
#include <linux/delay.h>

typedef int error_t;
#define FAILED(x) ((x) < 0)
#define SUCCEEDED(x) ((x) >= 0)
#define SUCCESS 0
#define ERROR(x) (-(x))
#define CEIL_DIVIDE(x, y) (((x)+(y)-1)/(y))

typedef struct _vfl_vsvfl_geometry
{
	uint16_t pages_per_block;
	uint16_t pages_per_block_2;
	uint16_t pages_per_sublk;
	uint32_t pages_total;
	uint32_t some_page_mask;
	uint32_t some_sublk_mask;
	uint16_t blocks_per_ce;
	uint16_t bytes_per_page;
	uint32_t num_ecc_bytes;
	uint32_t bytes_per_spare;
	uint32_t one;
	uint16_t num_ce;
	uint16_t ecc_bits;
	uint16_t reserved_blocks;
	uint16_t vfl_blocks;
	uint16_t some_crazy_val;
	uint16_t fs_start_block;
	uint32_t unk;
	uint32_t banks_per_ce;
	uint16_t banks_total;
	uint16_t bank_address_space;
	uint32_t blocks_per_bank;
	uint32_t blocks_per_bank_vfl;
} vfl_vsvfl_geometry_t;

typedef struct VSVFLStats {
	uint64_t counter0;
	uint64_t counter1;
	uint64_t counter2;
	uint64_t counter3;
	uint64_t counter4;
	uint64_t counter5;
	uint64_t counter6;
	uint64_t counter7;
	uint64_t counter8;
	uint64_t counter9;
	uint64_t counter10;
	uint64_t counter11;
} __attribute__((packed)) VSVFLStats;

// VSVFL conversion functions prototypes
typedef void (*vsvfl_virtual_to_physical_t)(struct apple_vfl *_vfl, uint32_t _vBank, uint32_t _vPage, uint32_t *_pCE, uint32_t *_pPage);

typedef void (*vsvfl_physical_to_virtual_t)(struct apple_vfl *_vfl, uint32_t, uint32_t, uint32_t *, uint32_t *);

struct VSVFL
{
	int defaultVendor;
	uint32_t current_version;
	struct _vsvfl_context *contexts;
	vfl_vsvfl_geometry_t geometry;
	uint8_t *bbt[16];

	uint32_t *pageBuffer;
	uint16_t *chipBuffer;
	uint16_t *blockBuffer;

	vsvfl_virtual_to_physical_t virtual_to_physical;
	vsvfl_physical_to_virtual_t physical_to_virtual;

	VSVFLStats *stats;
};

#define get_vsvfl(x) ((struct VSVFL*)((x)->private))

typedef struct _vsvfl_context
{
	uint32_t usn_inc; // 0x000
	uint32_t usn_dec; // 0x004
	uint32_t ftl_type; // 0x008
	uint16_t usn_block; // 0x00C // current block idx
	uint16_t usn_page; // 0x00E // used pages
	uint16_t active_context_block; // 0x010
	uint16_t write_failure_count; // 0x012
	uint16_t bad_block_count; // 0x014
	uint8_t replaced_block_count[4]; // 0x016
	uint16_t num_reserved_blocks; // 0x01A
	uint16_t field_1C; // 0x01C
	uint16_t total_reserved_blocks; // 0x01E
	uint8_t field_20[6]; // 0x020
	uint16_t reserved_block_pool_map[820]; // 0x026
	uint16_t vfl_context_block[4]; // 0x68E
	uint16_t usable_blocks_per_bank; // 0x696
	uint16_t reserved_block_pool_start; // 0x698
	uint16_t control_block[3]; // 0x69A
	uint16_t scrub_list_length; // 0x6A0
	uint16_t scrub_list[20]; // 0x6A2
	uint32_t field_6CA[4]; // 0x6CA
	uint32_t vendor_type; // 0x6DA
	uint8_t field_6DE[204]; // 0x6DE
	uint16_t remapping_schedule_start; // 0x7AA
	uint8_t unk3[0x48];				// 0x7AC
	uint32_t version;				// 0x7F4
	uint32_t checksum1;				// 0x7F8
	uint32_t checksum2;				// 0x7FC
} __attribute__((packed)) vsvfl_context_t;

typedef struct _vfl_vsvfl_spare_data
{
	union
	{
		struct
		{
			uint32_t logicalPageNumber;
			uint32_t usn;
		} __attribute__ ((packed)) user;

		struct
		{
			uint32_t usnDec;
			uint16_t idx;
			uint8_t field_6;
			uint8_t field_7;
		} __attribute__ ((packed)) meta;
	};

	uint8_t type2;
	uint8_t type1;
	uint8_t eccMark;
	uint8_t field_B;
} __attribute__ ((packed)) vfl_vsvfl_spare_data_t;

static void virtual_to_physical_10001(struct apple_vfl *_vfl, uint32_t _vBank, uint32_t _vPage, uint32_t *_pCE, uint32_t *_pPage)
{
	*_pCE = _vBank;
	*_pPage = _vPage;
}

static void physical_to_virtual_10001(struct apple_vfl *_vfl, uint32_t _pCE, uint32_t _pPage, uint32_t *_vBank, uint32_t *_vPage)
{
	*_vBank = _pCE;
	*_vPage = _pPage;
}

static void virtual_to_physical_100014(struct apple_vfl *_vfl, uint32_t _vBank, uint32_t _vPage, uint32_t *_pCE, uint32_t *_pPage)
{
	uint32_t pBank, pPage;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	pBank = _vBank / vfl->geometry.num_ce;
	pPage = ((vfl->geometry.pages_per_block - 1) & _vPage) | (2 * (~(vfl->geometry.pages_per_block - 1) & _vPage));
	if (pBank & 1)
		pPage |= vfl->geometry.pages_per_block;

	*_pCE = _vBank % vfl->geometry.num_ce;
	*_pPage = pPage;
}

static void physical_to_virtual_100014(struct apple_vfl *_vfl, uint32_t _pCE, uint32_t _pPage, uint32_t *_vBank, uint32_t *_vPage)
{
	uint32_t vBank, vPage;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	vBank = vfl->geometry.pages_per_block & _pPage;
	vPage = ((vfl->geometry.pages_per_block - 1) & _pPage) | (((vfl->geometry.pages_per_block * -2) & _pPage) / 2);
	if(vBank)
		vBank = vfl->geometry.num_ce;

	*_vBank = _pCE + vBank;
	*_vPage = vPage;
}

static void virtual_to_physical_150011(struct apple_vfl *_vfl, uint32_t _vBank, uint32_t _vPage, uint32_t *_pCE, uint32_t *_pPage)
{
	uint32_t pBlock;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	pBlock = 2 * (_vPage / vfl->geometry.pages_per_block);
	if(_vBank % (2 * vfl->geometry.num_ce) >= vfl->geometry.num_ce)
		pBlock++;

	*_pCE = _vBank % vfl->geometry.num_ce;
	*_pPage = (vfl->geometry.pages_per_block * pBlock) | (_vPage % 128);
}

static void physical_to_virtual_150011(struct apple_vfl *_vfl, uint32_t _pCE, uint32_t _pPage, uint32_t *_vBank, uint32_t *_vPage)
{
	uint32_t pBlock;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	*_vBank = _pCE;
	pBlock = _pPage / vfl->geometry.pages_per_block;
	if(pBlock % 2) {
		pBlock--;
		*_vBank = vfl->geometry.num_ce + _pCE;
	}
	*_vPage = (vfl->geometry.pages_per_block * (pBlock / 2)) | (_pPage % 128);
}

static error_t virtual_block_to_physical_block(struct apple_vfl *_vfl, uint32_t _vBank, uint32_t _vBlock, uint32_t *_pBlock)
{
	uint32_t pCE, pPage;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	if(!vfl->virtual_to_physical) {
		printk("vsvfl: virtual_to_physical hasn't been initialized yet!\r\n");
		return -EINVAL;
	}

	vfl->virtual_to_physical(_vfl, _vBank, vfl->geometry.pages_per_block * _vBlock, &pCE, &pPage);
	*_pBlock = pPage / vfl->geometry.pages_per_block;

	return SUCCESS;
}

static error_t physical_block_to_virtual_block(struct apple_vfl *_vfl, uint32_t _pBlock, uint32_t *_vBank, uint32_t *_vBlock)
{
	uint32_t vBank, vPage;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	if(!vfl->physical_to_virtual) {
		printk("vsvfl: physical_to_virtual hasn't been initialized yet!\r\n");
		return -EINVAL;
	}

	vfl->physical_to_virtual(_vfl, 0, vfl->geometry.pages_per_block * _pBlock, &vBank, &vPage);
	*_vBank = vBank / vfl->geometry.num_ce;
	*_vBlock = vPage / vfl->geometry.pages_per_block;

	return SUCCESS;
}


static int vfl_is_good_block(uint8_t* badBlockTable, uint32_t block) {
	return (badBlockTable[block / 8] & (1 << (block % 8))) != 0;
}

static uint32_t remap_block(struct apple_vfl *_vfl, uint32_t _ce, uint32_t _block, uint32_t *_isGood) {
	struct VSVFL *vfl = get_vsvfl(_vfl);
	int pwDesPbn;

	printk(KERN_DEBUG "vsvfl: remap_block: CE %d, block %d\r\n", _ce, _block);

	if(vfl_is_good_block(vfl->bbt[_ce], _block))
		return _block;

	printk(KERN_DEBUG "vsvfl: remapping block...\r\n");

	if(_isGood)
		_isGood = 0;

	for(pwDesPbn = 0; pwDesPbn < vfl->geometry.blocks_per_ce - vfl->contexts[_ce].reserved_block_pool_start * vfl->geometry.banks_per_ce; pwDesPbn++)
	{
		if(vfl->contexts[_ce].reserved_block_pool_map[pwDesPbn] == _block)
		{
			uint32_t vBank, vBlock, pBlock;

			/*
			if(pwDesPbn >= _vfl->geometry.blocks_per_ce)
				printk("ftl: Destination physical block for remapping is greater than number of blocks per CE!");
			*/

			vBank = _ce + vfl->geometry.num_ce * (pwDesPbn / (vfl->geometry.blocks_per_bank_vfl - vfl->contexts[_ce].reserved_block_pool_start));
			vBlock = vfl->contexts[_ce].reserved_block_pool_start + (pwDesPbn % (vfl->geometry.blocks_per_bank_vfl - vfl->contexts[_ce].reserved_block_pool_start));

			if(FAILED(virtual_block_to_physical_block(_vfl, vBank, vBlock, &pBlock)))
				panic("vfl: failed to convert virtual reserved block to physical\r\n");

			return pBlock;
		}
	}

	printk("vfl: failed to remap CE %d block 0x%04x\r\n", _ce, _block);
	return _block;
}

static error_t virtual_page_number_to_physical(struct apple_vfl *_vfl, uint32_t _vpNum, uint32_t* _ce, uint32_t* _page) {
	uint32_t ce, vBank, ret, bank_offset, pBlock;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	vBank = _vpNum % vfl->geometry.banks_total;
	ce = vBank % vfl->geometry.num_ce;

	ret = virtual_block_to_physical_block(_vfl, vBank, _vpNum / vfl->geometry.pages_per_sublk, &pBlock);

	if(FAILED(ret))
		return ret;

	pBlock = remap_block(_vfl, ce, pBlock, 0);

	bank_offset = vfl->geometry.bank_address_space * (pBlock / vfl->geometry.blocks_per_bank);

	*_ce = ce;
	//printk("vfl_vsvfl_read_single_page: pBlock:%x pages_per_block_2:%x!\r\n", pBlock,vfl->geometry.pages_per_block_2 );

	*_page = vfl->geometry.pages_per_block_2 * (bank_offset + (pBlock % vfl->geometry.blocks_per_bank))
			+ ((_vpNum % vfl->geometry.pages_per_sublk) / vfl->geometry.banks_total);

	return SUCCESS;
}

static void vfl_checksum(void* data, int size, uint32_t* a, uint32_t* b)
{
	int i;
	uint32_t* buffer = (uint32_t*) data;
	uint32_t x = 0;
	uint32_t y = 0;
	for(i = 0; i < (size / 4); i++) {
		x += buffer[i];
		y ^= buffer[i];
	}

	*a = x + 0xAABBCCDD;
	*b = y ^ 0xAABBCCDD;
}

static int vfl_gen_checksum(struct apple_vfl *_vfl, int ce)
{
	struct VSVFL *vfl = get_vsvfl(_vfl);
	vfl_checksum(&vfl->contexts[ce],
				 (uint32_t)&vfl->contexts[ce].checksum1 - (uint32_t)&vfl->contexts[ce],
				 &vfl->contexts[ce].checksum1, &vfl->contexts[ce].checksum2);
	return 1;
}

static int vfl_check_checksum(struct apple_vfl *_vfl, int ce)
{
	static int counter = 0;
	struct VSVFL *vfl = get_vsvfl(_vfl);
	uint32_t checksum1;
	uint32_t checksum2;

	counter++;

	vfl_checksum(&vfl->contexts[ce],
				 (uint32_t)&vfl->contexts[ce].checksum1 - (uint32_t)&vfl->contexts[ce],
				 &checksum1, &checksum2);

	// Yeah, this looks fail, but this is actually the logic they use
	if(checksum1 == vfl->contexts[ce].checksum1)
		return 1;

	if(checksum2 != vfl->contexts[ce].checksum2)
		return 1;

	return 0;
}

static error_t vsvfl_store_vfl_cxt(struct apple_vfl *_vfl, uint32_t _ce);
static int is_block_in_scrub_list(struct apple_vfl *_vfl, uint32_t _ce, uint32_t _block) {
	uint32_t i;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	for (i = 0; i < vfl->contexts[_ce].scrub_list_length; i++) {
		if (vfl->contexts[_ce].scrub_list[i] == _block)
			return 1;
	}

	return 0;
}

static int add_block_to_scrub_list(struct apple_vfl *_vfl, uint32_t _ce, uint32_t _block) {
	struct VSVFL *vfl = get_vsvfl(_vfl);

	if(is_block_in_scrub_list(_vfl, _ce, _block))
			return 0;

	if(vfl->contexts[_ce].scrub_list_length > 0x13) {
		printk("vfl: too many scrubs!\r\n");
		return 0;
	}

	if(!vfl_check_checksum(_vfl, _ce))
		panic("vfl_add_block_to_scrub_list: failed checksum\r\n");

	vfl->contexts[_ce].scrub_list[vfl->contexts[_ce].scrub_list_length++] = _block;
	vfl_gen_checksum(_vfl, _ce);
	return vsvfl_store_vfl_cxt(_vfl, _ce);
}

static error_t vfl_vsvfl_write_single_page(struct apple_vfl *_vfl, uint32_t dwVpn, uint8_t* buffer, uint8_t* spare, int _scrub)
{
	struct VSVFL *vfl = get_vsvfl(_vfl);

	uint32_t pCE = 0, pPage = 0;
	error_t ret;

	ret = virtual_page_number_to_physical(_vfl, dwVpn, &pCE, &pPage);
	if(FAILED(ret))
	{
		printk("vfl_vsvfl_write_single_page: virtual_page_number_to_physical returned an error (dwVpn %d)!\r\n", dwVpn);
		return ret;
	}

	// TODO: use SGs?
	ret = apple_vfl_write_nand_page(_vfl, pCE, pPage, buffer, spare);

	if(ret < 0)
	{
		if(!vfl_check_checksum(_vfl, pCE))
			panic("vfl_vsfl_write_single_page: failed checksum\r\n");

		vfl->contexts[pCE].write_failure_count++;
		vfl_gen_checksum(_vfl, pCE);

		// TODO: add block map support
		// vsvfl_mark_page_as_bad(pCE, pPage, ret);

		if(_scrub)
			add_block_to_scrub_list(_vfl, pCE, pPage / vfl->geometry.pages_per_block); // Something like that, I think

		return ret;
	}

	return 0;
}

static error_t vfl_vsvfl_read_single_page(struct apple_vfl *_vfl, uint32_t dwVpn, uint8_t* buffer, uint8_t* spare, int empty_ok, int* refresh_page)
{
	struct VSVFL *vfl = _vfl->private;
	uint32_t pCE = 0, pPage = 0;
	int ret;

	if(refresh_page)
		*refresh_page = 0;

	//VFLData1.field_8++;
	//VFLData1.field_20++;

	ret = virtual_page_number_to_physical(_vfl, dwVpn, &pCE, &pPage);

	if(FAILED(ret)) {
		printk("vfl_vsvfl_read_single_page: virtual_page_number_to_physical returned an error (dwVpn %d)!\r\n", dwVpn);
		return ret;
	}

	// Hack to get reading by absolute page number.
	u8* spareBuffer = kmalloc(vfl->geometry.bytes_per_spare, GFP_KERNEL);

	ret = apple_vfl_read_nand_page(_vfl, pCE, pPage, buffer, spareBuffer);

	if(spare){
		spare[0] = 0;
			memcpy(spare, spareBuffer, _vfl->get(_vfl, NAND_OOB_ALLOC));


	}

	if(!empty_ok && ret == -ENOENT)
		ret = -EIO;
	else if(empty_ok && ret == -ENOENT)
		return 1;

	if(ret == -EINVAL || ret == -EIO)
	{
		ret = apple_vfl_read_nand_page(_vfl, pCE, pPage, buffer, spare);
		if(!empty_ok && ret == -ENOENT)
			return -EIO;

		if(ret == -EINVAL || ret == -EIO)
			return ret;
	}

	if(ret == -ENOENT && spare)
		memset(spare, 0xFF, vfl->geometry.bytes_per_spare);

	return ret;
}

static error_t vsvfl_write_vfl_cxt_to_flash(struct apple_vfl *_vfl, uint32_t _ce) {
	uint8_t* pageBuffer;
	uint8_t* spareBuffer;
	struct VSVFL *vfl = _vfl->private;
	vsvfl_context_t *curVFLCxt = &vfl->contexts[_ce];
	uint32_t curPage = curVFLCxt->usn_page;
	int i;
	int fails = 0;

	if(_ce >= vfl->geometry.num_ce)
		return -EINVAL;

	if(!vfl_check_checksum(_vfl, _ce))
		panic("vsvfl_write_vfl_cxt_to_flash: failed checksum\r\n");

	// TODO: do these need to aligned. -- Ricky26
	pageBuffer = kmalloc(vfl->geometry.bytes_per_page, GFP_KERNEL);
	spareBuffer = kmalloc(vfl->geometry.bytes_per_spare, GFP_KERNEL);
	if(pageBuffer == NULL || spareBuffer == NULL) {
		printk("vfl: cannot allocate page and spare buffer\r\n");
		return -ENOMEM;
	}
	memset(pageBuffer, 0x0, vfl->geometry.bytes_per_page);

	curVFLCxt->usn_inc = vfl->current_version++;
	curVFLCxt->usn_page += 8;
	curVFLCxt->usn_dec -= 1;
	vfl_gen_checksum(_vfl, _ce);

	memcpy(pageBuffer, curVFLCxt, 0x800);
	for (i = 0; i < 8; i++) {
		uint32_t bankStart = (curVFLCxt->vfl_context_block[curVFLCxt->usn_block] / vfl->geometry.blocks_per_bank) * vfl->geometry.bank_address_space;
		uint32_t blockOffset = curVFLCxt->vfl_context_block[curVFLCxt->usn_block] % vfl->geometry.blocks_per_bank;
		int status = apple_vfl_write_nand_page(_vfl, _ce, (bankStart + blockOffset) * vfl->geometry.pages_per_block_2 + curPage + i, pageBuffer, spareBuffer);

		memset(spareBuffer, 0xFF, vfl->geometry.bytes_per_spare);
		((uint32_t*)spareBuffer)[0] = curVFLCxt->usn_dec;
		spareBuffer[8] = 0;
		spareBuffer[9] = 0x80;

		if(FAILED(status)) {
			printk("vfl_write_vfl_cxt_to_flash: Failed write\r\n");
			kfree(pageBuffer);
			kfree(spareBuffer);
			// vsvfl_mark_page_as_bad(_ce, (bankStart + blockOffset) * _vfl->geometry.pages_per_block_2 + curPage + i, status);
			return -EIO;
		}
	}

	for (i = 0; i < 8; i++) {
		uint32_t bankStart = (curVFLCxt->vfl_context_block[curVFLCxt->usn_block] / vfl->geometry.blocks_per_bank) * vfl->geometry.bank_address_space;
		uint32_t blockOffset = curVFLCxt->vfl_context_block[curVFLCxt->usn_block] % vfl->geometry.blocks_per_bank;
		if(FAILED(apple_vfl_read_nand_page(_vfl, _ce, (bankStart + blockOffset) * vfl->geometry.pages_per_block_2 + curPage + i, pageBuffer, spareBuffer))) {
			//vsvfl_store_block_map_single_page(_ce, (bankStart + blockOffset) * _vfl->geometry.pages_per_block_2 + curPage + i);
			fails++;
			continue;
		}
		if(memcmp(pageBuffer, curVFLCxt, 0x6E0) || ((uint32_t*)spareBuffer)[0] != curVFLCxt->usn_dec || spareBuffer[8] || spareBuffer[9] != 0x80)
			fails++;
	}
	kfree(pageBuffer);
	kfree(spareBuffer);
	if(fails > 3)
		return -EIO;
	else
		return SUCCESS;
}

static error_t vfl_vsvfl_write_context(struct apple_vfl *_vfl, uint16_t *_control_block)
{
	struct VSVFL *vfl = _vfl->private;
	uint32_t ce = vfl->current_version % vfl->geometry.num_ce;
	uint32_t i;

	// check and update cxt of each CE
	for(i = 0; i < vfl->geometry.num_ce; i++) {
		if(!vfl_check_checksum(_vfl, i))
			panic("vsvfl: VFLCxt has bad checksum.\r\n");
		memmove(vfl->contexts[i].control_block, _control_block, 6);
		vfl_gen_checksum(_vfl, i);
	}

	// write cxt on the ce with the oldest cxt
	if(FAILED(vsvfl_store_vfl_cxt(_vfl, ce))) {
		printk("vsvfl: context write fail!\r\n");
		return -EIO;
	}

	return 0;
}

static error_t vsvfl_store_vfl_cxt(struct apple_vfl *_vfl, uint32_t _ce) {
	struct VSVFL *vfl = _vfl->private;
	vsvfl_context_t *curVFLCxt = &vfl->contexts[_ce];
	int result;

	if(_ce >= vfl->geometry.num_ce)
		panic("vfl: Can't store VFLCxt on non-existent CE\r\n");

	if(curVFLCxt->usn_page + 8 > vfl->geometry.pages_per_block || FAILED(vsvfl_write_vfl_cxt_to_flash(_vfl, _ce))) {
		int startBlock = curVFLCxt->usn_block;
		int nextBlock = (curVFLCxt->usn_block + 1) % 4;
		while(startBlock != nextBlock) {
			if(curVFLCxt->vfl_context_block[nextBlock] != 0xFFFF) {
				int fail = 0;
				int i;
				for (i = 0; i < 4; i++) {
					uint32_t bankStart = (curVFLCxt->vfl_context_block[nextBlock] / vfl->geometry.blocks_per_bank) * vfl->geometry.bank_address_space;
					uint32_t blockOffset = curVFLCxt->vfl_context_block[nextBlock] % vfl->geometry.blocks_per_bank;
					int status = _vfl->erase_single_block(_vfl, _ce, bankStart + blockOffset);
					if(SUCCEEDED(status))
						break;
					//vsvfl_mark_bad_vfl_block(_vfl, _ce, curVFLCxt->vfl_context_block[nextBlock], status);
					if(i == 3)
						fail = 1;
				}
				if(!fail) {
					if(!vfl_check_checksum(_vfl, _ce))
						panic("vsvfl_store_vfl_cxt: failed checksum\r\n");
					curVFLCxt->usn_block = nextBlock;
					curVFLCxt->usn_page = 0;
					vfl_gen_checksum(_vfl, _ce);
					result = vsvfl_write_vfl_cxt_to_flash(_vfl, _ce);
					if(SUCCEEDED(result))
						return result;
				}
			}
			nextBlock = (nextBlock + 1) % 4;
		}
		return -EIO;
	}
	return SUCCESS;
}

static error_t vsvfl_replace_bad_block(struct apple_vfl *_vfl, uint32_t _ce, uint32_t _block) {
	struct VSVFL *vfl = _vfl->private;
	vsvfl_context_t *curVFLCxt = &vfl->contexts[_ce];
	uint32_t vBank = 0, vBlock;
	uint16_t drbc[16]; // dynamic replaced block count

	int i;
	uint32_t reserved_blocks = vfl->geometry.blocks_per_ce - (curVFLCxt->reserved_block_pool_start * vfl->geometry.banks_per_ce);

	vfl->bbt[_ce][_block >> 3] &= ~(1 << (_block & 7));

	for (i = 0; i < reserved_blocks; i++) {
		uint32_t reserved_blocks_per_bank = vfl->geometry.blocks_per_bank - curVFLCxt->reserved_block_pool_start;
		uint32_t bank = _ce + vfl->geometry.num_ce * (i / reserved_blocks_per_bank);
		uint32_t block_number = curVFLCxt->reserved_block_pool_start + (i % reserved_blocks_per_bank);
		uint32_t pBlock;

		if(curVFLCxt->reserved_block_pool_map[i] != _block)
			continue;

		virtual_block_to_physical_block(_vfl, bank, block_number, &pBlock);
		vfl->bbt[_ce][pBlock] &= ~(1 << (pBlock & 7));
	}

	physical_block_to_virtual_block(_vfl, _block, &vBank, &vBlock);
	while(curVFLCxt->replaced_block_count[vBank] < (vfl->geometry.blocks_per_bank_vfl - curVFLCxt->reserved_block_pool_start)) {
		uint32_t weirdBlock = curVFLCxt->replaced_block_count[vBank] + (vfl->geometry.blocks_per_bank_vfl - curVFLCxt->reserved_block_pool_start) * vBank;
		if(curVFLCxt->reserved_block_pool_map[weirdBlock] == 0xFFF0) {
			curVFLCxt->reserved_block_pool_map[weirdBlock] = _block;
			curVFLCxt->replaced_block_count[vBank]++;
			return SUCCESS;
		}
		vBank++;
	}

	for (i = 0; i < vfl->geometry.banks_per_ce; i++) {
		drbc[i] = i;
	}
	if(vfl->geometry.banks_per_ce != 1) {
		for(i = 0; i < (vfl->geometry.banks_per_ce - 1); i++) {
			int j;
			for (j = i + 1; j < vfl->geometry.banks_per_ce; j++) {
				if(curVFLCxt->replaced_block_count[drbc[j]] < curVFLCxt->replaced_block_count[i]) {
					drbc[j] = i;
					drbc[i] = j;
				}
			}
		}
	}
	for (i = 0; i < vfl->geometry.banks_per_ce; i++) {
		while(curVFLCxt->replaced_block_count[drbc[i]] < (vfl->geometry.blocks_per_bank_vfl - curVFLCxt->reserved_block_pool_start)) {
			uint32_t weirdBlock = curVFLCxt->replaced_block_count[drbc[i]] + (vfl->geometry.blocks_per_bank_vfl - curVFLCxt->reserved_block_pool_start) * vBank;
			if(curVFLCxt->reserved_block_pool_map[weirdBlock] == 0xFFF0) {
				curVFLCxt->reserved_block_pool_map[weirdBlock] = _block;
				curVFLCxt->replaced_block_count[drbc[i]]++;
				return SUCCESS;
			}
			i++;
		}
	}

	panic("vsvfl_replace_bad_block: Failed to replace block\r\n");
	return -EIO;
}

static error_t vfl_vsvfl_erase_single_block(struct apple_vfl *_vfl, uint32_t _vbn, int _replaceBadBlock) {
	struct VSVFL *vfl = _vfl->private;
	uint32_t bank;
	uint32_t status = EINVAL;

	// In order to erase a single virtual block, we have to erase the matching
	// blocks across all banks.
	for (bank = 0; bank < vfl->geometry.banks_total; bank++) {
		uint32_t pBlock, pCE, blockRemapped;

		// Find the physical block before bad-block remapping.
		virtual_block_to_physical_block(_vfl, bank, _vbn, &pBlock);
		pCE = bank % vfl->geometry.num_ce;
		vfl->blockBuffer[bank] = pBlock;

		if (is_block_in_scrub_list(_vfl, pCE, pBlock)) {
			vsvfl_context_t *curVFLCxt = &vfl->contexts[pCE];

			vsvfl_replace_bad_block(_vfl, pCE, pBlock);
			vfl_gen_checksum(_vfl, pCE);
			if(is_block_in_scrub_list(_vfl, pCE, vfl->blockBuffer[bank])) {
				int i;
				for (i = 0; i < curVFLCxt->scrub_list_length; i++) {
					if(curVFLCxt->scrub_list[i] != vfl->blockBuffer[bank])
						continue;
					if(!vfl_check_checksum(_vfl, pCE))
						panic("vfl_erase_single_block: failed checksum\r\n");
					curVFLCxt->scrub_list[i] = 0;
					curVFLCxt->scrub_list_length--;
					if(i != curVFLCxt->scrub_list_length && curVFLCxt->scrub_list_length != 0)
						curVFLCxt->scrub_list[i] = curVFLCxt->scrub_list[curVFLCxt->scrub_list_length];
					vfl_gen_checksum(_vfl, pCE);
					vsvfl_store_vfl_cxt(_vfl, pCE);
					break;
				}
			} else
				printk("vfl_erase_single_block: Failed checking for block in scrub list\r\n");
		}

		// Remap the block and calculate its physical number (considering bank address space).
		blockRemapped = remap_block(_vfl, pCE, pBlock, 0);
		vfl->blockBuffer[bank] = blockRemapped % vfl->geometry.blocks_per_bank
			+ (blockRemapped / vfl->geometry.blocks_per_bank) * vfl->geometry.bank_address_space;
	}

	// TODO: H2FMI erase multiple blocks. Currently we erase the blocks one by one.
	// Actually, the block buffer is used for erase multiple blocks, so we won't use it here.

	for (bank = 0; bank < vfl->geometry.banks_total; bank++) {
		uint32_t pBlock, pCE, tries;

		virtual_block_to_physical_block(_vfl, bank, _vbn, &pBlock);
		pCE = bank % vfl->geometry.num_ce;

		// Try to erase each block at most 3 times.
		for (tries = 0; tries < 3; tries++) {
			uint32_t blockRemapped, bankStart, blockOffset;

			blockRemapped = remap_block(_vfl, pCE, pBlock, 0);
			bankStart = (blockRemapped / vfl->geometry.blocks_per_bank) * vfl->geometry.bank_address_space;
			blockOffset = blockRemapped % vfl->geometry.blocks_per_bank;

			status = _vfl->erase_single_block(_vfl, pCE, bankStart + blockOffset);
			if (status == 0)
				break;

			// TODO: add block map support.
			//mark_bad_block(vfl, pCE, pBlock, status);
			printk("vfl: failed erasing physical block %d on bank %d. status: 0x%08x\r\n",
				blockRemapped, bank, status);

			if (!_replaceBadBlock)
				return -EINVAL;

			// Bad block management at erasing should actually be like this (improvised \o/)
			vsvfl_replace_bad_block(_vfl, pCE, bankStart + blockOffset);
			if(!vfl_check_checksum(_vfl, pCE))
				panic("vfl_erase_single_block: failed checksum\r\n");
			vfl->contexts[pCE].bad_block_count++;
			vfl_gen_checksum(_vfl, pCE);
			vsvfl_store_vfl_cxt(_vfl, pCE);
		}
	}

	if (status)
		panic("vfl: failed to erase virtual block %d!\r\n", _vbn);

	return 0;
}

static vsvfl_context_t* get_most_updated_context(struct apple_vfl *_vfl) {
	int ce = 0;
	int max = 0;
	vsvfl_context_t* cxt = NULL;
	struct VSVFL *vfl = _vfl->private;

	for(ce = 0; ce < vfl->geometry.num_ce; ce++)
	{
		int cur = vfl->contexts[ce].usn_inc;
		if(max <= cur)
		{
			max = cur;
			cxt = &vfl->contexts[ce];
		}
	}

	return cxt;
}

static uint16_t* VFL_get_FTLCtrlBlock(struct apple_vfl *_vfl)
{
	//struct VSVFL *vfl = _vfl->private;
	vsvfl_context_t *cxt = get_most_updated_context(_vfl);

	if(cxt)
		return cxt->control_block;
	else
		return NULL;
}

static inline error_t vfl_vsvfl_setup_geometry(struct apple_vfl *_vfl)
{
	struct VSVFL *vfl = get_vsvfl(_vfl);
	uint16_t z, a;
	uint32_t mag;

	#ifdef CONFIG_MACH_IPAD_1G
		vfl->geometry.reserved_blocks = 1;
	#else
		vfl->geometry.reserved_blocks = 16;
	#endif

#define nand_load(what, where) (vfl->geometry.where = apple_vfl_get(_vfl, (what)))

	nand_load(NAND_BLOCKS_PER_CE, blocks_per_ce);
	printk("blocks per ce: 0x%08x\r\n", vfl->geometry.blocks_per_ce);

	nand_load(NAND_BLOCKS_PER_BANK, blocks_per_bank);
	printk("blocks per bank: 0x%0x\r\n", vfl->geometry.blocks_per_bank);

	nand_load(NAND_BANKS_PER_CE, banks_per_ce);
	printk("banks per ce: 0x%08x\r\n", vfl->geometry.banks_per_ce);

	nand_load(NAND_PAGE_SIZE, bytes_per_page);
	printk("bytes per page: 0x%08x\r\n", vfl->geometry.bytes_per_page);

	nand_load(NAND_BANK_ADDRESS_SPACE, bank_address_space);
	printk("bank address space: 0x%08x\r\n", vfl->geometry.bank_address_space);

	vfl->geometry.num_ce = _vfl->num_chips;
	printk("num ce: 0x%08x\r\n", vfl->geometry.num_ce);

	nand_load(NAND_PAGES_PER_BLOCK, pages_per_block);
	printk("pages per block: 0x%08x\r\n", vfl->geometry.pages_per_block);

	nand_load(NAND_BLOCK_ADDRESS_SPACE, pages_per_block_2);
	printk("block address space: 0x%08x\r\n", vfl->geometry.pages_per_block_2);

	nand_load(NAND_ECC_BITS, ecc_bits);
	printk("ecc bits: 0x%08x\r\n", vfl->geometry.ecc_bits);

	z = vfl->geometry.blocks_per_ce;
	mag = 1;
	while(z != 0 && mag < z) mag <<= 1;
	mag >>= 10;

	a = (mag << 7) - (mag << 3) + mag;
	vfl->geometry.some_page_mask = a;
	printk("some_page_mask: 0x%08x\r\n", vfl->geometry.some_page_mask);

	vfl->geometry.pages_total = z * vfl->geometry.pages_per_block * vfl->geometry.num_ce;
	printk("pages_total: 0x%08x\r\n", vfl->geometry.pages_total);

	vfl->geometry.pages_per_sublk = vfl->geometry.pages_per_block * vfl->geometry.banks_per_ce * vfl->geometry.num_ce;
	printk("pages_per_sublk: 0x%08x\r\n", vfl->geometry.pages_per_sublk);

	vfl->geometry.some_sublk_mask =
		vfl->geometry.some_page_mask * vfl->geometry.pages_per_sublk;
	printk("some_sublk_mask: 0x%08x\r\n", vfl->geometry.some_sublk_mask);

	vfl->geometry.banks_total = vfl->geometry.num_ce * vfl->geometry.banks_per_ce;
	printk("banks_total: 0x%08x\r\n", vfl->geometry.banks_total);

	nand_load(NAND_OOB_SIZE, num_ecc_bytes);
	printk("num_ecc_bytes: 0x%08x\r\n", vfl->geometry.num_ecc_bytes);

	nand_load(NAND_OOB_ALLOC, bytes_per_spare);
	printk("bytes_per_spare: 0x%08x\r\n", vfl->geometry.bytes_per_spare);

	vfl->geometry.one = 1;
	printk("one: 0x%08x\r\n", vfl->geometry.one);

	if(vfl->geometry.num_ce != 1)
	{
		vfl->geometry.some_crazy_val =	vfl->geometry.blocks_per_ce - 27
			- vfl->geometry.reserved_blocks - vfl->geometry.some_page_mask;
		printk("blocks_per_ce: %x ,reserved_blocks: %x ,some_page_mask: %x ,\r\n", vfl->geometry.blocks_per_ce,vfl->geometry.reserved_blocks,vfl->geometry.some_page_mask);

	}
	else
	{
		vfl->geometry.some_crazy_val =	vfl->geometry.blocks_per_ce - 27
			- vfl->geometry.some_page_mask - (vfl->geometry.reserved_blocks & 0xFFFF);
	}

	printk("some_crazy_val: 0x%08x\r\n", vfl->geometry.some_crazy_val);

	vfl->geometry.vfl_blocks = vfl->geometry.some_crazy_val + 4;
	printk("vfl_blocks: 0x%08x\r\n", vfl->geometry.vfl_blocks);

	vfl->geometry.fs_start_block = vfl->geometry.vfl_blocks + vfl->geometry.reserved_blocks;
	printk("fs_start_block: 0x%08x\r\n", vfl->geometry.fs_start_block);

	/*uint32_t val = 1;
	nand_device_set_info(nand, diVendorType, &val, sizeof(val));

	val = 0x10001;
	nand_device_set_info(nand, diBanksPerCEVFL, &val, sizeof(val));*/

#undef nand_load

	return SUCCESS;
}

static error_t vfl_vsvfl_open(struct apple_vfl *_vfl)
{
	struct VSVFL *vfl = kzalloc(sizeof(struct VSVFL), GFP_KERNEL);
	error_t ret;
	uint32_t ce = 0;
	int vendorType;

	if(!vfl)
		return -ENOMEM;
	_vfl->private = vfl;

	ret = vfl_vsvfl_setup_geometry(_vfl);
	if(FAILED(ret))
	{
		kfree(vfl);
		return ret;
	}

	printk("vsvfl: Opening %p.\r\n", _vfl);

	vfl->contexts = kzalloc(vfl->geometry.num_ce * sizeof(vsvfl_context_t), GFP_KERNEL);
	if(!vfl->contexts)
	{
		kfree(vfl);
		return -ENOMEM;
	}

	vfl->pageBuffer = (uint32_t*) kmalloc(
			vfl->geometry.pages_per_block * sizeof(uint32_t), GFP_KERNEL);
	vfl->chipBuffer = (uint16_t*) kmalloc(
			vfl->geometry.pages_per_block * sizeof(uint16_t), GFP_KERNEL);
	vfl->blockBuffer = (uint16_t*) kmalloc(
			vfl->geometry.banks_total * sizeof(uint16_t), GFP_KERNEL);
	vfl->stats = kzalloc(sizeof(VSVFLStats), GFP_KERNEL);

	if(!vfl->pageBuffer || !vfl->chipBuffer
			|| !vfl->blockBuffer || !vfl->stats)
	{
		kfree(vfl->pageBuffer);
		kfree(vfl->chipBuffer);
		kfree(vfl->blockBuffer);
		kfree(vfl->stats);
		kfree(vfl);
		return -ENOMEM;
	}

	for(ce = 0; ce < vfl->geometry.num_ce; ce++) {
		int i;
		int page = 8;
		int last = 0;

		vfl->bbt[ce] = (uint8_t*) kmalloc(CEIL_DIVIDE(vfl->geometry.blocks_per_ce, 8), GFP_KERNEL);



		printk("vsvfl: Checking CE %d.\r\n", ce);

		if(FAILED(apple_vfl_special_page(_vfl, ce, "DEVICEINFOBBT\0\0\0",
						vfl->bbt[ce], CEIL_DIVIDE(vfl->geometry.blocks_per_ce, 8))))
		{
			printk("vsvfl: Failed to find DEVICEINFOBBT!\r\n");
			return -EIO;
		}

		if(ce >= vfl->geometry.num_ce)
			return -EIO;

		vsvfl_context_t *curVFLCxt = &vfl->contexts[ce];
		uint8_t* pageBuffer = kmalloc(vfl->geometry.bytes_per_page, GFP_KERNEL);
		uint8_t* spareBuffer = kmalloc(vfl->geometry.bytes_per_spare, GFP_KERNEL);
		memset(pageBuffer,0,vfl->geometry.bytes_per_page);
		//memset(spareBuffer,0,vfl->geometry.bytes_per_spare);
		//int fl=0;
		//for (fl=0;fl<=10;fl++){
			//memset(spareBuffer,0,vfl->geometry.bytes_per_page);
			//int rr = apple_vfl_read_nand_page(_vfl, ce, vfl->geometry.reserved_blocks*vfl->geometry.pages_per_block, pageBuffer, spareBuffer);
			//printk("-------%d Start-------\n",fl);
			//print_hex_dump(KERN_INFO, "", DUMP_PREFIX_OFFSET, 16, 1, pageBuffer,0x200, true);
			//printk("--------%d End--------\n",fl);
			//print_hex_dump(KERN_INFO, "SB: ", DUMP_PREFIX_OFFSET, 16, 1, spareBuffer,12, true);
			//msleep(1000);
		//}

		if(pageBuffer == NULL || spareBuffer == NULL || !vfl->bbt[ce]) {
			printk("vfl: cannot allocate page and spare buffer\r\n");
			return -ENOMEM;
		}

		// Any VFLCxt page will contain an up-to-date list of all blocks used to store VFLCxt pages. Find any such
		// page in the system area.

		for(i = vfl->geometry.reserved_blocks; i < vfl->geometry.fs_start_block; i++) {
			// so pstBBTArea is a bit array of some sort
			if(!(vfl->bbt[ce][i / 8] & (1 << (i  & 0x7))))
				continue;

			//printk(KERN_INFO "reading vfl area: %d, %d, %p.\n", i, ce, _vfl);
			if(SUCCEEDED(apple_vfl_read_nand_page(_vfl, ce, i*vfl->geometry.pages_per_block, pageBuffer, spareBuffer)))
			{
				memcpy(curVFLCxt->vfl_context_block, ((vsvfl_context_t*)pageBuffer)->vfl_context_block,
						sizeof(curVFLCxt->vfl_context_block));
				break;
			}
		}

		if(i == vfl->geometry.fs_start_block) {
			printk("vsvfl: cannot find readable VFLCxtBlock\r\n");
			kfree(pageBuffer);
			kfree(spareBuffer);
			return -EIO;
		}

		// Since VFLCxtBlock is a ringbuffer, if blockA.page0.spare.usnDec < blockB.page0.usnDec, then for any page a
		// in blockA and any page b in blockB, a.spare.usNDec < b.spare.usnDec. Therefore, to begin finding the
		// page/VFLCxt with the lowest usnDec, we should just look at the first page of each block in the ring.
		int minUsn = 0xFFFFFFFF;
		int VFLCxtIdx = 4;

		for(i = 0; i < 4; i++) {
			uint16_t block = curVFLCxt->vfl_context_block[i];
			//printk("vsvfl: block:%x\r\n",block);

			if(block == 0xFFFF)
				continue;
			//print_hex_dump(KERN_INFO, "spareBuffer1 : ", DUMP_PREFIX_OFFSET, 16, 1, spareBuffer,12, true);

			if(FAILED(apple_vfl_read_nand_page(_vfl, ce, block*vfl->geometry.pages_per_block, pageBuffer, spareBuffer)))
				continue;
			print_hex_dump(KERN_INFO, "spareBuffer: ", DUMP_PREFIX_OFFSET, 16, 1, spareBuffer,12, false);
			//msleep(4000);
			vfl_vsvfl_spare_data_t *spareData = (vfl_vsvfl_spare_data_t*)spareBuffer;
			//printk("vsvfl: spareData->meta.usnDec:%x\r\n",((vfl_vsvfl_spare_data_t*)spareBuffer)->meta.usnDec);

			if(spareData->meta.usnDec > 0 && spareData->meta.usnDec <= minUsn) {
				minUsn = spareData->meta.usnDec;
				VFLCxtIdx = i;
			}
		}

		if(VFLCxtIdx == 4) {
			printk("vsvfl: cannot find readable VFLCxtBlock index in spares\r\n");
			kfree(pageBuffer);
			kfree(spareBuffer);
			return -EIO;
		}

		// VFLCxts are stored in the block such that they are duplicated 8 times. Therefore, we only need to
		// read every 8th page, and nand_readvfl_cxt_page will try the 7 subsequent pages if the first was
		// no good. The last non-blank page will have the lowest spare.usnDec and highest usnInc for VFLCxt
		// in all the land (and is the newest).
		for(page = 8; page < vfl->geometry.pages_per_block; page += 8) {
			if(apple_vfl_read_nand_page(_vfl, ce, curVFLCxt->vfl_context_block[VFLCxtIdx]*vfl->geometry.pages_per_block + page, pageBuffer, spareBuffer) != 0) {
				break;
			}

			last = page;
		}
		if(apple_vfl_read_nand_page(_vfl, ce, curVFLCxt->vfl_context_block[VFLCxtIdx]*vfl->geometry.pages_per_block + last, pageBuffer, spareBuffer) != 0) {
			printk("vsvfl: cannot find readable VFLCxt\n");
			kfree(pageBuffer);
			kfree(spareBuffer);
			return -1;
		}
		// Aha, so the upshot is that this finds the VFLCxt and copies it into vfl->contexts
		memcpy(&vfl->contexts[ce], pageBuffer, sizeof(vsvfl_context_t));

		// This is the newest VFLCxt across all CEs
		if(curVFLCxt->usn_inc >= vfl->current_version) {
			vfl->current_version = curVFLCxt->usn_inc;
		}

		kfree(pageBuffer);
		kfree(spareBuffer);

		// Verify the checksum
		if(!vfl_check_checksum(_vfl, ce))
		{
			printk("vsvfl: VFLCxt has bad checksum.\r\n");
			return -EIO;
		}
	}

	// retrieve some global parameters from the latest VFL across all CEs.
	{
		vsvfl_context_t *latestCxt = get_most_updated_context(_vfl);

		// Then we update the VFLCxts on every ce with that information.
		for(ce = 0; ce < vfl->geometry.num_ce; ce++) {
			// Don't copy over own data.
			if(&vfl->contexts[ce] != latestCxt) {
				// Copy the data, and generate the new checksum.
				memcpy(vfl->contexts[ce].control_block, latestCxt->control_block, sizeof(latestCxt->control_block));
				vfl->contexts[ce].usable_blocks_per_bank = latestCxt->usable_blocks_per_bank;
				vfl->contexts[ce].reserved_block_pool_start = latestCxt->reserved_block_pool_start;
				vfl->contexts[ce].ftl_type = latestCxt->ftl_type;
				memcpy(vfl->contexts[ce].field_6CA, latestCxt->field_6CA, sizeof(latestCxt->field_6CA));
				
				vfl_gen_checksum(_vfl, ce);
			}
		}
	}
	
	// Vendor-specific virtual-from/to-physical functions.
	// Note: support for some vendors is still missing.
	vendorType = vfl->contexts[0].vendor_type;
	
	if(!vendorType)
		return -EIO;
	
	switch(vendorType) {
	case 0x10001:
		vfl->geometry.banks_per_ce = 1;
		vfl->virtual_to_physical = virtual_to_physical_10001;
		vfl->physical_to_virtual = physical_to_virtual_10001;
		break;
		
	case 0x100010:
	case 0x100014:
	case 0x120014:
		vfl->geometry.banks_per_ce = 2;
		vfl->virtual_to_physical = virtual_to_physical_100014;
		vfl->physical_to_virtual = physical_to_virtual_100014;
		break;
		
	case 0x150011:
		vfl->geometry.banks_per_ce = 2;
		vfl->virtual_to_physical = virtual_to_physical_150011;
		vfl->physical_to_virtual = physical_to_virtual_150011;
		break;
		
	default:
		printk("vsvfl: unsupported vendor 0x%06x\r\n", vendorType);
		return -EIO;
	}
	
	//if(FAILED(vsvfl_set(_vfl, VFL_VENDOR_TYPE, &vendorType)))
	//	return -EIO;

	vfl->geometry.pages_per_sublk = vfl->geometry.pages_per_block * vfl->geometry.banks_per_ce * vfl->geometry.num_ce;
	vfl->geometry.banks_total = vfl->geometry.num_ce * vfl->geometry.banks_per_ce;
	vfl->geometry.blocks_per_bank_vfl = vfl->geometry.blocks_per_ce / vfl->geometry.banks_per_ce;
	printk("vsvfl: detected chip vendor 0x%06x\r\n", vendorType);

	{
		uint32_t banksPerCE = vfl->geometry.banks_per_ce;
		if(FAILED(apple_vfl_set(_vfl, NAND_BANKS_PER_CE_VFL, &banksPerCE)))
			return -EIO;
		

		// Now, discard the old scfg bad-block table, and set it using the VFL context's reserved block pool map.
		uint32_t bank, i;
		uint32_t num_reserved = vfl->contexts[0].reserved_block_pool_start;
		uint32_t num_non_reserved = vfl->geometry.blocks_per_bank_vfl - num_reserved;

		
		for(ce = 0; ce < vfl->geometry.num_ce; ce++) {
			memset(vfl->bbt[ce], 0xFF, CEIL_DIVIDE(vfl->geometry.blocks_per_ce, 8));
			
			for(bank = 0; bank < banksPerCE; bank++) {
				for(i = 0; i < num_non_reserved; i++) {
					uint16_t mapEntry = vfl->contexts[ce].reserved_block_pool_map[bank * num_non_reserved + i];
					uint32_t pBlock;
					
					if(mapEntry == 0xFFF0)
						continue;
					
					if(mapEntry < vfl->geometry.blocks_per_ce) {
						pBlock = mapEntry;
					} else if(mapEntry > 0xFFF0) {
						virtual_block_to_physical_block(_vfl, ce + bank * vfl->geometry.num_ce, num_reserved + i, &pBlock);
					} else {
						printk("vsvfl: ce:%x bank:%x i:%x \n",ce,bank,i);
						panic("vsvfl: bad map table: CE %d, entry %d, value 0x%08x\r\n",
								ce, bank * num_non_reserved + i, mapEntry);
					}
					
					vfl->bbt[ce][pBlock / 8] &= ~(1 << (pBlock % 8));
				}
		}
		}
	}
	printk("vsvfl: VFL successfully opened!\r\n");

	return SUCCESS;
}

static int vsvfl_get(struct apple_vfl *_vfl, int _item)
{

	struct VSVFL *vfl = get_vsvfl(_vfl);
	struct apple_nand *nand = _vfl->devices[0];
	vsvfl_context_t *ctx = &vfl->contexts[0];

	switch(_item)
	{
	case VFL_PAGES_PER_BLOCK:
		return vfl->geometry.pages_per_sublk;

	case VFL_USABLE_BLOCKS_PER_BANK:
		return ctx->usable_blocks_per_bank;

	case VFL_FTL_TYPE:
		return ctx->ftl_type;

	case NAND_TOTAL_BANKS_VFL:
			return (vfl->geometry.num_ce * vfl->geometry.banks_per_ce);

	default:
		return apple_nand_get(nand, _item);
	}
}

static int vsvfl_set(struct apple_vfl *_vfl, int _item, int _val)
{

	struct VSVFL *vfl = get_vsvfl(_vfl);
	struct apple_nand *nand = _vfl->devices[0];

	switch(_item)
	{

	default:
	return apple_nand_set(nand, _item, _val);
	}

}

static void* *vfl_vsvfl_get_stats(struct apple_vfl *_vfl, uint32_t *size)
{
	struct VSVFL *vfl = get_vsvfl(_vfl);
	if(size)
		*size = sizeof(VSVFLStats);
	return (void*)vfl->stats;
}

static struct apple_nand* *vfl_vsvfl_get_device(struct apple_vfl *_vfl, int num)
{
	struct apple_nand *nand = _vfl->devices[num];
	return nand;
}

static void vsvfl_cleanup(struct apple_vfl *_vfl)
{
	uint32_t i;
	struct VSVFL *vfl = get_vsvfl(_vfl);

	if(vfl->contexts)
		kfree(vfl->contexts);

	for (i = 0; i < sizeof(vfl->bbt) / sizeof(void*); i++) {
		if(vfl->bbt[i])
			kfree(vfl->bbt[i]);
	}

	if(vfl->pageBuffer)
		kfree(vfl->pageBuffer);

	if(vfl->chipBuffer)
		kfree(vfl->chipBuffer);

	if(vfl->blockBuffer)
		kfree(vfl->blockBuffer);

	memset(vfl, 0, sizeof(*vfl));
}

int apple_vsvfl_detect(struct apple_vfl *_vfl)
{
	struct VSVFL *vfl = kzalloc(sizeof(*vfl), GFP_KERNEL);
	if(!vfl)
		return -ENOMEM;

	printk(KERN_INFO "apple-vsvfl: detected.\n");
	memset(&vfl->geometry, 0, sizeof(vfl->geometry));

	_vfl->private = vfl;
	_vfl->cleanup = vsvfl_cleanup;
	//_vfl->read = vsvfl_read;
	//_vfl->write = vsvfl_write;
	_vfl->get = vsvfl_get;
	_vfl->set = vsvfl_set;
	_vfl->get_stats = vfl_vsvfl_get_stats;

	_vfl->get_device = vfl_vsvfl_get_device;

	_vfl->read_single_page = vfl_vsvfl_read_single_page;

	_vfl->write_single_page = vfl_vsvfl_write_single_page;

	_vfl->erase_single_block = vfl_vsvfl_erase_single_block;

	_vfl->write_context = vfl_vsvfl_write_context;
	_vfl->get_ftl_ctrl_block = VFL_get_FTLCtrlBlock;

	return vfl_vsvfl_open(_vfl);
}
