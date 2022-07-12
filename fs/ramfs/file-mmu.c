/* file-mmu.c: ramfs MMU-based file operations
 *
 * Resizable simple ram filesystem for Linux.
 *
 * Copyright (C) 2000 Linus Torvalds.
 *               2000 Transmeta Corp.
 *
 * Usage limits added by David Gibson, Linuxcare Australia.
 * This file is released under the GPL.
 */

/*
 * NOTE! This filesystem is probably most useful
 * not as a real filesystem, but as an example of
 * how virtual filesystems can be written.
 *
 * It doesn't get much simpler than this. Consider
 * that this file implements the full semantics of
 * a POSIX-compliant read-write filesystem.
 *
 * Note in particular how the filesystem does not
 * need to implement any data structures of its own
 * to keep track of the virtual data: using the VFS
 * caches is sufficient.
 */

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <asm/pgtable.h>
#include <linux/pagemap.h>

#include "internal.h"
#include "pram-ioctl.h"
#include <uapi/linux/pram-phys.h>

struct pram_data {
	struct list_head ranges;
	struct mutex lock;
	struct address_space *mapping;
};

static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
						 unsigned long addr,
						 unsigned long len,
						 unsigned long pgoff,
						 unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

static long pram_ioctl_dump(struct pram_data *data, struct pram_dump *karg)

{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *pte;
	struct page *page = NULL;
	pud_t *pud;
	void *kernel_address;

	struct mm_struct *mm = current->mm;
	pgd = pgd_offset(mm, karg->addr);
	pmd = pmd_offset(pgd, karg->addr);
	pte = *pte_offset_map(pmd, karg->addr);
	page = pte_page(pte);

	struct pram_file_node *node;
	pgoff_t index = PRAM_ENTRY_INDEX(node->entries[0].flags);
	pgoff_t subindex;

	kernel_address = kmap(page);
	add_to_page_cache_lru(page, data->mapping, index + subindex,
			      GFP_KERNEL);
	kunmap(page);
}

static long pram_ioctl(struct file *filp, unsigned int ioctl, unsigned long arg)
{
	struct pram_data *data = filp->private_data;
	data->mapping = filp->f_mapping;
	BUG_ON(!data);
	switch (ioctl) {
	case PRAM_IOCTL_DUMP: {
		struct pram_dump __user *uarg = (struct pram_dump __user *)arg;
		struct pram_dump _karg;
		if (unlikely(copy_from_user(&_karg, uarg, sizeof(_karg))))
			return -EFAULT;
		return pram_ioctl_dump(data, &_karg);
	}
	default:
		return -ENOTTY;
	}
}

const struct file_operations ramfs_file_operations = {
	.read_iter = generic_file_read_iter,
	.write_iter = generic_file_write_iter,
	.mmap = generic_file_mmap,
	.fsync = noop_fsync,
	.splice_read = generic_file_splice_read,
	.splice_write = iter_file_splice_write,
	.llseek = generic_file_llseek,
	.get_unmapped_area = ramfs_mmu_get_unmapped_area,
	.unlocked_ioctl = pram_ioctl,
};

const struct inode_operations ramfs_file_inode_operations = {
	.setattr = simple_setattr,
	.getattr = simple_getattr,
};
