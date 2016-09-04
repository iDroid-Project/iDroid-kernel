/**
 * Copyright (c) 2016 Erfan Abdi.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ctype.h>
#include "check.h"
#include "lwvm.h"


static const char LwVMType[] = { 0x6A, 0x90, 0x88, 0xCF, 0x8A, 0xFD, 0x63, 0x0A, 0xE3, 0x51, 0xE2, 0x48, 0x87, 0xE0, 0xB9, 0x8B };
static const char LwVMType_noCRC[] = { 0xB1, 0x89, 0xA5, 0x19, 0x4F, 0x59, 0x4B, 0x1D, 0xAD, 0x44, 0x1E, 0x12, 0x7A, 0xAF, 0x45, 0x39 };
uint16_t** LwVM_chunks;
uint32_t LwVM_numValidChunks;
uint32_t LwVM_rangeShiftValue;
uint32_t LwVM_rangeByteCount;
uint64_t LwVM_seek;

int lwvm_partition(struct parsed_partitions *state)
{
	Sector sect;

	MBR *md;
	LwVM *lwvm;

	/* Get 0th block and look at the first partition map entry. */
	md = read_part_sector(state, sizeof(MBR), &sect);
	if (!md)
		return -1;

	if(!memcmp(LwVMType, ((LwVM*)md)->type, sizeof(LwVMType))) {
		put_dev_sector(sect);
		lwvm = read_part_sector(state, sizeof(lwvm), &sect);
		if (!lwvm) {
			printk("bdev: detected LwVM partition table but failed to read it.\r\n");
			put_dev_sector(sect);
			return -EIO;
		}
		LwVM_rangeShiftValue = 32 - __builtin_clz((lwvm->mediaSize-1) >> 10);
		LwVM_rangeByteCount = 1 << LwVM_rangeShiftValue;
		LwVM_numValidChunks = lwvm->mediaSize >> LwVM_rangeShiftValue;
		LwVM_chunks = kmalloc(lwvm->numPartitions*sizeof(uint32_t),GFP_KERNEL);
		memset(LwVM_chunks, 0, lwvm->numPartitions*sizeof(uint32_t));
		int i;
		for(i = 0; i < lwvm->numPartitions; i++){
			LwVM_chunks[i] = kmalloc(0x800,GFP_KERNEL);
			memset(LwVM_chunks[i], 0xFF, 0x800);
			int j;
			for (j = 1; j < 0x400; j++) {
				uint16_t chunk_unk = lwvm->chunks[j];
				if(chunk_unk >> 12 == i) {
					LwVM_chunks[i][chunk_unk & 0x3ff] = j;
				}
			}
			LwVMPartitionRecord *record = &lwvm->partitions[i];
			char *string = kmalloc(sizeof(record->partitionName)/2,GFP_KERNEL);
			memset(string, 0, sizeof(string));
			for (j = 0; record->partitionName[j*2] != 0; j++) {
				string[j] = record->partitionName[j*2];
			}
			printk("bdev: partition id: %d, name: %s, range: %u - %u\r\n", i, string, (uint32_t)record->begin, (uint32_t)record->end);
			put_partition(state, i,
					(uint32_t)record->begin,
					(uint32_t)record->end - (uint32_t)record->begin);
			kfree(string);
			if(i == 8)
				break;
		}
		put_dev_sector(sect);
	}

	return 1;
}
