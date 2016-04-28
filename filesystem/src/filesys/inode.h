#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "filesys/buffer_cache.h"
#include <list.h>

/////////
/*
#define BUFFER_SIZE 64

struct buffer_entry{
	//void *idisk;
	block_sector_t f_sector;
	void *buf_loc;
	struct lock buf_entry_lock;
	bool is_dirty;
	struct list_elem elem;
	int readcount; // (initial value = 0)
	struct semaphore mutex, read, write; // ( initial value = 1 )
};


struct buffer{
	struct list buf_list;
	int count;
	struct lock buf_cnt_lock;
};
//////////




void buffer_flush();
void buffer_flush_thread();
*/


void inode_init (void);
bool inode_create (block_sector_t, off_t);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
void inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);
block_sector_t inode_get_parent( struct inode *inode);
void inode_set_parent(struct inode *inode,block_sector_t parent);
void inode_set_directory(struct inode *inode,int flag);
bool inode_is_directory( struct inode *inode);
block_sector_t inode_get_self( struct inode *inode);

#endif /* filesys/inode.h */
