#ifndef __COMMON_H__
#define __COMMON_H__

#define DEV_NAME									"demo_find_pagelist"

#ifdef __COMPILE_KERNEL_CODE

#define dbg_info(fmt, args...)											\
	printk(KERN_NOTICE "In %s(%d): " fmt, __FILE__, __LINE__, ##args)

#define err_info(fmt, args...)											\
	printk(KERN_ERR "Err at %s(%d): " fmt, __FILE__, __LINE__, ##args)

#define kprintf(fmt, args...)			printk(KERN_NOTICE fmt, ##args)

extern int get_pagelist_and_pin(unsigned long virt_addr, size_t length,
		struct page ***p_page_list, unsigned long *p_npages);

extern void free_page_list(struct page **pagelist, unsigned long npages);

extern int get_page_idx(unsigned long virtaddr, size_t off);

extern unsigned long get_page_off(unsigned long virtaddr, size_t off);

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

struct write_param {
	unsigned long				addr;
	size_t						length;
};

#endif
