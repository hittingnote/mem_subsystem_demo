#ifndef __COMMON_H__
#define __COMMON_H__

#ifdef __KERNEL__
#define color_dbg(color, fmt, args...)						\
	printk(KERN_NOTICE "\x1b[1;" #color "m%s(%d): \x1b[0m" fmt,		\
			__FILE__, __LINE__, ##args)

#define err_info(fmt, args...)							\
	printk(KERN_ERR "\x1b[1;31mErr at %s(%d): \x1b[0m" fmt,			\
			__FILE__, __LINE__, ##args)
#else	/* __KERNEL__ */
#define color_dbg(color, fmt, args...)						\
	printf("\x1b[1;" #color "m%s(%d): \x1b[0m" fmt,	__FILE__, __LINE__, ##args)

#define err_info(fmt, args...)							\
	fprintf(stderr, "\x1b[1;31mErr at %s(%d): \x1b[0m" fmt,	__FILE__, __LINE__, ##args)
#endif

#define dbg_info(fmt, args...)		color_dbg(32, fmt, ##args)
#define DEVICE_NAME			"test_mmap"

#endif