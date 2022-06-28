#ifndef PRAM_H
#define PRAM_H 

#include <linux/types.h>
#include <linux/ioctl.h>

#define PRAM_IOCTL_NR 0xf1

#define PRAM_PIN_MAX (PAGE_SIZE / sizeof(struct page *))
struct pram_dump {
	void *addr;
	int nr_pages;
};
#define PRAM_IOCTL_DUMP _IOW(PRAM_IOCTL_NR, 0xc0, struct pram_dump)

#endif
