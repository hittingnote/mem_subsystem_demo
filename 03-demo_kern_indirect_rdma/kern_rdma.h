#ifndef __KERN_RDMA_H__
#define __KERN_RDMA_H__

#include <linux/in.h>

extern int init_ib_dev_list(void);
extern void destroy_ib_dev_list(void);
extern int kern_rdma_core(bool is_server, const char *dev_name,
				const struct sockaddr_in *s_addr, 
				int rdma_port, int sgid_index,
				unsigned long virtaddr, unsigned long length);

extern void kern_rdma_release(void);

#endif

