/**
 * Copyright (c) 2016 Erfan Abdi.
 *
 * This file is part of the iDroid Project. (http://www.idroidproject.org).
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
typedef struct _MBRPartitionRecord {
	uint8_t status;
	uint8_t beginHead;
	uint8_t beginSectorCyl;
	uint8_t beginCyl;
	uint8_t type;
	uint8_t endHead;
	uint8_t endSectorCyl;
	uint8_t endCyl;
	uint32_t beginLBA;
	uint32_t numSectors;
} __attribute__ ((packed)) MBRPartitionRecord;

typedef struct _MBR {
	uint8_t code[0x1B8];
	uint32_t signature;
	uint16_t padding;
	MBRPartitionRecord partitions[4];
	uint16_t magic;
} __attribute__ ((packed)) MBR;

typedef struct _LwVMPartitionRecord {
	uint64_t type[2];
	uint64_t guid[2];
	uint64_t begin;
	uint64_t end;
	uint64_t attribute; // 0 == unencrypted; 0x1000000000000 == encrypted
	char	partitionName[0x48];
} __attribute__ ((packed)) LwVMPartitionRecord;

typedef struct _LwVM {
	uint64_t type[2];
	uint64_t guid[2];
	uint64_t mediaSize;
	uint32_t numPartitions;
	uint32_t crc32;
	uint8_t unkn[464];
	LwVMPartitionRecord partitions[12];
	uint16_t chunks[1024]; // chunks[0] should be 0xF000
} __attribute__ ((packed)) LwVM;

int lwvm_partition(struct parsed_partitions *state);
