#ifndef __COMMON_H__
#define __COMMON_H__

#define DEV_NAME									"demo_indirect_rdma"

#ifdef __COMPILE_KERNEL_CODE

#define dbg_info(fmt, args...)											\
	printk(KERN_NOTICE "In %s(%d): " fmt, __FILE__, __LINE__, ##args)

#define err_info(fmt, args...)											\
	printk(KERN_ERR "Err at %s(%d): " fmt, __FILE__, __LINE__, ##args)

#define kprintf(fmt, args...)			printk(KERN_NOTICE fmt, ##args)

#else
#include <error.h>

#define dbg_info(fmt, args...)											\
	printf("In %s(%d): " fmt, __FILE__, __LINE__, ##args)

#define err_info(err, fmt, args...)										\
	error_at_line(0, -err, __FILE__, __LINE__, fmt, ##args)

#endif

#define CHECK(cond)		({												\
	typeof(cond) ___r = (cond);											\
	dbg_info("CHECK (%s)? %s\n", #cond, ___r? "true": "false");			\
	___r;																\
})

#define PRINT(fmt, arg)		({											\
	typeof(arg) ___r = (arg);											\
	dbg_info("PRINT (%s): " fmt "\n", #arg, (typeof(arg))___r);			\
	___r;																\
})

#define WRAP_CODE(code)		do {										\
	dbg_info("Going to execute %s\n", #code);							\
	code;																\
	dbg_info("Finish executing %s\n", #code);							\
} while(0)

#define DEV_NAME_SIZE						50

#include <linux/in.h>
struct write_param {
	char					dev_name[DEV_NAME_SIZE];
	struct sockaddr_in		s_addr;
	int						rdma_port;
	int						sgid_index;
	unsigned long			virtaddr;
	unsigned long			length;
};

#endif
