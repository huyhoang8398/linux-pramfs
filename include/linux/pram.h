#ifndef _LINUX_PRAM_H
#define _LINUX_PRAM_H

#include <uapi/linux/pram-phys.h>

extern unsigned long pram_pfn;

#ifdef CONFIG_PRAM
extern unsigned long pram_reserved_pages;
extern void pram_reserve(void);
extern void pram_ban_region(unsigned long start, unsigned long end);
extern void pram_show_banned(void);
#else
#define pram_reserved_pages 0UL
static inline void pram_reserve(void) { }
static inline void pram_ban_region(unsigned long start, unsigned long end) { }
static inline void pram_show_banned(void) { }
#endif

#endif
