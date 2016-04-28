#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <stdio.h>
#include <stdlib.h>
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"



struct frame{
	struct thread *owner;
	void *frame_no;
	void *page_no;
	bool is_swappable;
	struct list_elem elem;
};

struct frame_table{
	struct list allframes;
	size_t count;
	struct lock frame_mutex;
};
//void init_frame_table();
//void* get_frame(enum palloc_flags flags);
//bool load_frame(off_t ofs, uint8_t *upage, uint32_t read_bytes, uint32_t zero_bytes, bool writable);
//bool install_frame(void *upage, void *kpage, bool writable);
//void* get_frame_multiple(enum palloc_flags flags, size_t page_count);
//bool try_evicting();

#endif
