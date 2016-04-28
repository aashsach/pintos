
#ifndef FILESYS_BUFFER_H
#define FILESYS_BUFFER_H
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "filesys/off_t.h"
#include "devices/block.h"
#include "filesys/filesys.h"
#define BUFFER_SIZE 64

struct buffer_cache_entry{
	void* cache_loc;
	block_sector_t disk_sector;
	uint32_t active_rw;
	block_sector_t n_disk_sector;
	uint32_t passive_rw;
	bool being_evicted;
	struct lock flush_load,enter_lock;
	uint32_t timestamp;
};

struct buffer_block{
	struct buffer_cache_entry *buffer[BUFFER_SIZE];
	struct lock mutex;
	struct semaphore die;
};

struct read_ahead{
	block_sector_t next;
	struct semaphore mutex, full, empty;
};

bool buffer_init();
void buffer_flush();
struct buffer_cache_entry* buffer_enter(block_sector_t sector);
void buffer_exit(struct buffer_cache_entry *entry);
void producer_ahead(block_sector_t sector);
void read_ahead_thread();

#endif
