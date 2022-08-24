#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/rwlock.h>
#include <linux/list.h>
#include <linux/highmem.h>
#include "kern_sg.h"
#include "common.h"

struct sg_tbl_entry {
	struct sg_table				sg_tbl;
	struct kmap_table			*kaddr_tbl;
	pid_t						pid;
	struct mm_struct			*mm;
	struct list_head			ent;
	unsigned long				npages;
};

static struct list_head sg_tbl_list;
static rwlock_t rwlock;

void init_sg_tbl_list(void) {
	INIT_LIST_HEAD(&sg_tbl_list);
	rwlock_init(&rwlock);
}

static struct sg_tbl_entry *get_list_ent_from_pid(pid_t pid) {
	struct sg_tbl_entry *sg_tbl_ent;

	read_lock(&rwlock);
	list_for_each_entry(sg_tbl_ent, &sg_tbl_list, ent) {
		if(sg_tbl_ent->pid == pid) {
			break;
		}
	}

	if(&sg_tbl_ent->ent == &sg_tbl_list) {
		sg_tbl_ent = NULL;
	}

	read_unlock(&rwlock);
	return sg_tbl_ent;
}

struct sg_table *get_sg_tbl_from_pid(pid_t pid) {
	struct sg_tbl_entry *sg_tbl_ent;
	sg_tbl_ent = get_list_ent_from_pid(pid);
	return sg_tbl_ent? (&sg_tbl_ent->sg_tbl): NULL;
}

static inline bool addr_int_overflow(unsigned long virt_addr, size_t length) {
	return (virt_addr + length < virt_addr ||
			PAGE_ALIGN(virt_addr + length) < virt_addr + length);
}

static inline size_t get_npages(unsigned long addr, size_t size) {
	return size?
			((ALIGN(addr+size, PAGE_SIZE) - ALIGN_DOWN(addr, PAGE_SIZE)) >> PAGE_SHIFT)
			: 0;
}

static inline unsigned long pg_offset(unsigned long addr) {
	return (addr & (~PAGE_MASK));
}

static int get_pagelist_and_pin(unsigned long virt_addr, size_t length,
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

static void free_page_list(struct page **pagelist,
				unsigned long npages, const bool unpin) {
	int i;

	if(!pagelist || !npages)
		return;

	if(unpin) {
		for(i = 0; i < npages; i++) {
			put_page(pagelist[i]);
		}
		atomic64_sub(npages, (atomic64_t*)&current->mm->pinned_vm);
		mmdrop(current->mm);
	}
	free_page((unsigned long)pagelist);
}

int get_sg_list(unsigned long virtaddr, unsigned long length,
			unsigned int max_seg_sz, struct sg_table **pp_sg_head) {
	struct page **page_list;
	struct sg_tbl_entry *sg_tbl_ent;
	struct sg_table *p_sg_head;
	struct scatterlist *sg;
	size_t npages, i;
	int n_sg_ent = 0;
	int err = 0;

	if(!pp_sg_head) {
		err = -EINVAL;
		err_info("Output parameter null\n");
		return err;
	}

	err = get_pagelist_and_pin(virtaddr, length, &page_list, &npages);
	if(err) {
		err_info("Failed to get pagelist\n");
		return err;
	}

	sg_tbl_ent = kzalloc(sizeof(*sg_tbl_ent), GFP_KERNEL);
	if(!sg_tbl_ent) {
		err = -ENOMEM;
		err_info("Failed to alloc sg tbl entry\n");
		goto err_alloc_tbl_ent;
	}

	sg_tbl_ent->pid = current->pid;
	sg_tbl_ent->mm = current->mm;
	p_sg_head = &sg_tbl_ent->sg_tbl;
	err = sg_alloc_table(p_sg_head, npages, GFP_KERNEL);
	if(err) {
		err_info("Failed to alloc sg table\n");
		goto err_alloc_sg;
	}

	sg = NULL;
	while(i < npages) {
		unsigned long n_cont_page;
		struct page *cur_page = page_list[i];
		unsigned long cur_pfn = page_to_pfn(page_list[i]);
		bool is_first = (i == 0);

		sg = is_first? p_sg_head->sgl: sg_next(sg);

		for(n_cont_page = 0;
					i < npages && n_cont_page < (max_seg_sz >> PAGE_SHIFT);
					i++, n_cont_page++) {
			unsigned long end_pfn = page_to_pfn(page_list[i]);
			if(cur_pfn + n_cont_page != end_pfn) {
				break;
			}
		}

		sg_set_page(sg, cur_page, n_cont_page << PAGE_SHIFT, 0);

		n_sg_ent++;
	}

	sg_mark_end(sg);
	p_sg_head->nents = n_sg_ent;
	free_page_list(page_list, npages, false);
	write_lock(&rwlock);
	list_add_tail(&sg_tbl_ent->ent, &sg_tbl_list);
	write_unlock(&rwlock);
	*pp_sg_head = p_sg_head;
	return err;

err_alloc_sg:
	kfree(sg_tbl_ent);
err_alloc_tbl_ent:
	free_page_list(page_list, npages, true);
	return err;
}

void free_sg_list(const struct sg_table *sg_head) {
	int i;
	int npages = 0;
	struct scatterlist *sg;
	struct sg_tbl_entry *sg_tbl_ent;
	struct mm_struct *mm;

	for_each_sg(sg_head->sgl, sg, sg_head->nents, i) {
		unsigned long cur_pfn = page_to_pfn(sg_page(sg));
		int cur_npg = get_npages(sg->offset, sg->length);
		int j;
		npages += cur_npg;
		for(j = 0; j < cur_npg; j++) {
			put_page(pfn_to_page(cur_pfn + j));
		}
	}

	sg_free_table((struct sg_table*)sg_head);

	sg_tbl_ent = container_of(sg_head, struct sg_tbl_entry, sg_tbl);
	mm = sg_tbl_ent->mm;
	atomic64_sub(npages, (atomic64_t*)&mm->pinned_vm);
	mmdrop(mm);

	write_lock(&rwlock);
	list_del(&sg_tbl_ent->ent);
	write_unlock(&rwlock);
}

struct kmap_table **get_kmap_table_from_pid(pid_t pid) {
	struct sg_tbl_entry *sg_tbl_ent;
	sg_tbl_ent = get_list_ent_from_pid(pid);
	return sg_tbl_ent? (&sg_tbl_ent->kaddr_tbl): NULL;
}

int kmap_user_addr(unsigned long virtaddr, unsigned long length,
			struct kmap_table ***p_kmap_addr, unsigned long *p_npages) {
	struct page **page_list;
	struct kmap_table **kmap_addr = NULL;
	struct sg_tbl_entry *tbl_entry = NULL;
	unsigned long cur_base;
	unsigned long cur_size;
	int i, err = 0;

	if(!p_kmap_addr || !p_npages) {
		err = -EINVAL;
		err_info("%s: output parameter nil\n", __func__);
		return err;
	}

	*p_kmap_addr = NULL;
	*p_npages = 0;

	err = get_pagelist_and_pin(virtaddr, length, &page_list, p_npages);
	if(err) {
		err_info("Failed to get page list\n");
		return err;
	}

	tbl_entry = kzalloc(sizeof(*tbl_entry), GFP_KERNEL);
	if(!tbl_entry) {
		err = -ENOMEM;
		err_info("Failed to alloc tbl entry\n");
		goto err_kmap_alloc;
	}

	tbl_entry->pid = current->pid;
	tbl_entry->mm = current->mm;
	tbl_entry->npages = (*p_npages);

	kmap_addr = &tbl_entry->kaddr_tbl;
	*kmap_addr = kzalloc(sizeof(**kmap_addr)*(*p_npages), GFP_KERNEL);
	if(!(*kmap_addr)) {
		err = -ENOMEM;
		err_info("Failed to alloc kmap arr\n");
		goto err_kmap_alloc;
	}

	cur_base = virtaddr;
	for(i = 0; i < (*p_npages); i++) {
		void *kaddr = kmap(page_list[i]);
		if(!kaddr) {
			err = -ENOMEM;
			err_info("Failed to kmap\n");
			goto err_kmap;
		}

		(*kmap_addr)[i].base = kaddr + pg_offset(cur_base);
		cur_size = PAGE_SIZE - pg_offset(cur_base);
		cur_base = ALIGN_DOWN(cur_base, PAGE_SIZE) + PAGE_SIZE;
		if(cur_base > virtaddr + length)
			cur_size -= (cur_base - virtaddr - length);
		(*kmap_addr)[i].length = cur_size;
	}

	write_lock(&rwlock);
	list_add_tail(&tbl_entry->ent, &sg_tbl_list);
	write_unlock(&rwlock);

	free_page_list(page_list, *p_npages, false);
	*p_kmap_addr = kmap_addr;
	return err;

err_kmap:
	kfree(*kmap_addr);
err_kmap_alloc:
	if(tbl_entry)
		kfree(tbl_entry);
	free_page_list(page_list, *p_npages, true);
	*p_npages = 0;
	return err;
}

void free_kmap_table(struct kmap_table **kmap_tbl) {
	struct sg_tbl_entry *tbl_entry;
	struct mm_struct *mm;
	int i;

	tbl_entry = container_of(kmap_tbl, struct sg_tbl_entry, kaddr_tbl);
	for(i = 0; i < tbl_entry->npages; i++) {
		struct page *pg = virt_to_page((*kmap_tbl)[i].base);
		put_page(pg);
	}

	mm = tbl_entry->mm;
	atomic64_sub(tbl_entry->npages, (atomic64_t*)&mm->pinned_vm);
	mmdrop(mm);

	write_lock(&rwlock);
	list_del(&tbl_entry->ent);
	write_unlock(&rwlock);
}
