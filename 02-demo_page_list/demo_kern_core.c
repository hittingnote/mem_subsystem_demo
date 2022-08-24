#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/atomic.h>
#include "common.h"

static inline bool addr_int_overflow(unsigned long virt_addr, size_t length) {
	return (virt_addr + length < virt_addr ||
			PAGE_ALIGN(virt_addr + length) < virt_addr + length);
}

static inline size_t get_npages(unsigned long addr, size_t size) {
	return size?
			((ALIGN(addr+size, PAGE_SIZE) - ALIGN_DOWN(addr, PAGE_SIZE)) >> PAGE_SHIFT)
			: 0;
}

int get_pagelist_and_pin(unsigned long virt_addr, size_t length,
		struct page ***p_page_list, unsigned long *p_npages) {
	struct mm_struct *mm;
	struct page **page_list;
	unsigned long lock_limit;
	unsigned long new_pinned;
	unsigned long cur_base;
	unsigned long npages;
	unsigned int gup_flags = FOLL_WRITE;
	int err = 0;

	if(!p_page_list || !p_npages) {
		err = -EINVAL;
		err_info("output parameter null\n");
		return err;
	}

	*p_page_list = NULL;
	*p_npages = 0;

	if(addr_int_overflow(virt_addr, length)) {
		err = -EINVAL;
		err_info("address integer overflow\n");
		return err;
	}

	if(!can_do_mlock()) {
		err = -EPERM;
		err_info("no mlock permission\n");
		return err;
	}

	mm = current->mm;
	mmgrab(mm);

	page_list = (struct page **)__get_free_page(GFP_KERNEL);
	if(!page_list) {
		err = -ENOMEM;
		err_info("Failed to get free page\n");
		goto err_get_free_page;
	}

	npages = get_npages(virt_addr, length);
	if(npages == 0 || npages > UINT_MAX) {
		err = -EINVAL;
		err_info("Page range overflow\n");
		goto err_npages;
	}

	lock_limit = rlimit(RLIMIT_MEMLOCK) >> PAGE_SHIFT;
	new_pinned = atomic64_add_return(npages, (atomic64_t*)&mm->pinned_vm);
	if(new_pinned > lock_limit && !capable(CAP_IPC_LOCK)) {
		err = -ENOMEM;
		err_info("Pin page exceeds limit\n");
		goto err_npages_pinned;
	}

	cur_base = virt_addr & PAGE_MASK;
	*p_npages = npages;
	while(npages) {
		down_read(&mm->mmap_sem);
		err = get_user_pages(cur_base, npages,
						gup_flags | FOLL_FORCE, page_list, NULL);
		if(err < 0) {
			err_info("Failed to get user pages\n");
			up_read(&mm->mmap_sem);
			goto err_get_upages;
		}

		cur_base += err * PAGE_SIZE;
		npages -= err;
		up_read(&mm->mmap_sem);
	}

	err = 0;
	*p_page_list = page_list;
	*p_npages = npages;
	return err;

err_get_upages:
err_npages_pinned:
	atomic64_sub(npages, (atomic64_t*)&mm->pinned_vm);
err_npages:
	free_page((unsigned long)page_list);
err_get_free_page:
	mmdrop(mm);
	return err;
}

void free_page_list(struct page **pagelist, unsigned long npages) {
	int i;

	if(!pagelist || !npages)
		return;

	for(i = 0; i < npages; i++) {
		put_page(pagelist[i]);
	}
	atomic64_sub(npages, (atomic64_t*)&current->mm->pinned_vm);
	free_page((unsigned long)pagelist);
	mmdrop(current->mm);
}

int get_page_idx(unsigned long virtaddr, size_t off) {
	return ((((virtaddr+off)-ALIGN_DOWN(virtaddr, PAGE_SIZE))
							& PAGE_MASK) >> PAGE_SHIFT);
}

unsigned long get_page_off(unsigned long virtaddr, size_t off) {
	return ((virtaddr + off) & (~PAGE_MASK));
}
