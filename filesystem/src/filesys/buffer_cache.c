#include "filesys/buffer_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "devices/timer.h"
#include <list.h>

struct buffer_block buf_block;
struct read_ahead ra;

static void buffer_unload_out(struct buffer_cache_entry *entry) {
	lock_acquire(&entry->enter_lock);
	block_write(fs_device, entry->disk_sector, entry->cache_loc);
	//lock_release(&entry->enter_lock);
}
static void buffer_load_in(struct buffer_cache_entry *entry) {
	//lock_acquire(&entry->enter_lock);
	block_read(fs_device, entry->n_disk_sector, entry->cache_loc);
	lock_release(&entry->enter_lock);
}
void flusher(){
	int i=0;
	while(true){
		timer_sleep(1000);
		lock_acquire(&buf_block.mutex);
		for (i = 0; i < BUFFER_SIZE; i++){
			if (buf_block.buffer[i] && !buf_block.buffer[i]->being_evicted){
				block_write(fs_device, buf_block.buffer[i]->disk_sector, buf_block.buffer[i]->cache_loc);
			}
		}
		lock_release(&buf_block.mutex);
	}
}
bool buffer_init() {
	int i = 0;
	for (i = 0; i < BUFFER_SIZE; i++) {
		buf_block.buffer[i] = NULL;
	}
	lock_init(&buf_block.mutex);
	//thread_create("flusher",PRI_DEFAULT,&flusher,NULL);
	thread_create("read_ahead",PRI_DEFAULT,&read_ahead_thread,NULL);
	return true;
}
void buffer_flush() {
	int i = 0;
	lock_acquire(&buf_block.mutex);
	for (i = 0; i < BUFFER_SIZE; i++) {
		if (buf_block.buffer[i]) {
			lock_acquire(&buf_block.buffer[i]->enter_lock);
			block_write(fs_device, buf_block.buffer[i]->disk_sector, buf_block.buffer[i]->cache_loc);
			lock_release(&buf_block.buffer[i]->enter_lock);
			free(buf_block.buffer[i]->cache_loc);
			free(buf_block.buffer[i]);
			buf_block.buffer[i] = NULL;
		}
	}
	lock_release(&buf_block.mutex);
}

void* buffer_find_evict(block_sector_t sector) {
	static int i;
	struct buffer_cache_entry *entry;

	for (; i < BUFFER_SIZE; i++) {
		if (buf_block.buffer[i]->being_evicted)
			continue;
		else if(lock_held_by_current_thread(&buf_block.buffer[i]->enter_lock))
			continue;
		else if (buf_block.buffer[i]->active_rw==0 && buf_block.buffer[i]->passive_rw==0) {
			break;
		}
	}
	if(i<BUFFER_SIZE){
		entry = buf_block.buffer[i];
		entry->n_disk_sector = sector;
		entry->being_evicted = true;
		lock_acquire(&entry->flush_load);
		//printf("evicted:%d\n",i);
		return entry;
	}
	else{
		i=0;
	}
	uint32_t min_time = timer_ticks();
	uint32_t pos = 0;
	int j;
	for (j = 0; j < BUFFER_SIZE; j++) {
		if (buf_block.buffer[j]->being_evicted)
			continue;
		else if(lock_held_by_current_thread(&buf_block.buffer[j]->enter_lock))
			continue;
		else if (min_time > buf_block.buffer[j]->timestamp) {
			min_time = buf_block.buffer[j]->timestamp;
			pos = j;
		}
	}

	entry = buf_block.buffer[pos];
	entry->n_disk_sector = sector;
	entry->being_evicted = true;
	lock_acquire(&entry->flush_load);
	//printf("evicted:%d\n",i);
	return entry;
}

void* buffer_find(block_sector_t sector) {
	ASSERT(fs_device);
	int i;

	for (i = 0; i < BUFFER_SIZE; i++) {
		if (!buf_block.buffer[i]){
			buf_block.buffer[i] = malloc(sizeof(struct buffer_cache_entry));
			buf_block.buffer[i]->cache_loc = malloc(512);
			buf_block.buffer[i]->active_rw = 1;
			buf_block.buffer[i]->passive_rw = 0;
			buf_block.buffer[i]->disk_sector = sector;
			buf_block.buffer[i]->n_disk_sector = sector;
			buf_block.buffer[i]->being_evicted = false;
			lock_init(&buf_block.buffer[i]->flush_load);
			lock_init(&buf_block.buffer[i]->enter_lock);
			block_read(fs_device, sector, buf_block.buffer[i]->cache_loc);
			return buf_block.buffer[i];
		}
		else if (buf_block.buffer[i]->disk_sector
				== buf_block.buffer[i]->n_disk_sector) {
			if (buf_block.buffer[i]->disk_sector == sector){
				//buf_block.buffer[i]->active_rw++;
				return buf_block.buffer[i];
			}
		} else if (buf_block.buffer[i]->n_disk_sector == sector) {
			buf_block.buffer[i]->passive_rw++;
			lock_release(&buf_block.mutex);
			lock_acquire(&buf_block.buffer[i]->flush_load);
			lock_release(&buf_block.buffer[i]->flush_load);
			lock_acquire(&buf_block.mutex);
			return buf_block.buffer[i];
		} else if (buf_block.buffer[i]->disk_sector == sector) {
			lock_release(&buf_block.mutex);
			lock_acquire(&buf_block.buffer[i]->flush_load);
			lock_release(&buf_block.buffer[i]->flush_load);
			lock_acquire(&buf_block.mutex);
			i=-1;
		} else
			continue;
	}
	return NULL;
}

struct buffer_cache_entry* buffer_enter(block_sector_t sector) {
	struct buffer_cache_entry *entry;

	lock_acquire(&buf_block.mutex);
	entry = buffer_find(sector);
	if (!entry) {
		entry = buffer_find_evict(sector);
		lock_release(&buf_block.mutex);
		buffer_unload_out(entry);
		buffer_load_in(entry);
		lock_acquire(&buf_block.mutex);
		entry->active_rw = entry->passive_rw+1;
		entry->passive_rw=0;
		entry->disk_sector = sector;
		entry->being_evicted = false;
		lock_release(&entry->flush_load);
	}else{
		//entry->active_rw++;
		//entry->passive_rw--;
	}
	entry->timestamp = timer_ticks();
	lock_release(&buf_block.mutex);
	lock_acquire(&entry->enter_lock);
	return entry;
}

void buffer_exit(struct buffer_cache_entry *entry) {
	ASSERT(entry);
	lock_acquire(&buf_block.mutex);
	--entry->active_rw;
	lock_release(&buf_block.mutex);
	lock_release(&entry->enter_lock);
	entry=NULL;
}

void producer_ahead(block_sector_t sector){
	if(!sema_try_down(&ra.empty)){
		return;
	}
	sema_down(&ra.mutex);
	ra.next=sector;
	sema_up(&ra.mutex);
	sema_up(&ra.full);
}

void consumer_ahead(){
	while(true){
		sema_down(&ra.full);
		sema_down(&ra.mutex);
		buffer_exit(buffer_enter(ra.next));
		sema_up(&ra.mutex);
		sema_up(&ra.empty);
	}
}
void read_ahead_thread(){
	sema_init(&ra.empty,1);
	sema_init(&ra.mutex,1);
	sema_init(&ra.full,0);
	consumer_ahead();
}

