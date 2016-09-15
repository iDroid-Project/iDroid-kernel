#ifndef _LINUX_APPLE_FLASH_H
#define _LINUX_APPLE_FLASH_H

#include <linux/scatterlist.h>
#include <linux/device.h>
#include <plat/cdma.h>

typedef uint32_t page_t;


typedef int error_t;
#define FAILED(x) ((x) < 0)
#define SUCCEEDED(x) ((x) >= 0)
#define SUCCESS 0
#define ERROR(x) (-(x))
#define CEIL_DIVIDE(x, y) (((x)+(y)-1)/(y))
#define CONTAINER_OF(type, member, ptr) ((type*)(((char*)(ptr)) - ((char*)(&((type*)NULL)->member))))
//#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))
#define ROUND_UP(val, amt) (((val + amt - 1) / amt) * amt)

#ifndef MIN
#define MIN(a, b) (((a) < (b))? (a): (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b))? (a): (b))
#endif


enum apple_nand_info
{
	NAND_NUM_CE,
	NAND_BITMAP,

	NAND_BLOCKS_PER_CE,
	NAND_PAGES_PER_CE,
	NAND_PAGES_PER_BLOCK,
	NAND_ECC_STEPS,
	NAND_ECC_BITS,
	NAND_BYTES_PER_SPARE,
	NAND_TOTAL_BANKS_VFL,
	NAND_BANKS_PER_CE_VFL,
	NAND_BANKS_PER_CE,
	NAND_BLOCKS_PER_BANK,
	NAND_BLOCK_ADDRESS_SPACE,
	NAND_BANK_ADDRESS_SPACE,
	NAND_TOTAL_BLOCK_SPACE,

	NAND_PAGE_SIZE,
	NAND_OOB_SIZE,
	NAND_OOB_ALLOC,

	VFL_PAGES_PER_BLOCK,
	VFL_USABLE_BLOCKS_PER_BANK,
	VFL_FTL_TYPE,
	VFL_VENDOR_TYPE,

};

enum apple_vfl_detection
{
	APPLE_VFL_OLD_STYLE = 0x64,
	APPLE_VFL_NEW_STYLE = 0x65,
};

struct apple_chip_map
{
	int bus;
	u16 chip;
};

struct apple_vfl;

struct apple_nand
{
	struct device *device;
	struct apple_vfl *vfl;
	int index;

	int (*detect)(struct apple_nand *, uint8_t *_buf, size_t _size);

	int (*default_aes)(struct apple_nand *, struct cdma_aes *, int _decrypt);
	int (*aes)(struct apple_nand *, struct cdma_aes *);
	void (*setup_aes) (struct apple_nand *, int _enabled, int _encrypt, const uint8_t * _offset);

	int (*read)(struct apple_nand *, size_t _count,
		u16 *_chips, page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob);

	int (*write)(struct apple_nand *, size_t _count,
		u16 *_chips, page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob);

	int (*erase)(struct apple_nand *, size_t _count,
		u16 *_chips, page_t *_pages);

	int (*get)(struct apple_nand *, int _info);
	int (*set)(struct apple_nand *, int _info, int _val);

	int (*is_bad)(struct apple_nand*, u16 _ce, page_t _page);
	void (*set_bad)(struct apple_nand*, u16 _ce, page_t _page);
};

static inline int apple_nand_get(struct apple_nand *_nd, int _id)
{
	if(!_nd)
		return 0;

	return _nd->get(_nd, _id);
}

static inline int apple_nand_set(struct apple_nand *_nd, int _id, int _val)
{
	if(!_nd)
		return -EINVAL;

	return _nd->set(_nd, _id, _val);
}

extern int apple_nand_special_page(struct apple_nand*, u16 _ce, char _page[16],
		uint8_t* _buffer, size_t _amt);
extern int apple_nand_read_page(struct apple_nand*, u16 _ce, page_t _page,
		uint8_t *_data, uint8_t *_oob);
extern int apple_nand_write_page(struct apple_nand*, u16 _ce, page_t _page,
	 	const uint8_t *_data, const uint8_t *_oob);
extern int apple_nand_erase_block(struct apple_nand*, u16 _ce, page_t _page);

extern int apple_nand_register(struct apple_nand*, struct apple_vfl*, struct device*);
extern void apple_nand_unregister(struct apple_nand*);

extern uint8_t DKey[32];
extern uint8_t EMF[32];
extern int h2fmi_ftl_count;
extern int h2fmi_ftl_databuf;
extern int h2fmi_ftl_smth[2];
extern void set_ftl_region(int _lpn, int _a2, int _count, void* _buf);

extern int h2fmi_emf;
extern int h2fmi_emf_iv_input;
extern void h2fmi_set_emf(int enable, int iv_input);
extern int h2fmi_get_emf(void);

struct apple_vfl
{
	int num_devices;
	int max_devices;
	struct apple_nand **devices;
	int num_chips;
	int max_chips;
	struct apple_chip_map *chips;

	void *private;

	void (*cleanup)(struct apple_vfl*);

	int (*read)(struct apple_vfl *, int _count,
		page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob);

	int (*write)(struct apple_vfl *, int _count,
		page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob);

	int (*get)(struct apple_vfl *, int _info);
	int (*set)(struct apple_vfl *, int _info, int _val);


	void* (*get_stats) (struct apple_vfl *_vfl, uint32_t *size);
	struct apple_nand* (*get_device) (struct apple_vfl *_vfl, int num);
	error_t (*read_single_page) (struct apple_vfl *_vfl, uint32_t dwVpn, uint8_t* buffer, uint8_t* spare, int empty_ok, int* refresh_page);
	error_t (*write_single_page) (struct apple_vfl *_vfl, uint32_t dwVpn, uint8_t* buffer, uint8_t* spare, int _scrub);
	error_t (*erase_single_block) (struct apple_vfl *_vfl, uint32_t _vbn, int _replaceBadBlock);
	error_t (*write_context) (struct apple_vfl *_vfl, uint16_t *_control_block);
	uint16_t* (*get_ftl_ctrl_block) (struct apple_vfl *_vfl);

};

extern void apple_vfl_init(struct apple_vfl*);
extern int apple_vfl_register(struct apple_vfl*, enum apple_vfl_detection _detect);
extern void apple_vfl_unregister(struct apple_vfl*);

extern int apple_vfl_special_page(struct apple_vfl*, u16 _ce, char _page[16],
		uint8_t* _buffer, size_t _amt);
extern int apple_vfl_read_nand_pages(struct apple_vfl*,
		size_t _count, u16 _ces, page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob, const uint8_t * offset);
extern int apple_vfl_write_nand_pages(struct apple_vfl*,
		size_t _count, u16 _ces, page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob, const uint8_t * offset);
extern int apple_vfl_read_nand_page(struct apple_vfl*, u16 _ce,
		page_t _page, uint8_t *_data, uint8_t *_oob);
extern int apple_vfl_write_nand_page(struct apple_vfl*, u16 _ce,
        page_t _page, const uint8_t *_data, const uint8_t *_oob);
extern int apple_vfl_erase_nand_block(struct apple_vfl*, u16 _ce,
	    page_t _page);
extern int apple_vfl_read_page(struct apple_vfl*, page_t _page,
		uint8_t *_data, uint8_t *_oob);
extern int apple_vfl_write_page(struct apple_vfl*, page_t _page,
		const uint8_t *_data, const uint8_t *_oob);

extern int apple_legacy_vfl_detect(struct apple_vfl *_vfl);
extern int apple_vsvfl_detect(struct apple_vfl *_vfl);

static inline int apple_vfl_get(struct apple_vfl *_nd, int _id)
{
	if(!_nd)
		return 0;

	return _nd->get(_nd, _id);
}

static inline int apple_vfl_set(struct apple_vfl *_nd, int _id, int _val)
{
	if(!_nd)
		return -EINVAL;

	return _nd->set(_nd, _id, _val);
}

/**
 *  FTL Device Functions
 *
 *  @ingroup FTL
 */

struct apple_ftl
{
	struct apple_vfl *vfl;
	void *private;

	size_t block_size;

	int (*open)(struct apple_ftl *, struct apple_vfl *_vfl);
	void (*close)(struct apple_ftl *);

	int (*read_single_page)(struct apple_ftl *, uint32_t _page, uint8_t *_buffer);
	int (*write_single_page)(struct apple_ftl *, uint32_t _page, uint8_t *_buffer);

	int (*read)(page_t _page, int nPages, uint8_t *_buffer);

	int (*write)(page_t _page, int nPages, uint8_t *_buffer);
	void (*flush)(void);

	int (*get)(struct apple_ftl *, int _info);
	int (*set)(struct apple_ftl *, int _info, int _val);

};

extern int ftl_init(struct apple_ftl *_dev);
extern void ftl_cleanup(struct apple_ftl *_dev);

extern int ftl_register(struct apple_ftl *_dev);
extern void ftl_unregister(struct apple_ftl *_dev);

extern int ftl_open(struct apple_ftl *_dev,struct apple_vfl *_vfl);
extern void ftl_close(struct apple_ftl *_dev);

extern int ftl_read_single_page(struct apple_ftl *_dev, page_t _page, uint8_t *_buffer);
extern int ftl_write_single_page(struct apple_ftl *_dev, page_t _page, uint8_t *_buffer);

extern int ftl_detect(struct apple_ftl *_dev, struct apple_vfl *_vfl);
extern int apple_ftl_register(struct apple_ftl *_dev,struct apple_vfl *_vfl);

extern void YAFTL_Flush(void);
extern int ftl_yaftl_read_page(page_t _page, int nPages, uint8_t *_buffer);
extern int ftl_yaftl_write_page(page_t _page, int nPages, uint8_t *_buffer);
/*
extern error_t (*ftl_device_open_t)(struct apple_ftl *,struct  apple_vfl *_vfl);
extern void (*ftl_device_close_t)(struct apple_ftl *);

extern error_t (*ftl_read_single_page_t)(struct apple_ftl *, uint32_t _page, uint8_t *_buffer);
extern error_t (*ftl_write_single_page_t)(struct apple_ftl *, uint32_t _page, uint8_t *_buffer);
*/

typedef struct _emf_key {
	uint32_t length;
	uint8_t key[1];
} EMFKey;

typedef struct _lwvm_key {
	uint8_t unkn[32];
	uint64_t partition_uuid[2];
	uint8_t key[32];
} LwVMKey;

typedef struct _locker_entry {
	uint16_t locker_magic; // 'kL'
	uint16_t length;
	uint8_t identifier[4];
	uint8_t key[1];
} LockerEntry;

typedef struct _plog_struct {
	uint8_t header[0x38]; // header[0:16] XOR header[16:32] = ’ecaF’ + dw(0x1) + dw(0x1) + dw(0x0)
	uint32_t generation;
	uint32_t crc32; // headers + data

	LockerEntry locker;
} PLog;
extern void get_encryption_keys(struct apple_vfl *_vfl);


extern int iphone_block_probe(void);
extern int iphone_block(void);

#endif //_LINUX_APPLE_FLASH_H
