#ifndef __KERN_SG_H__
#define __KERN_SG_H__

#include <linux/scatterlist.h>

struct kmap_table {
	void*							base;
	unsigned long					length;
	u64								dma_addr;
};

extern int get_sg_list(unsigned long virtaddr, unsigned long length,
			unsigned int max_seg_sz, struct sg_table **pp_sg_head);
extern void free_sg_list(const struct sg_table *sg_head);

extern void init_sg_tbl_list(void);
extern struct sg_table *get_sg_tbl_from_pid(pid_t pid);
extern struct kmap_table **get_kmap_table_from_pid(pid_t pid);

extern int kmap_user_addr(unsigned long virtaddr, unsigned long length,
			struct kmap_table ***p_kmap_addr, unsigned long *p_npages);
extern void free_kmap_table(struct kmap_table **kmap_tbl);

static inline u64 get_dma_address_from_sgtbl(
				const struct sg_table *sg_head, unsigned long virtaddr) {
	return sg_dma_address(sg_head->sgl) + (virtaddr & (~PAGE_MASK));
}

#endif
