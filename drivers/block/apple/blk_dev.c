#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/apple_flash.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <ftl/yaftl_common.h>
#include <linux/vmalloc.h>

// 2^9 = 512
#define SECTOR_SHIFT 9

static struct
{
	spinlock_t lock;
	struct gendisk* gd;
	struct block_device* bdev;
	struct request_queue* queue;
	int sectorSize;
	int pageShift;
	int majorNum;

	struct request* req;
	bool processing;
	u8* bounceBuffer;
} iphone_block_device;

static void ftl_workqueue_handler(struct work_struct* work);

DECLARE_WORK(ftl_workqueue, &ftl_workqueue_handler);
static struct workqueue_struct* ftl_wq;

static void iphone_block_scatter_gather(struct request* req, bool gather)
{
	unsigned int offset = 0;
	struct req_iterator iter;
	struct bio_vec *bvec;
	unsigned int i = 0;
	size_t size;
	void *buf;

	rq_for_each_segment(bvec, req, iter) {
		unsigned long flags;
		//printk("%s:%u: bio %u: %u segs %u sectors from %lu\n",
		//		__func__, __LINE__, i, bio_segments(iter.bio),
		//		bio_sectors(iter.bio), (unsigned long) iter.bio->bi_sector);

		size = bvec->bv_len;
		buf = bvec_kmap_irq(bvec, &flags);
		if (gather)
			memcpy(iphone_block_device.bounceBuffer + offset, buf, size);
		else
			memcpy(buf, iphone_block_device.bounceBuffer + offset, size);
		offset += size;
		flush_kernel_dcache_page(bvec->bv_page);
		bvec_kunmap_irq(bvec, &flags);
		i++;
	}

	//printk("scatter_gather total: %d / %d\n", offset, sGeometry.pagesPerSuBlk * sGeometry.bytesPerPage);

}

static void ftl_workqueue_handler(struct work_struct* work)
{
	unsigned long flags;
	bool dir_out;
	int ret;

	//printk("ftl_workqueue_handler enter\n");

	while(true)
	{
		u32 lpn;
		u32 numPages;
		u32 remainder;

		//printk("ftl_workqueue_handler loop\n");

		spin_lock_irqsave(&iphone_block_device.lock, flags);
		if(iphone_block_device.req == NULL || iphone_block_device.processing)
		{
			spin_unlock_irqrestore(&iphone_block_device.lock, flags);
			//printk("ftl_workqueue_handler exit\n");
			return;
		}

		iphone_block_device.processing = true;
		spin_unlock_irqrestore(&iphone_block_device.lock, flags);

		if(iphone_block_device.req->cmd_type == REQ_TYPE_FS)
		{
			lpn = blk_rq_pos(iphone_block_device.req) >> (iphone_block_device.pageShift - SECTOR_SHIFT);
			numPages = blk_rq_bytes(iphone_block_device.req) / iphone_block_device.sectorSize;
			remainder = numPages * iphone_block_device.sectorSize - blk_rq_bytes(iphone_block_device.req);

			if(remainder)
			{
				printk("iphone_block: requested not page aligned number of bytes (%d bytes)\n", blk_rq_bytes(iphone_block_device.req));
				blk_end_request_all(iphone_block_device.req, -EINVAL);
			} else
			{
				if(rq_data_dir(iphone_block_device.req))
				{
					dir_out = true;
					iphone_block_scatter_gather(iphone_block_device.req, true);
				} else
				{
					dir_out = false;
				}


				if(dir_out)
				{
					//printk("FTL_Write enter: %p\n", iphone_block_device.req);
					ret = ftl_yaftl_write_page(lpn, numPages, iphone_block_device.bounceBuffer);
					//printk("FTL_Write exit: %p\n", iphone_block_device.req);
				} else
				{
					//printk("FTL_Read enter: %p\n", iphone_block_device.req);
					ret = ftl_yaftl_read_page(lpn, numPages, iphone_block_device.bounceBuffer);
					//printk("FTL_Read exit: %p\n", iphone_block_device.req);
				}

				if(!dir_out)
				{
					iphone_block_scatter_gather(iphone_block_device.req, false);
				}

				blk_end_request_all(iphone_block_device.req, ret);
			}
		} else
		{
			blk_end_request_all(iphone_block_device.req, -EINVAL);
		}
		spin_lock_irqsave(&iphone_block_device.lock, flags);
		iphone_block_device.processing = false;
		iphone_block_device.req = blk_fetch_request(iphone_block_device.queue);
		spin_unlock_irqrestore(&iphone_block_device.lock, flags);
	}
}

static int iphone_block_busy(struct request_queue *q)
{
	int ret = (iphone_block_device.req == NULL) ? 0 : 1;
	printk("iphone_block_busy: %d\n", ret);
	return ret;
}

static int iphone_block_getgeo(struct block_device* bdev, struct hd_geometry* geo)
{
	long size = (sGeometry.pagesPerSublk * sGeometry.numBlocks) * (iphone_block_device.sectorSize >> SECTOR_SHIFT);

	geo->heads = 64;
	geo->sectors = 32;
	geo->cylinders = size / (geo->heads * geo->sectors);

	return 0;
}

static int iphone_block_open(struct block_device* bdev, fmode_t mode)
{
	return 0;
}

static int iphone_block_release(struct gendisk *disk, fmode_t mode)
{
	//ftl->flush();
	YAFTL_Flush();
	return 0;
}

static int iphone_block_ioctl(struct block_device *bdev, fmode_t mode, unsigned int cmd, unsigned long arg)
{
	switch(cmd)
	{
		case BLKFLSBUF:
			//ftl->flush();
			YAFTL_Flush();
			return 0;
		default:
			return -ENOTTY;
	}
}

static void iphone_block_request(struct request_queue* q)
{
	if(iphone_block_device.req)
	{
		//printk("not queueing request due to busy\n");
		return;
	}

	iphone_block_device.req = blk_fetch_request(q);

	//printk("scheduling work: %p\n", iphone_block_device.req);
	queue_work(ftl_wq, &ftl_workqueue);
}

static struct block_device_operations iphone_block_fops =
{
	.owner		= THIS_MODULE,
	.getgeo		= iphone_block_getgeo,
	.open		= iphone_block_open,
	.release	= iphone_block_release,
	.ioctl		= iphone_block_ioctl
};

int iphone_block_probe(struct apple_ftl *_ftl)
{
	int i;

	ftl_wq = create_workqueue("iphone_ftl_worker");

	for(i = 0; i < 31; ++i)
	{
		if((1 << i) == sGeometry.bytesPerPage)
		{
			iphone_block_device.pageShift = i;
			break;
		}
	}

	spin_lock_init(&iphone_block_device.lock);

	iphone_block_device.processing = false;
	iphone_block_device.req = NULL;
	iphone_block_device.bounceBuffer = (u8*) vmalloc(sGeometry.pagesPerSublk * sGeometry.bytesPerPage);
	if(!iphone_block_device.bounceBuffer)
		return -EIO;

	iphone_block_device.sectorSize = sGeometry.bytesPerPage;
	iphone_block_device.majorNum = register_blkdev(0, "nand");

	iphone_block_device.gd = alloc_disk(5);
	if(!iphone_block_device.gd)
		goto out_unregister;

	iphone_block_device.gd->major = iphone_block_device.majorNum;
	iphone_block_device.gd->first_minor = 0;
	iphone_block_device.gd->fops = &iphone_block_fops;
	iphone_block_device.gd->private_data = &iphone_block_device;
	strcpy(iphone_block_device.gd->disk_name, "nand0");

	iphone_block_device.queue = blk_init_queue(iphone_block_request, &iphone_block_device.lock);
	if(!iphone_block_device.queue)
		goto out_put_disk;

	blk_queue_lld_busy(iphone_block_device.queue, iphone_block_busy);
	blk_queue_bounce_limit(iphone_block_device.queue, BLK_BOUNCE_ANY);
	blk_queue_max_hw_sectors(iphone_block_device.queue, sGeometry.pagesPerSublk * (iphone_block_device.sectorSize >> SECTOR_SHIFT));
	blk_queue_max_segment_size(iphone_block_device.queue, sGeometry.pagesPerSublk * iphone_block_device.sectorSize);
	blk_queue_physical_block_size(iphone_block_device.queue, iphone_block_device.sectorSize);
	blk_queue_logical_block_size(iphone_block_device.queue, iphone_block_device.sectorSize);
	iphone_block_device.gd->queue = iphone_block_device.queue;

	set_capacity(iphone_block_device.gd, (sGeometry.pagesPerSublk * sGeometry.numBlocks) * (iphone_block_device.sectorSize >> SECTOR_SHIFT));
	add_disk(iphone_block_device.gd);

	printk("iphone-block: block device (SIZE :%d) registered with major num %d\n", (sGeometry.pagesPerSublk * sGeometry.numBlocks) * (iphone_block_device.sectorSize >> SECTOR_SHIFT), iphone_block_device.majorNum);

	return 0;

out_put_disk:
	put_disk(iphone_block_device.gd);

out_unregister:
	unregister_blkdev(iphone_block_device.majorNum, "nand");
	kfree(iphone_block_device.bounceBuffer);

	return -ENOMEM;
}




