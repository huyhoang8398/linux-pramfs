#include <linux/xxhash.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/list.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/namei.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>
#include <linux/pfn.h>
#include <linux/pram.h>
#include <linux/ramfs.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include "../mm/internal.h" /* for compound page functions */

struct banned_region {
	unsigned long start, end; /* pfn, inclusive */
};

#define MAX_NR_BANNED (32 + MAX_NUMNODES * 2)

/* arranged in ascending order, do not overlap */
static struct banned_region banned[MAX_NR_BANNED];
static unsigned int nr_banned;

unsigned long __initdata pram_reserved_pages;
static bool __meminitdata pram_reservation_in_progress;

unsigned long pram_pfn;

static int __init parse_pram_pfn(char *arg)
{
	return kstrtoul(arg, 16, &pram_pfn);
}
early_param("pram", parse_pram_pfn);

static inline u32 pram_meta_csum(void *addr)
{
	/* skip magic and csum fields */
	/* use xxh64 since it's faster than xxh32 on 64-bit processors */
	return (u32)(xxh64(addr + 8, PAGE_SIZE - 8, 0) & 0xfffffffful);
}

void __meminit pram_ban_region(unsigned long start, unsigned long end)
{
	int i, merged = -1;

	if (pram_reservation_in_progress)
		return;

	/* first try to merge the region with an existing one */
	for (i = nr_banned - 1; i >= 0 && start <= banned[i].end + 1; i--) {
		if (end + 1 >= banned[i].start) {
			start = min(banned[i].start, start);
			end = max(banned[i].end, end);
			if (merged < 0)
				merged = i;
		} else
			/* regions are arranged in ascending order and do not
			 * intersect so the merged region cannot jump over its
			 * predecessors */
			BUG_ON(merged >= 0);
	}

	i++;
	if (merged >= 0) {
		banned[i].start = start;
		banned[i].end = end;
		/* shift if merged with more than one region */
		memmove(banned + i + 1, banned + merged + 1,
			sizeof(*banned) * (nr_banned - merged - 1));
		nr_banned -= merged - i;
		return;
	}

	/* the region does not intersect with anyone existing,
	 * try to create a new one */
	if (nr_banned == MAX_NR_BANNED) {
		pr_err("PRAM: Failed to ban %lu-%lu: "
		       "Too many banned regions\n",
		       start, end);
		return;
	}

	memmove(banned + i + 1, banned + i, sizeof(*banned) * (nr_banned - i));
	banned[i].start = start;
	banned[i].end = end;
	nr_banned++;
}

void __init pram_show_banned(void)
{
	int i;
	unsigned long n, total = 0;

	pr_info("PRAM: banned regions:\n");
	for (i = 0; i < nr_banned; i++) {
		n = banned[i].end - banned[i].start + 1;
		pr_info("%4d: [%08lx - %08lx] %ld pages\n", i, banned[i].start,
			banned[i].end, n);
		total += n;
	}
	pr_info("Total banned: %lu pages in %u regions\n", total, nr_banned);
}

static bool pram_page_banned(struct page *page)
{
	unsigned long pfn = page_to_pfn(page);
	int l = 0, r = nr_banned - 1, m;

	/* do binary search */
	while (l <= r) {
		m = (l + r) / 2;
		if (pfn < banned[m].start)
			r = m - 1;
		else if (pfn > banned[m].end)
			l = m + 1;
		else
			return true;
	}
	return false;
}

static int __init pram_reserve_page(unsigned long pfn, unsigned int order)
{
	int err = 0;
	phys_addr_t base, size;

	if (pfn >= max_low_pfn)
		return -EINVAL;

	base = PFN_PHYS(pfn);
	size = PAGE_SIZE << order;

	if (memblock_is_region_reserved(base, size) ||
	    memblock_reserve(base, size) < 0) {
		err = -EBUSY;
	}
	if (err)
		pr_err("PRAM: pfn:%lx order %u busy\n", pfn, order);
	else
		pram_reserved_pages += 1 << order;
	return err;
}

static void __init pram_unreserve_page(unsigned long pfn, unsigned int order)
{
	memblock_free(PFN_PHYS(pfn), PAGE_SIZE << order);
	pram_reserved_pages -= 1 << order;
}

static int __init pram_reserve_file(unsigned long file_head_pfn)
{
	int i, err = 0;
	struct pram_file_head *head;
	u32 csum;

	unsigned long pfn;
	struct pram_file_node *node;
	u32 node_csum;

	if (file_head_pfn >= max_low_pfn)
		return -EINVAL;

	head = pfn_to_kaddr(file_head_pfn);
	csum = pram_meta_csum(head);
	if (head->magic_file_head != PRAM_MAGIC_FILE_HEAD ||
	    head->csum != csum) {
		pr_err("PRAM: head pfn:%lx corrupted: expected %x, found %x\n",
		       file_head_pfn, head->csum, csum);
		return -EIO;
	}

	err = pram_reserve_page(file_head_pfn, 0);
	if (err)
		return err;
	pfn = head->first_node_pfn;
	while (pfn != 0) {
		if (pfn >= max_low_pfn)
			return -EINVAL;

		node = pfn_to_kaddr(pfn);
		node_csum = pram_meta_csum(node);
		if (node->magic_file_node != PRAM_MAGIC_FILE_NODE ||
		    node->csum != node_csum) {
			pr_err("PRAM: node pfn:%lx corrupted: expected %x, found %x\n",
			       pfn, node->csum, node_csum);
			err = -EIO;
			break;
		}

		err = pram_reserve_page(pfn, 0);
		if (err)
			return err;
		for (i = 0; i < node->len; i++) {
			err = pram_reserve_page(
				node->entries[i].pfn,
				PRAM_ENTRY_ORDER(node->entries[i].flags));
			if (err)
				break;
		}
		if (err) {
			while (--i >= 0)
				pram_unreserve_page(
					node->entries[i].pfn,
					PRAM_ENTRY_ORDER(node->entries[i].flags));
			pram_unreserve_page(pfn, 0);
			break;
		}

		pfn = node->next_node_pfn;
	}

	return err;
}

static void __init pram_unreserve_file(unsigned long file_head_pfn)
{
	int i;
	struct pram_file_head *head;

	unsigned long pfn, next_pfn;
	struct pram_file_node *node;

	head = pfn_to_kaddr(file_head_pfn);
	pfn = head->first_node_pfn;
	while (pfn != 0) {
		node = pfn_to_kaddr(pfn);
		for (i = 0; i < node->len; i++) {
			pram_unreserve_page(
				node->entries[i].pfn,
				PRAM_ENTRY_ORDER(node->entries[i].flags));
		}

		next_pfn = node->next_node_pfn;
		pram_unreserve_page(pfn, 0);
		pfn = next_pfn;
	}
	pram_unreserve_page(file_head_pfn, 0);
}

static int __init pram_reserve_link(unsigned long pfn, unsigned long *next_pfn)
{
	int i, err = 0;
	struct pram_root *link;
	u32 csum;

	if (pfn >= max_low_pfn) {
		pr_err("PRAM: pfn:%lx invalid\n", pfn);
		return -EINVAL;
	}

	link = pfn_to_kaddr(pfn);
	csum = pram_meta_csum(link);
	if (link->magic_root != PRAM_MAGIC_ROOT || link->csum != csum) {
		pr_err("PRAM: root pfn:%lx corrupted: expected %x, found %x\n",
		       pfn, link->csum, csum);
		return -EIO;
	}

	err = pram_reserve_page(pfn, 0);
	if (err)
		return err;
	for (i = 0; i < link->len; i++) {
		err = pram_reserve_file(link->entries[i].file_head_pfn);
		if (err)
			break;
	}
	if (err) {
		while (--i >= 0)
			pram_unreserve_file(link->entries[i].file_head_pfn);
		pram_unreserve_page(pfn, 0);
	} else {
		*next_pfn = link->next_root_pfn;
	}
	return err;
}

static void __init pram_unreserve_link(unsigned long pfn)
{
	int i;
	struct pram_root *link = pfn_to_kaddr(pfn);

	for (i = 0; i < link->len; i++)
		pram_unreserve_file(link->entries[i].file_head_pfn);
	pram_unreserve_page(pfn, 0);
}

void __init pram_reserve(void)
{
	unsigned long pfn = pram_pfn, next_pfn;
	struct pram_root *link;
	int err;

	if (!pfn)
		return;

	pr_info("PRAM: Examining persistent memory from pram_pfn = %lx...\n",
		pram_pfn);
	pram_reservation_in_progress = true;

	while (pfn != 0) {
		err = pram_reserve_link(pfn, &next_pfn);
		if (err)
			break;
		pfn = next_pfn;
	}

	pram_reservation_in_progress = false;
	if (err) {
		unsigned long bad_pfn = pfn;

		pfn = pram_pfn;
		while (pfn != bad_pfn) {
			link = pfn_to_kaddr(pfn);
			next_pfn = link->next_root_pfn;
			pram_unreserve_link(pfn);
			pfn = next_pfn;
		}
		pr_err("PRAM: Reservation failed: %d\n", err);
		pram_pfn = 0;
	} else
		pr_info("PRAM: %lu pages reserved\n", pram_reserved_pages);
}

static struct dentry *pram_mkfile(struct dentry *parent,
				  struct pram_file_head *file_head)
{
	struct dentry *dentry;
	int err;

	if (!S_ISREG(file_head->mode) || file_head->name_len > NAME_MAX)
		return ERR_PTR(-EINVAL);

	down_write(&parent->d_inode->i_rwsem);
	dentry = lookup_one_len(file_head->name, parent, file_head->name_len);
	if (IS_ERR(dentry))
		return dentry;
	err = vfs_create(parent->d_inode, dentry, file_head->mode, NULL);
	if (err) {
		dput(dentry);
		dentry = ERR_PTR(err);
		goto out_unlock;
	}
	i_size_write(dentry->d_inode, (loff_t)file_head->numframes << PAGE_SHIFT);
out_unlock:
	up_write(&parent->d_inode->i_rwsem);
	return dentry;
}

static int pram_load_file(struct dentry *parent, unsigned long file_head_pfn)
{
	int i, err = 0;

	struct dentry *dentry = NULL;
	struct pram_file_head *head;
	struct page *head_page;

	unsigned long node_pfn;

	head_page = pfn_to_page(file_head_pfn);
	ClearPageReserved(head_page);
	head = kmap(head_page);

	dentry = pram_mkfile(parent, head);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto cleanup;
	}

	node_pfn = head->first_node_pfn;
	while (node_pfn) {
		struct pram_file_node *node;
		struct page *node_page;

		node_page = pfn_to_page(node_pfn);
		ClearPageReserved(node_page);
		node = kmap(node_page);

		for (i = 0; i < node->len; i++) {
			struct address_space *mapping =
				dentry->d_inode->i_mapping;
			struct page *page = pfn_to_page(node->entries[i].pfn);
			unsigned int order = PRAM_ENTRY_ORDER(node->entries[i].flags);
			pgoff_t index = PRAM_ENTRY_INDEX(node->entries[i].flags);
			pgoff_t subindex;

			for (subindex = 0; subindex < 1 << order; subindex++) {
				struct page *subpage = page + subindex;
				ClearPageReserved(subpage);
				err = add_to_page_cache_lru(subpage, mapping,
							    index + subindex,
							    GFP_KERNEL);
				if (err) {
					put_page(subpage);
					goto cleanup;
				} else {
					SetPageUptodate(subpage);
					set_page_dirty(subpage);
					unlock_page(subpage);
					put_page(subpage);
				}
			}
		}

		node_pfn = node->next_node_pfn;
		kunmap(node_page);
		put_page(node_page);
	}

	dput(dentry);

cleanup:
	kunmap(head_page);
	put_page(head_page);
	return err;
}

static struct dentry *pram_load_tree(struct dentry *dentry_root)
{
	int i, err = 0;

	unsigned long root_pfn;
	struct pram_root *root;
	struct page *root_page;

	root_pfn = pram_pfn;
	while (root_pfn) {
		root_page = pfn_to_page(root_pfn);
		ClearPageReserved(root_page);
		root = kmap(root_page);

		for (i = 0; i < root->len; i++) {
			err = pram_load_file(dentry_root,
					     root->entries[i].file_head_pfn);
			if (err)
				break;
		}

		root_pfn = root->next_root_pfn;
		kunmap(root_page);
		put_page(root_page);
		if (err)
			break;
	}

	pram_pfn = 0;
	if (err) {
		pr_err("PRAM: Failed to load FS tree: %d\n", err);
		return ERR_PTR(err);
	} else {
		return dentry_root;
	}
}

static struct dentry *pram_mount(struct file_system_type *fs_type, int flags,
				 const char *dev_name, void *data)
{
	struct dentry *root;

	root = mount_single(fs_type, flags, data, ramfs_fill_super);
	if (!IS_ERR(root))
		root = pram_load_tree(root);
	return root;
}

static struct file_system_type pram_fs_type = {
	.name = "pram",
	.mount = pram_mount,
	.kill_sb = ramfs_kill_sb,
};

static int __init pram_init(void)
{
	return register_filesystem(&pram_fs_type);
}
module_init(pram_init);
