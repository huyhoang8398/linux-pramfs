#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/sched/mm.h>
#include <linux/ramfs.h>

#include <linux/pram.h>

struct pram_data
{
	struct list_head ranges;
	struct mutex lock;
};

struct pram_range
{
	struct page **pages;
	unsigned long nr_pages;
	struct list_head list;
};

static int pram_open(struct inode *inode, struct file *filp)
{
	struct pram_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (unlikely(!data))
		return -ENOMEM;

	mutex_init(&data->lock);
	INIT_LIST_HEAD(&data->ranges);

	filp->private_data = data;

	return 0;
}

static long pram_ioctl_dump(struct pram_data *data,
							  struct pram_dump *karg)
{
	int ret = 0;

	struct pram_range *range;
	unsigned long start, length;

	if (unlikely(!capable(CAP_IPC_LOCK)))
	{
		return -EACCES;
	}

	if (unlikely(karg->nr_pages <= 0))
	{
		return 0;
	}

	/* prechecks done */

	start = (unsigned long)karg->addr;
	if (unlikely(start & (PAGE_SIZE - 1)))
	{
		ret = -EINVAL;
		goto err;
	}

	length = karg->nr_pages << PAGE_SHIFT;
	if (unlikely(karg->nr_pages > pram_dump_MAX ||
				 start + length <= start))
	{
		ret = -E2BIG;
		goto err;
	}

	if (unlikely(!access_ok(start, length)))
	{
		ret = -EFAULT;
		goto err;
	}

	range = kzalloc(sizeof(*range), GFP_KERNEL);
	if (unlikely(!range))
	{
		ret = -ENOMEM;
		goto err;
	}

	range->pages =
		kcalloc(karg->nr_pages, sizeof(range->pages[0]), GFP_KERNEL);
	if (unlikely(!range->pages))
	{
		ret = -ENOMEM;
		goto free_range;
	}

	// TODO: write to ramfs
	printk(KERN_INFO "Test ramfs ioctl\n");

	if (unlikely(ret < 0))
	{
		goto free_range_pages;
	}

	range->nr_pages = ret;
	mutex_lock(&data->lock);
	list_add(&range->list, &data->ranges);
	mutex_unlock(&data->lock);
	return ret;

free_range_pages:
	kfree(range->pages);
free_range:
	kfree(range);
err:
	return ret;
}

static long pram_ioctl(struct file *filp, unsigned int ioctl,
						  unsigned long arg)
{
	struct pram_data *data = filp->private_data;
	BUG_ON(!data);
	switch (ioctl)
	{
	case PRAM_IOCTL_DUMP:
	{
		struct pram_dump __user *uarg =
			(struct pram_dump __user *)arg;
		struct pram_dump _karg;
		if (unlikely(copy_from_user(&_karg, uarg, sizeof(_karg))))
			return -EFAULT;
		return pram_ioctl_dump(data, &_karg);
	}
	default:
		return -ENOTTY;
	}
}

static unsigned long ramfs_mmu_get_unmapped_area(struct file *file,
		unsigned long addr, unsigned long len, unsigned long pgoff,
		unsigned long flags)
{
	return current->mm->get_unmapped_area(file, addr, len, pgoff, flags);
}

const struct file_operations pram_fops = {
	.read_iter	= generic_file_read_iter,
	.write_iter	= generic_file_write_iter,
	.mmap		= generic_file_mmap,
	.fsync		= noop_fsync,
	.splice_read	= generic_file_splice_read,
	.splice_write	= iter_file_splice_write,
	.llseek		= generic_file_llseek,
	.get_unmapped_area	= ramfs_mmu_get_unmapped_area,
	.owner = THIS_MODULE,
	.open = pram_open,
	.unlocked_ioctl = pram_ioctl,
	.llseek = no_llseek,
};

const struct inode_operations pram_inode_file_operations = {
	.setattr	= simple_setattr,
	.getattr	= simple_getattr,
}

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Kernal Module for dumping page to ramfs");
MODULE_AUTHOR("DO Duy Huy Hoang");
