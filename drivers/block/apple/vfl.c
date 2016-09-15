#include <linux/apple_flash.h>

int apple_legacy_vfl_detect(struct apple_vfl *_vfl) {
	printk(KERN_ERR "apple-flash: legacy VFL not implemented!\n");
	return -42;
}
