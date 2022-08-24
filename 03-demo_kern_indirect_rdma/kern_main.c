#include <linux/init.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "kern_rdma.h"
#include "kern_sg.h"
#include "common.h"

static ssize_t indirect_rdma_write(struct file *filep,
				const char __user *buf, size_t size, loff_t *loff) {
	int err = 0;
	struct write_param param;
	bool is_server;

	if(size != sizeof(struct write_param)) {
		err = -EINVAL;
		err_info("write size invalid\n");
		return err;
	}

	err = copy_from_user(&param, buf, size);
	if(err) {
		err_info("Failed to copy from user\n");
		return err;
	}

	is_server = (param.s_addr.sin_addr.s_addr == htonl(INADDR_ANY));
	err = kern_rdma_core(is_server, param.dev_name, &param.s_addr, 
						param.rdma_port, param.sgid_index,
						param.virtaddr, param.length);
	if(err) {
		err_info("Failed to execute indirect RDMA\n");
	}

	return (!err)? size: err;
}

static int indirect_rdma_release(struct inode *inode, struct file *filep) {
	kern_rdma_release();
	return 0;
}

static struct file_operations dev_fops = {
	.owner				= THIS_MODULE,
	.write				= indirect_rdma_write,
	.release			= indirect_rdma_release,
};

static struct miscdevice misc = {
	.minor			= MISC_DYNAMIC_MINOR,
	.name			= DEV_NAME,
	.fops			= &dev_fops,
	.mode			= 00666,
};

static int __init indirect_rdma_init(void) {
	int err = 0;

	err = misc_register(&misc);
	if(err) {
		err_info("misc_register error\n");
		return err;
	}

	err = init_ib_dev_list();
	if(err) {
		err_info("Failed to init ibdev list\n");
		goto err_init_list;
	}

	init_sg_tbl_list();
	
	return err;

err_init_list:
	misc_deregister(&misc);
	return err;
}

static void __exit indirect_rdma_exit(void) {
	destroy_ib_dev_list();
	misc_deregister(&misc);
}

module_init(indirect_rdma_init);
module_exit(indirect_rdma_exit);
MODULE_LICENSE("GPL");
