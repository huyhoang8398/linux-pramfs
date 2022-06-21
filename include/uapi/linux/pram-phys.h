#ifndef _UAPI_LINUX_PRAM_H
#define _UAPI_LINUX_PRAM_H

#include <linux/types.h>

#define PRAM_MAGIC_ROOT 0x50524D32 /* "PRM2" */
#define PRAM_MAGIC_FILE_HEAD 0x243F6A88 /* Pi */
#define PRAM_MAGIC_FILE_NODE 0x85A308D3 /* Pi */

#define PRAM_FILE_NAME_MAX 255

struct pram_root_entry {
	__u32 file_head_pfn;
};

/* 4K */
struct pram_root {
	__u32 magic_root;
	__u32 csum;
	__u32 next_root_pfn;
	__u32 len;
	struct pram_root_entry entries[0];
};

#define PRAM_ROOT_LEN_MAX                                                      \
	((PAGE_SIZE - sizeof(struct pram_root)) /                              \
	 sizeof(struct pram_root_entry))

/* 4K */
struct pram_file_head {
	__u32 magic_file_head;
	__u32 csum;
	__u32 numframes;
	__u32 mode;
	__u32 first_node_pfn;
	__u32 name_len;
	char name[PRAM_FILE_NAME_MAX];
};

#define PRAM_ENTRY_ORDER(x) ((x >> 27) & 0x1f)
#define PRAM_ENTRY_INDEX(x) ((unsigned long)x & 0x7ffffff)

struct pram_file_node_entry {
	/* 27 lower bits: index
	 * 5 upper bits: order
	 */
	__u32 flags;
	__u32 pfn;
};

/* 4K */
struct pram_file_node {
	__u32 magic_file_node;
	__u32 csum;
	__u32 next_node_pfn;
	__u32 len;
	struct pram_file_node_entry entries[0];
};

#define PRAM_FILE_NODE_LEN_MAX                                                 \
	((PAGE_SIZE - sizeof(struct pram_file_node)) /                         \
	 sizeof(struct pram_file_node_entry))

#endif
