#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/page.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/mm_types.h>
#include <asm/io.h>
#include "common.h"

//#define max(a, b)	((a)>(b)?(a):(b))
//#define min(a, b)	((a)<(b)?(a):(b))
#define ARR_SIZE(arr)	(sizeof(arr)/sizeof(*arr))

static char array[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

static char *buffer;

static int my_open(struct inode *inode, struct file *file) {
	return 0;
}

static int my_mmap(struct file *filp, struct vm_area_struct *vma) {
	unsigned long start = (unsigned long)vma->vm_start;
	unsigned long size = (unsigned long)(vma->vm_end - vma->vm_start);
	unsigned long page;
	int i, err = 0;

	page = virt_to_phys(buffer);
	err = remap_pfn_range(vma, start, page >> PAGE_SHIFT, size, PAGE_SHARED);
	if(err) {
		err_info("remap_pfn_range error, err: %d\n", err);
		return err;
	}

	for(i = 0; i < ARR_SIZE(array); i++) {
		buffer[i] = array[i];
	}

	return err;
}

static struct file_operations dev_fops = {
	.owner		= THIS_MODULE,
	.open		= my_open,
	.mmap		= my_mmap,
};

static struct miscdevice misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DEVICE_NAME,
	.fops		= &dev_fops,
	.mode		= 00666,
};

static int __init dev_init(void) {
	int err = 0;

	err = misc_register(&misc);
	if(err) {
		err_info("misc_register error, err: %d\n", err);
		return err;
	}

	buffer = (char*)kzalloc(PAGE_SIZE, GFP_KERNEL);
	if(!buffer) {
		err = -ENOMEM;
		goto err_kzalloc;
	}
	mark_page_reserved(virt_to_page(buffer));
	return err;

err_kzalloc:
	misc_deregister(&misc);
	return err;
}

static void __exit dev_exit(void) {
	kfree(buffer);
	misc_deregister(&misc);
}

module_init(dev_init);
module_exit(dev_exit);
MODULE_LICENSE("GPL");
