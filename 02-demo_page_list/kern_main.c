#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/highmem.h>
#include "common.h"

static struct page **page_list;
static size_t npages;

static ssize_t find_pgl_write(struct file *filep, const char __user *buf,
					size_t size, loff_t *loff) {
	struct write_param addr_param;
	int *kvaddr = NULL;
	unsigned long virtaddr;
	size_t length;
	int i;
	unsigned long page_idx;
	int err = 0;

	if(size != sizeof(struct write_param)) {
		err = -EINVAL;
		err_info("write size invalid\n");
		return err;
	}

	err = copy_from_user(&addr_param, buf, size);
	if(err) {
		err_info("error occurs when copying from user\n");
		return err;
	}

	virtaddr = addr_param.addr;
	length = addr_param.length;
	err = get_pagelist_and_pin(virtaddr, length, &page_list, &npages);
	if(err) {
		err_info("Failed to get pagelist\n");
		return err;
	}

	kprintf("i""\t""\t""virtaddr""\t""off""\t""\t""pg_idx""\t""pg_off""\t""virtaddr[i]\n");
	for(i = 0; i < length/sizeof(typeof(*kvaddr)); i++) {
		unsigned long page_off = get_page_off(virtaddr, i*sizeof(typeof(*kvaddr)));
		page_idx = get_page_idx(virtaddr, i*sizeof(typeof(*kvaddr)));

		if(i == 0 ||
				ALIGN(virtaddr+i*sizeof(typeof(*kvaddr)), PAGE_SIZE)
								== (virtaddr+i*sizeof(typeof(*kvaddr)))) {
			if(page_idx > 0) {
				kunmap(page_list[page_idx-1]);
			}
			kvaddr = (typeof(kvaddr))kmap(page_list[page_idx]);
			if(!kvaddr) {
				err = -EFAULT;
				err_info("Failed to map physical page\n");
				return err;
			}
		}

		kprintf("%d""\t\t""0x%x""\t""0x%03x""\t""%u""\t\t""0x%03x""\t""%d\n",
						i, virtaddr, i*sizeof(typeof(*kvaddr)), page_idx,
						page_off, kvaddr[page_off/sizeof(typeof(*kvaddr))]);
	}
	kunmap(page_list[page_idx]);

	return (!err)? size: err;
}

static int find_pgl_release(struct inode *inode, struct file *filep) {
	free_page_list(page_list, npages);
	return 0;
}

static struct file_operations dev_fops = {
	.owner			= THIS_MODULE,
	.write			= find_pgl_write,
	.release		= find_pgl_release,
};

static struct miscdevice misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name		= DEV_NAME,
	.fops		= &dev_fops,
	.mode		= 00666,
};

static int __init find_pgl_init(void) {
	int err = 0;

	err = misc_register(&misc);
	if(err) {
		err_info("misc_register error\n");
		return err;
	}

	return err;
}

static void __exit find_pgl_exit(void) {
	misc_deregister(&misc);
}

module_init(find_pgl_init);
module_exit(find_pgl_exit);
MODULE_LICENSE("GPL");
