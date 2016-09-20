#include <linux/module.h>
#include <linux/slab.h>
#include <linux/apple_flash.h>
#include <linux/delay.h>
#include <mach/clock.h>
#include <plat/cdma.h>

//
// NAND
//

int h2fmi_ftl_count = 0;
int h2fmi_ftl_databuf = 0;
int h2fmi_ftl_smth[2] = {0, 0};
int h2fmi_emf = 0;
int h2fmi_emf_iv_input = 0;
uint8_t DKey[32];
uint8_t EMF[32];

int apple_nand_special_page(struct apple_nand *_nd, u16 _ce, char _page[16],
		uint8_t* _buffer, size_t _amt)
{
	int pagesz = _nd->get(_nd, NAND_PAGE_SIZE);
	int bpce = _nd->get(_nd, NAND_BLOCKS_PER_CE);
	int bpb = _nd->get(_nd, NAND_BLOCKS_PER_BANK);
	int ppb = _nd->get(_nd, NAND_PAGES_PER_BLOCK);
	int baddr = _nd->get(_nd, NAND_BANK_ADDRESS_SPACE);
	int oobsz = _nd->get(_nd, NAND_OOB_ALLOC);
	u8 *buf, *oob;
	int lowestBlock = bpce - (bpce/10);
	int block;
	int ret;
	int retval;

	int doAes = -1;
	struct cdma_aes aes;
	buf = kmalloc(pagesz, GFP_DMA32);
	oob = kmalloc(oobsz, GFP_DMA32);

	for(block = bpce-1; block >= lowestBlock; block--)
	{
		int page;
		int badCount = 0;

		int realBlock = (block/bpb) * baddr	+ (block % bpb);
		for(page = 0; page < ppb; page++)
		{
			if(badCount > 2)
			{
				printk(KERN_INFO "vfl: read_special_page - too many bad pages, skipping block %d\r\n", block);
							break;
						}

			if((doAes = _nd->default_aes(_nd, &aes, 1)) >= 0){
				//printk(KERN_INFO "AESed\n");
				_nd->aes(_nd, &aes);
			}
			memset(buf,0,pagesz);


			ret = apple_nand_read_page(_nd, _ce,
					(realBlock * ppb) + page, buf, oob);
			//printk(KERN_INFO "realBlock%x, ppb%x, page%x, fine:%x\n",realBlock,ppb,page,(realBlock * ppb) + page);

			if(ret < 0)
			{
				if(ret != -ENOENT){
					printk(KERN_INFO "vfl: read_special_page - found 'badBlock' on ce %d, page %d\r\n", _ce, (block * ppb) + page);
					badCount++;
				}
				printk(KERN_INFO "vfl: read_special_page - skipping ce %d, page %d\r\n", _ce, (block * ppb) + page);

				continue;
			}

			/*{
				int sp = 1, i;
				for(i = 0; i < 16; i++)
				{
					if(!buf[i] || (buf[i] >= 'A' && buf[i] <= 'Z'))
						continue;
					
					sp = 0;
					break;
				}
				}

				if(sp)
					print_hex_dump(KERN_INFO, "P: ", DUMP_PREFIX_OFFSET, 16,
								   1, buf, 0x400, true);
				printk("-------------------------------\n");
				msleep(3000);
*/
				
			if(memcmp(buf, _page, sizeof(_page)) == 0)
			{
				if(_buffer)
				{
					size_t size = min(((size_t)((size_t*)buf)[13]), _amt);
					memcpy(_buffer, buf + 0x38, size);
				}

				retval = 0;
				goto exit;
			}
		}
	}

	printk(KERN_ERR "apple_nand: failed to find special page %s.\n", _page);
	retval = -ENOENT;

 exit:
	kfree(buf);
	kfree(oob);
	if(doAes >= 0)
		_nd->aes(_nd, NULL);
	return retval;
}
EXPORT_SYMBOL_GPL(apple_nand_special_page);

int apple_nand_read_page(struct apple_nand *_nd, u16 _ce, page_t _page,
		uint8_t *_data, uint8_t *_oob)
{
	struct scatterlist sg_buf, sg_oob;

	if(!_nd->read)
		return -EPERM;

	sg_init_one(&sg_buf, _data, _nd->get(_nd, NAND_PAGE_SIZE));
	sg_init_one(&sg_oob, _oob, _nd->get(_nd, NAND_OOB_ALLOC));

	return _nd->read(_nd, 1, &_ce, &_page, &sg_buf, _data ? 1 : 0,
					 &sg_oob, _oob ? 1 : 0);
}
EXPORT_SYMBOL_GPL(apple_nand_read_page);

int apple_nand_write_page(struct apple_nand *_nd, u16 _ce, page_t _page,
		const uint8_t *_data, const uint8_t *_oob)
{
	struct scatterlist sg_buf, sg_oob;

	if(!_nd->write)
		return -EPERM;

	sg_init_one(&sg_buf, _data, _nd->get(_nd, NAND_PAGE_SIZE));
	sg_init_one(&sg_oob, _oob, _nd->get(_nd, NAND_OOB_ALLOC));

	return _nd->write(_nd, 1, &_ce, &_page, &sg_buf, _data ? 1 : 0,
					  &sg_oob, _oob ? 1 : 0);
}
EXPORT_SYMBOL_GPL(apple_nand_write_page);

int apple_nand_erase_block(struct apple_nand *_nd, u16 _ce, page_t _page)
{
	if(!_nd->erase)
		return -EPERM;

	return _nd->erase(_nd, 1, &_ce, &_page);
}
EXPORT_SYMBOL_GPL(apple_nand_erase_block);

int apple_nand_register(struct apple_nand *_nd, struct apple_vfl *_vfl, struct device *_dev)
{
	printk(KERN_INFO "%s: %p, %p, %p\n", __func__, _nd, _vfl, _dev);

	if(_nd->vfl)
		panic("apple_nand: tried to register a NAND device more than once.\n");

	_nd->device = get_device(_dev);
	if(!_nd->device)
	{
		printk(KERN_ERR "apple_nand: tried to register dying device!\n");
		return -ENOENT;
	}

	if(_vfl->num_devices >= _vfl->max_devices)
		panic("apple_nand: tried to register more devices than we said we would!\n");

	_nd->vfl = _vfl;
	_nd->index = _vfl->num_devices;
	_vfl->num_devices++;
	_vfl->devices[_nd->index] = _nd;
	return 0;
}
EXPORT_SYMBOL_GPL(apple_nand_register);

void apple_nand_unregister(struct apple_nand *_nd)
{
	if(!_nd->vfl)
	{
		printk(KERN_WARNING "apple_nand: tried to unregister non-registered nand!\n");
		return;
	}

	// TODO: signal vfl layer!

	put_device(_nd->device);
	_nd->vfl->devices[_nd->index] = NULL;
	_nd->vfl = NULL;
}
EXPORT_SYMBOL_GPL(apple_nand_unregister);

void set_ftl_region(int _lpn, int _a2, int _count, void* _buf) {
	h2fmi_ftl_count = _count;
	h2fmi_ftl_databuf = (uint32_t) _buf;
	h2fmi_ftl_smth[0] = _lpn;
	h2fmi_ftl_smth[1] = _a2;
}
EXPORT_SYMBOL_GPL(set_ftl_region);

void h2fmi_set_emf(int enable, int iv_input) {
	h2fmi_emf = enable;
	if (iv_input)
		h2fmi_emf_iv_input = iv_input;
}
EXPORT_SYMBOL_GPL(h2fmi_set_emf);

int h2fmi_get_emf() {
	return h2fmi_emf;
}
EXPORT_SYMBOL_GPL(h2fmi_get_emf);

//
// VFL
//

int apple_vfl_special_page(struct apple_vfl *_vfl,
						   u16 _ce, char _page[16],
						   uint8_t* _buffer, size_t _amt)
{
	struct apple_chip_map *chip = &_vfl->chips[_ce];
	struct apple_nand *nand = _vfl->devices[chip->bus];
	return apple_nand_special_page(nand, chip->chip,
								   _page, _buffer, _amt);
}

int apple_vfl_read_nand_pages(struct apple_vfl *_vfl,
		size_t _count, u16 _ces, page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob, const uint8_t* offset)
{
	//FIXME Check whether this is all on one bus, if so, just pass-through

	int ret, ok = 1;
	//int ce, i;

	if(!_count)
		return 0;

	/*ce = _ces[0];


	bus = _vfl->chips[ce].bus;
	for(i = 1; i < _count; i++)
	{
		if(_vfl->chips[_ces[i]].bus != bus)
		{
			ok = 0;
			break;
		}
	}*/

	if(ok)
	{
		// only one bus, pass it along!
		struct apple_chip_map *chip = &_vfl->chips[_ces];
		int bus = chip->bus;
		struct apple_nand *nand = _vfl->devices[bus];
		/*u16 *chips = kmalloc(sizeof(*chips)*_count, GFP_KERNEL);
		if(!chips)
			return -ENOMEM;

		//struct apple_chip_map *chip = &_vfl->chips[_ces];
		int doAes = -1;
			struct cdma_aes aes;

		//struct apple_chip_map *chip = &_vfl->chips[_ces];
		//struct apple_nand *nand = _vfl->devices[chip->bus];*/

		nand->setup_aes(nand, 1, 0, offset);
		ret = nand->read(nand, _count, &chip->chip, _pages,
						 _sg_data, _sg_num_data,
						 _sg_oob, _sg_num_oob);


		//kfree(chips);
		return ret;
	}
	else
	{
/*
		int nd = _vfl->num_devices;
		u16 *chips = kmalloc(sizeof(*chips)*_count*nd, GFP_KERNEL);
		page_t *pages = kmalloc(sizeof(*pages)*_count*nd, GFP_KERNEL);
		int *count = kzalloc(sizeof(*count)*nd, GFP_KERNEL);
		int num_bus = 0;

		if(!chips || !pages || !count)
		{
			kfree(chips);
			kfree(pages);
			kfree(count);
			return -ENOMEM;
		}

		for(i = 0; i < _count; i++)
		{
			int ce = _ces[i];
			int bus = _vfl->chips[ce].bus;
			int realCE = _vfl->chips[ce].chip;
			
			int idx = count[bus]++;
			chips[bus*_count + idx] = realCE;
			pages[bus*_count + idx] = _pages[i];
		}

		ret = 0;
		for(i = 0; i < nd; i++)
		{
			struct apple_nand *nand = _vfl->devices[i];

			if(!count[i])
				continue;
			
			ret = nand->read(nand, count[i],
							 &chips[i*_count],
							 &pages[i*_count],
							 
							 // ARGH, NEED TO SPLIT SGs!
		}
*/
		panic("apple-flash: SG splitting not yet implemented!\n");
	}
}
EXPORT_SYMBOL_GPL(apple_vfl_read_nand_pages);

int apple_vfl_write_nand_pages(struct apple_vfl *_vfl,
		size_t _count, u16 _ces, page_t *_pages,
		struct scatterlist *_sg_data, size_t _sg_num_data,
		struct scatterlist *_sg_oob, size_t _sg_num_oob, const uint8_t * offset)
{
	// Check whether this is all on one bus, if so, just pass-through

	int ret, ok = 1;
	//int ce, i;

	if(!_count)
		return 0;

	/*ce = _ces[0];
	bus = _vfl->chips[ce].bus;
	for(i = 1; i < _count; i++)
	{
		if(_vfl->chips[_ces[i]].bus != bus)
		{
			ok = 0;
			break;
		}
	}*/

	if(ok)
	{
		// only one bus, pass it along!

		/*struct apple_nand *nand = _vfl->devices[bus];
		u16 *chips = kmalloc(sizeof(*chips)*_count, GFP_KERNEL);
		if(!chips)
			return -ENOMEM;*/
		struct apple_chip_map *chip = &_vfl->chips[_ces];
		int bus = chip->bus;
		struct apple_nand *nand = _vfl->devices[bus];
		nand->setup_aes(nand, 1, 1, offset);

		ret = nand->write(nand, _count, &chip->chip, _pages,
						 _sg_data, _sg_num_data,
						 _sg_oob, _sg_num_oob);
		//kfree(chips);
		return ret;
	}
	else
		panic("apple-flash: SG splitting not implemented!\n");
}
EXPORT_SYMBOL_GPL(apple_vfl_write_nand_pages);

int apple_vfl_read_nand_page(struct apple_vfl *_vfl, u16 _ce,
		page_t _page, uint8_t *_data, uint8_t *_oob)
{
	struct scatterlist sg_buf, sg_oob;

	sg_init_one(&sg_buf, _data, _vfl->get(_vfl, NAND_PAGE_SIZE));
	sg_init_one(&sg_oob, _oob, _vfl->get(_vfl, NAND_OOB_ALLOC));

	return apple_vfl_read_nand_pages(_vfl, 1, _ce, &_page,
					&sg_buf, _data ? 1 : 0,
					&sg_oob, _oob ? 1 : 0, _data);
}
EXPORT_SYMBOL_GPL(apple_vfl_read_nand_page);

int apple_vfl_write_nand_page(struct apple_vfl *_vfl, u16 _ce,
        page_t _page, const uint8_t *_data, const uint8_t *_oob)
{
	struct scatterlist sg_buf, sg_oob;

	sg_init_one(&sg_buf, _data, _vfl->get(_vfl, NAND_PAGE_SIZE));
	sg_init_one(&sg_oob, _oob, _vfl->get(_vfl, NAND_OOB_ALLOC));

	return apple_vfl_write_nand_pages(_vfl, 1, _ce,
					&_page, &sg_buf, _data ? 1 : 0,
					&sg_oob, _oob ? 1 : 0, _data);
}
EXPORT_SYMBOL_GPL(apple_vfl_write_nand_page);

int apple_vfl_read_page(struct apple_vfl *_vfl, page_t _page,
		uint8_t *_data, uint8_t *_oob)
{
	struct scatterlist sg_buf, sg_oob;

	if(!_vfl->read)
		return -EPERM;

	sg_init_one(&sg_buf, _data, _vfl->get(_vfl, NAND_PAGE_SIZE));
	sg_init_one(&sg_oob, _oob, _vfl->get(_vfl, NAND_OOB_ALLOC));

	return _vfl->read(_vfl, 1, &_page, &sg_buf, _data ? 1 : 0,
					  &sg_oob, _oob ? 1 : 0);
}
EXPORT_SYMBOL_GPL(apple_vfl_read_page);

int apple_vfl_write_page(struct apple_vfl *_vfl, page_t _page,
		const uint8_t *_data, const uint8_t *_oob)
{
	struct scatterlist sg_buf, sg_oob;

	if(!_vfl->write)
		return -EPERM;

	sg_init_one(&sg_buf, _data, _vfl->get(_vfl, NAND_PAGE_SIZE));
	sg_init_one(&sg_oob, _oob, _vfl->get(_vfl, NAND_OOB_ALLOC));

	return _vfl->write(_vfl, 1, &_page, &sg_buf, _data ? 1 : 0,
					   &sg_oob, _oob ? 1 : 0);
}
EXPORT_SYMBOL_GPL(apple_vfl_write_page);

void apple_vfl_init(struct apple_vfl *_vfl)
{
	_vfl->devices = kzalloc(GFP_KERNEL, sizeof(*_vfl->devices)*_vfl->max_devices);

	if(!_vfl->max_chips)
		_vfl->max_chips = _vfl->max_devices*8;

	_vfl->chips = kzalloc(GFP_KERNEL, sizeof(*_vfl->chips)*_vfl->max_chips);
}
EXPORT_SYMBOL_GPL(apple_vfl_init);

int apple_vfl_register(struct apple_vfl *_vfl, enum apple_vfl_detection _detect)
{
	int i, ret;
	u8 sigbuf[264];
	u32 flags;
	int count[_vfl->num_devices];
	u16 total = 0;
	int bus;
	int chip = 0;

	struct apple_chip_map *dc = &_vfl->chips[0];

	// Setup Chip Map
	if(!_vfl->num_devices)
	{
		printk(KERN_WARNING "apple-flash: no devices!\n");
		return -ENOENT;
	}
	
	printk("apple-flash: num_devices:%x !\n",_vfl->num_devices);

	//FIXME This algo is wrong, fix it.

		for (i = 0; i < _vfl->num_devices; i++) {
			struct apple_nand *nand = _vfl->devices[i];
			int countx = apple_nand_get(nand, NAND_NUM_CE);
			count[i] = countx * i;
		}

		for(bus = 0; bus < _vfl->num_devices; bus++){
			struct apple_nand *nand = _vfl->devices[bus];
			int countx = apple_nand_get(nand, NAND_NUM_CE);
			total += countx;
		}
		_vfl->num_chips += total;
		for(chip = 0; chip < total;)
		{
			for(bus = 0; bus < _vfl->num_devices; bus++)
			{
				struct apple_nand *nand = _vfl->devices[bus];
				int bmap = apple_nand_get(nand, NAND_BITMAP);
				//printk("bmap :%x\n",bmap);

				if((bus == 0 ? bmap : bmap*100) & (1 << count[bus]))
				{
					struct apple_chip_map *e = &_vfl->chips[chip];
					e->bus = bus;
					e->chip = (count[bus] > 1 ? count[bus]-2 : count[bus]);
					printk("chip :%x,bus: %x \n",e->chip,bus);

					chip++;
				}

				count[bus]++;
			}
		}
/*:OLD algo
	for(i = 0; i < _vfl->num_devices; i++)
	{
		struct apple_nand *nand = _vfl->devices[i];
		int count = apple_nand_get(nand, NAND_NUM_CE);
		int bmap = apple_nand_get(nand, NAND_BITMAP);
		int j;
		u16 idx = 0;
		printk("apple-flash: count :%x num_chips:%x !\n",count,_vfl->num_chips);

		for(j = 0; j < count; j++)
		{
			struct apple_chip_map *map = &_vfl->chips[j+_vfl->num_chips];

			while((bmap & 1) == 0)
			{
				if(!bmap)
					panic("number of bits in bitmap wrong!\n");

				idx++;
				bmap >>= 1;
			}

			map->bus = i;
			map->chip = idx;
			printk("chip :%x,bus :%x CHIP:%x \n",idx,i,j+_vfl->num_chips);

		}

		_vfl->num_chips += count;
	}
*/
	printk(KERN_INFO "%s: detecting VFL on (%d, %u)...\n", __func__, dc->bus, dc->chip);

	// Detect VFL type
	switch(_detect)
	{
	case APPLE_VFL_OLD_STYLE:
		// TODO: implement this! -- Ricky26
		panic("apple-flash: old style VFL detection not implemented!\n");
		break;

	case APPLE_VFL_NEW_STYLE:
		ret = apple_nand_special_page(_vfl->devices[dc->bus], dc->chip,
									  "NANDDRIVERSIGN\0\0",
									  sigbuf, sizeof(sigbuf));
		break;

	default:
		ret = -EINVAL;
	};

	if(ret < 0)
	{
		printk(KERN_WARNING "apple-flash: failed to find VFL signature.\n");
		return ret;
	}

	// TODO: implement signature checking!

	printk(KERN_INFO "%s: checking signature.\n", __func__);

	flags = *(u32*)&sigbuf[4];
	if((_detect & 0x800)
	   && (!(flags & 0x10000) ||
		   ((_detect >> 10) & 1) ||
		   !((!(flags & 0x10000)) & ((_detect >> 10) & 1))))
	{
		printk(KERN_WARNING "apple-flash: metadata whitening mismatch!\n");
	}

	if(sigbuf[1] == '1')
	{
		// VSVFL
#ifdef CONFIG_BLK_DEV_APPLE_VSVFL
		ret = apple_vsvfl_detect(_vfl);
#else
		printk(KERN_ERR "apple-flash: detected VSVFL, but no support!\n");
		return -EINVAL;
#endif
	}
	else if(sigbuf[1] == '0')
	{
		// VFL
#ifdef CONFIG_BLK_DEV_APPLE_LEGACY_VFL
		ret = apple_legacy_vfl_detect(_vfl);
#else
		printk(KERN_ERR "apple-flash: detected legacy VFL, but no support!\n");
		return -EINVAL;
#endif
	}
	else
	{
		// Huh?
		printk(KERN_ERR "apple-flash: couldn't detect any VFL!\n");
		return -ENOENT;
	}

	if(ret < 0)
	{
		printk(KERN_ERR "apple-flash: failed to detect VFL.\n");
		return ret;
	}
	//ret = apple_ftl_register(&ftl,_vfl);
	if (ret)
			return  ret;

	return 0;
}
EXPORT_SYMBOL_GPL(apple_vfl_register);

void apple_vfl_unregister(struct apple_vfl *_vfl)
{
	kfree(_vfl->chips);
	kfree(_vfl->devices);
}
EXPORT_SYMBOL_GPL(apple_vfl_unregister);

//
// FTL
//

int ftl_init(struct apple_ftl *_dev)
{
	//_dev->open = FALSE;

	/*memcpy(&_dev->mtd, &ftl_mtd, sizeof(ftl_mtd));
	int ret = mtd_init(&_dev->mtd);
	if(FAILED(ret))
		return ret;
*/
	return SUCCESS;
}

void ftl_cleanup(struct apple_ftl *_dev)
{
	//mtd_cleanup(&_dev->mtd);
}

int ftl_register(struct apple_ftl *_dev)
{
	/*int ret = mtd_register(&_dev->mtd);
	if(FAILED(ret))
		return ret;
*/
	return SUCCESS;
}

void ftl_unregister(struct apple_ftl *_dev)
{
	//mtd_unregister(&_dev->mtd);
}

int ftl_read_single_page(struct apple_ftl *_dev, page_t _page, uint8_t *_buffer)
{
	if(_dev->read_single_page)
		return _dev->read_single_page(_dev, _page, _buffer);

	return ENOENT;
}

int ftl_write_single_page(struct apple_ftl *_dev, page_t _page, uint8_t *_buffer)
{
	if(_dev->write_single_page)
		return _dev->write_single_page(_dev, _page, _buffer);

	return ENOENT;
}

int apple_ftl_register(struct apple_ftl *_dev,struct apple_vfl *_vfl)
{

	int ret = ftl_detect(_dev,_vfl);

	if(ret < 0)
	{
		printk(KERN_ERR "apple-flash: failed to detect FTL.\n");
		return ret;
	}

	return SUCCESS;
}
EXPORT_SYMBOL_GPL(apple_ftl_register);

//
//Keys
//

static const unsigned char default_iv[] = { 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, };

static const uint8_t Gen835[] = {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01};
static const uint8_t Gen89B[] = {0x18, 0x3E, 0x99, 0x67, 0x6B, 0xB0, 0x3C, 0x54, 0x6F, 0xA4, 0x68, 0xF5, 0x1C, 0x0C, 0xBD, 0x49};
static const uint8_t Gen836[] = {0x00, 0xE5, 0xA0, 0xE6, 0x52, 0x6F, 0xAE, 0x66, 0xC5, 0xC1, 0xC6, 0xD4, 0xF1, 0x6D, 0x61, 0x80};
static const uint8_t Gen838[] = {0x8C, 0x83, 0x18, 0xA2, 0x7D, 0x7F, 0x03, 0x07, 0x17, 0xD2, 0xB8, 0xFC, 0x55, 0x14, 0xF8, 0xE1};

static uint8_t Key835[16];
static uint8_t Key89B[16];
static uint8_t Key836[16];
static uint8_t Key838[16];

void aes_encrypt(void* data, int size, int keyType, void* key, void* iv)
{
	//doAES(0x10, data, size, keyType, key, keyLen, iv);
	uint8_t* buff = NULL;
	memcpy(buff, data, size);
	aes_crypto_cmd(0x10, buff, buff, size, keyType, key, iv);
	memcpy(data, buff, size);
}

void aes_decrypt(void* data, int size, int keyType, void* key, void* iv)
{
	//doAES(0x11, data, size, keyType, key, keyLen, iv);
	uint8_t* buff = NULL;
	memcpy(buff, data, size);
	aes_crypto_cmd(0x11, buff, buff, size, keyType, key, iv);
	memcpy(data, buff, size);
}

int aes_setup(void) {
	memcpy(Key835, Gen835, 16);
	aes_encrypt(Key835, 16, 513 | (2 << 28), NULL, NULL);

	memcpy(Key89B, Gen89B, 16);
	aes_encrypt(Key89B, 16, 513 | (2 << 28), NULL, NULL);

	memcpy(Key836, Gen836, 16);
	aes_encrypt(Key836, 16, 513 | (2 << 28), NULL, NULL);

	memcpy(Key838, Gen838, 16);
	aes_encrypt(Key838, 16, 513 | (2 << 28), NULL, NULL);

	return 0;
}

int aes_wrap_key(uint8_t *_key, int _keyLen, const uint8_t *_iv,
				uint8_t *_out, const uint8_t *_in, uint32_t _inLen)
{
	int j, i;
	uint8_t A[16];

	if(!_iv)
		_iv = default_iv;
	*(long long *)A = *(long long *)_iv;

	memcpy(_out + 8, _in, _inLen);

	for(j = 0; j <= 5; j++)
	{
		uint32_t xor = (_inLen*j)/8;
		uint8_t *t = (uint8_t*)&xor;

		for(i = 1; i <= _inLen/8; i++)
		{
			long long *R = (long long*)(_out + i*8);
			xor++;

			memcpy(A+8, R, 8);

			aes_encrypt(A, sizeof(A), _keyLen, _key, NULL);

			A[4] ^= t[3];
			A[5] ^= t[2];
			A[6] ^= t[1];
			A[7] ^= t[0];

			memcpy(R, A+8, 8);
		}
	}

	memcpy(_out, A, 8);
	return _inLen + 8;
}

int aes_unwrap_key(uint8_t *_key, int _keyLen, const uint8_t *_iv,
				uint8_t *_out, const uint8_t *_in, uint32_t _inLen)
{
	int j, i;
	uint8_t A[16];

	_inLen -= 8;
	memcpy(A, _in, 8);
	memcpy(_out, _in + 8, _inLen);

	for(j = 0; j <= 5; j++)
	{
		uint32_t xor = (6 - j) * (_inLen/8);
		uint8_t *t = (uint8_t*)&xor;

		for(i = 1; i <= _inLen/8; i++)
		{
			long long *R = (long long*)(_out + _inLen - (i*8));

			if(xor > 0xFF) {
				A[4] ^= t[3];
				A[5] ^= t[2];
				A[6] ^= t[1];
			}
			A[7] ^= t[0];

			memcpy(A+8, R, 8);

			aes_decrypt(A, sizeof(A), _keyLen, _key, NULL);

			memcpy(R, A+8, 8);

			xor--;
		}
	}

	if(!_iv)
		_iv = default_iv;
	if(memcmp(_out, _iv, 8) != 0)
		return 0; // If IV doesn't match result, we failed!

	return _inLen;
}

void aes_835_unwrap_key(void* outBuf, void* inBuf, int size, void* iv) {
	aes_unwrap_key(Key835, CDMA_AES_128, iv, outBuf, inBuf, size);
}

void aes_89B_decrypt(void* data, int size, void* iv)
{
	aes_decrypt(data, size, CDMA_AES_128, Key89B, iv);
}

void get_encryption_keys(struct apple_vfl *_vfl) {
#ifndef CONFIG_MACH_IPAD_1G
	uint8_t* buffer = kmalloc(_vfl->get(_vfl, NAND_PAGE_SIZE),GFP_DMA32);
	PLog* plog = (PLog*)buffer;
	LockerEntry* locker;
	uint32_t ce, dev;
	uint32_t page = _vfl->get(_vfl, NAND_PAGES_PER_BLOCK) + 16;
	uint8_t emf_found = 0;
	uint8_t dkey_found = 0;
	EMFKey* emf;
	LwVMKey* lwvmkey;
	for (dev = 0; dev < _vfl->num_devices; dev++){
	for (ce = 0; ce < _vfl->get_device(_vfl, dev)->get(_vfl->get_device(_vfl, dev), NAND_NUM_CE); ce++) {
		apple_nand_read_page(_vfl->get_device(_vfl, dev), ce, page, buffer, NULL);
		if(plog->locker.locker_magic == 0x4c6b) // 'kL'
			break;
		if(ce == _vfl->get(_vfl, NAND_NUM_CE) - 1) {
			kfree(buffer);
			return;
		}
	}
	}
	locker = &plog->locker;
#else
	//FIXME
	mtd_t *imagesDevice = NULL;
	mtd_t *dev = NULL;
	while((dev = mtd_find(dev)))
	{
		if(dev->usage == mtd_boot_images)
		{
			imagesDevice = dev;
			break;
		}
	}
	if(!imagesDevice)
		return;
	dev = imagesDevice;

	LockerEntry* locker = NULL;

	mtd_prepare(dev);
	uint8_t* buffer = malloc(0x2000);
	mtd_read(dev, buffer, 0xFA000, 0x2000);
	mtd_finish(dev);
	uint32_t generation = 0;
	uint32_t i;
	for(i = 0; i < 0x2000; i += 0x400) {
		PLog* plog = (PLog*)(buffer+i);
		if(plog->locker.locker_magic == 0xffff)
			continue;
		if(plog->locker.locker_magic != 0x4c6b) // 'kL'
			continue;
		if(generation < plog->generation) {
			generation = plog->generation;
			locker = &plog->locker;
		}
	}
	if(!locker) {
		free(buffer);
		return;
	}
#endif
	printk("h2fmi: Found Plog\r\n");
	aes_setup();

	memset(EMF, 0, sizeof(EMF));
	memset(DKey, 0, sizeof(DKey));

	while(true) {
		if(locker->length == 0 || (dkey_found && emf_found))
			break;

		if(!memcmp(locker->identifier, "yek", 3)) {
			dkey_found = 1;
			printk("h2fmi: Found Dkey\r\n");
			aes_835_unwrap_key(DKey, locker->key, locker->length, NULL);
		}

		if(!memcmp(locker->identifier, "!FM", 3)) {
			emf_found = 1;
			printk("h2fmi: Found EMF\r\n");
			emf = (EMFKey*)(locker->key);
			memcpy((uint8_t*)EMF, emf->key, emf->length);
			aes_89B_decrypt(EMF, sizeof(EMF), NULL);
		}
		// Does only work when there's only one encrypted partition.
		if(!memcmp(locker->identifier, "MVwL", 4)) {
			emf_found = 1;
			printk("h2fmi: Found LwVM\r\n");
			aes_89B_decrypt(locker->key, locker->length, NULL);
			lwvmkey = (LwVMKey*)locker->key;
			memcpy(EMF, lwvmkey->key, sizeof(EMF));
		}

		locker = (LockerEntry*)(((uint8_t*)locker->key)+(locker->length));
	}
	kfree(buffer);
}

//
// Block
//

int iphone_block()
{

	int ret = iphone_block_probe();

	if(ret < 0)
	{
		printk(KERN_ERR "apple-flash: Block_dev failed.\n");
		return ret;
	}

	return SUCCESS;
}
EXPORT_SYMBOL_GPL(iphone_block);

MODULE_AUTHOR("Ricky Taylor <rickytaylor26@gmail.com>");
MODULE_DESCRIPTION("API for Apple Mobile Device NAND.");
MODULE_LICENSE("GPL");
