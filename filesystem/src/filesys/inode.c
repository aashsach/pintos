#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"
#include "filesys/buffer_cache.h"
/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44
#define base_size sizeof(block_sector_t)

//struct lock my_lock;
/* On-disk inode.
 Must be exactly BLOCK_SECTOR_SIZE bytes long. */

struct inode_disk {
	block_sector_t start; /* First data sector. */
	off_t length; /* File size in bytes. */
	unsigned magic; /* Magic number. */

	uint32_t direct_no;
	block_sector_t direct[100];

	uint32_t indirect_no;
	block_sector_t indirect[15];

	uint32_t d_indirect_no;
	block_sector_t d_indirect;

	int is_directory;

	uint32_t unused[5]; /* Not used. */
};

/* Returns the number of sectors to allocate for an inode SIZE
 bytes long. */
static inline size_t bytes_to_sectors(off_t size) {
	return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

/* In-memory inode. */
struct inode {
	struct list_elem elem; /* Element in inode list. */
	block_sector_t sector; /* Sector number of disk location. */
	int open_cnt; /* Number of openers. */
	bool removed; /* True if deleted, false otherwise. */
	int deny_write_cnt; /* 0: writes ok, >0: deny writes. */
	//struct inode_disk data; /* Inode content. */
	off_t len;
	bool isdir;
	struct lock grow_lock;

};

/* Returns the block device sector that contains byte offset POS
 within INODE.
 Returns -1 if INODE does not contain data for a byte at offset
 POS. */
static block_sector_t byte_to_sector(const struct inode *inode, off_t pos) {

	struct buffer_cache_entry *i_entry=buffer_enter(inode->sector);
	struct inode_disk *i_disk=i_entry->cache_loc;
	struct buffer_cache_entry *entry;

	ASSERT(inode != NULL);
	block_sector_t val = -1;
	if (pos < i_disk->length) {
		int sector_no = pos / BLOCK_SECTOR_SIZE;
		int sector_ofs;
		if (sector_no < 100) {
			val = i_disk->direct[sector_no];
			//if(pos+BLOCK_SECTOR_SIZE < i_disk->length && sector_no<99)
			//	producer_ahead(i_disk->direct[sector_no+1]);
		}
		else if (sector_no < 2020) {
			sector_no = sector_no - 100;
			sector_ofs = sector_no / 128;
			entry = buffer_enter(i_disk->indirect[sector_ofs]);
			sector_ofs = sector_no % 128;
			memcpy(&val, entry->cache_loc+sector_ofs*base_size, base_size);
			//if(pos+BLOCK_SECTOR_SIZE < i_disk->length && sector_ofs<127){
			//	block_sector_t next;
			//	memcpy(&next, entry->cache_loc+(sector_ofs+1)*base_size, base_size);
			//	producer_ahead(next);
			//}
			buffer_exit(entry);
		}
		else if (sector_no < 18404) {
			sector_no = sector_no - 2020;
			sector_ofs = sector_no / 128;
			entry = buffer_enter(i_disk->d_indirect);
			memcpy(&val, entry->cache_loc + sector_ofs*base_size, base_size);
			buffer_exit(entry);
			entry = buffer_enter(val);
			sector_ofs = sector_no % 128;
			memcpy(&val, entry->cache_loc + sector_ofs*base_size, base_size);
			buffer_exit(entry);
		}
		else {
			val = -1;
		}
	}
	else {
		val = -1;
	}
	//printf("\nofs:%d:val:%d\n",pos,val);

	buffer_exit(i_entry);
	return val;
}

/* List of open inodes, so that opening a single inode twice
 returns the same `struct inode'. */
static struct list open_inodes;


////EXTEND INODES
void direct_extend(struct inode_disk *data, off_t *diff) {
	struct buffer_cache_entry *entry;
	//printf("direct extend:%d\n",*diff);
	block_sector_t left = 100 - data->direct_no;

	if (left == 0 || *diff <= 0)
		return;

	block_sector_t add = left <= *diff ? left : *diff;
	int i = data->direct_no;
	int j = 0;
	while (j < add) {
		free_map_allocate(1, &data->direct[i]);
		entry = buffer_enter(data->direct[i]);
		memset(entry->cache_loc,0,BLOCK_SECTOR_SIZE);
		buffer_exit(entry);
		i++;
		j++;
	}
	data->start = data->direct[0];
	*diff = *diff - add;
	data->direct_no = data->direct_no + add;
}

void indirect_extend(struct inode_disk *data, off_t *diff) {

	struct buffer_cache_entry *entry1;
	struct buffer_cache_entry *entry2;
	block_sector_t left = 1920 - data->indirect_no;
	if (left == 0 || *diff <= 0)
		return;
	//printf("INDIRECT EXTEND %d:%d\n",*diff,data->indirect_no);
	block_sector_t add = left <= *diff ? left : *diff;
	int i = 0;
	int cnt = add;

	if(data->indirect_no>0)
	{
		i = (data->indirect_no - 1) / 128;
		int j = (data->indirect_no - 1) % 128;
		j++;
		entry1 = buffer_enter(data->indirect[i]);
		block_sector_t index;
		while (j < 128 && cnt > 0) {
			ASSERT(free_map_allocate (1, &index));
			entry2 = buffer_enter(index);
			memset(entry2->cache_loc,0,BLOCK_SECTOR_SIZE);
			buffer_exit(entry2);
			memcpy(entry1->cache_loc + base_size * j, &index, base_size);
			j++;
			cnt--;
		}
		buffer_exit(entry1);
		i++;
	}
	while (cnt > 0) {
		free_map_allocate(1, &data->indirect[i]);
		entry1 = buffer_enter(data->indirect[i]);
		memset(entry1->cache_loc,0,BLOCK_SECTOR_SIZE);
		int j = 0;
		block_sector_t index;
		while (j < 128 && cnt > 0) {
			ASSERT(free_map_allocate (1, &index));
			entry2 = buffer_enter(index);
			memset(entry2->cache_loc,0,BLOCK_SECTOR_SIZE);
			buffer_exit(entry2);
			memcpy(entry1->cache_loc + base_size * j, &index, base_size);
			j++;
			cnt--;
		}
		buffer_exit(entry1);
		i++;
	}
	*diff = *diff - add;
	data->indirect_no += add;
}
void d_indirect_extend(struct inode_disk *data, size_t *diff) {
	block_sector_t left = 16384 - data->d_indirect_no;
	if (left == 0 || *diff <= 0)
		return;

	struct buffer_cache_entry *entry1;
	struct buffer_cache_entry *entry2;
	struct buffer_cache_entry *entry3;
	if (left == 0 || *diff <= 0)
		return;
	block_sector_t add = left <= *diff ? left : *diff;
	int i = 0;
	int cnt = add;
	block_sector_t ofs;
	if(data->d_indirect_no>0)
	{
		entry1=buffer_enter(data->d_indirect);
		i = (data->indirect_no - 1) / 128;
		memcpy(&ofs,entry1->cache_loc+i*base_size,base_size);
		entry2 = buffer_enter(ofs);
		int j = (data->indirect_no - 1) % 128;
		j++;
		block_sector_t index;
		while (j < 128 && cnt > 0) {
			ASSERT(free_map_allocate (1, &index));
			entry3 = buffer_enter(index);
			memset(entry3->cache_loc,0,BLOCK_SECTOR_SIZE);
			buffer_exit(entry3);
			memcpy(entry2->cache_loc + base_size * j, &index, base_size);
			j++;
			cnt--;
		}
		buffer_exit(entry2);
		buffer_exit(entry1);
		i++;
	}
	else{
		free_map_allocate(1, &data->d_indirect);
		entry1 = buffer_enter(data->d_indirect);
		memset(entry1->cache_loc,0,BLOCK_SECTOR_SIZE);
		int j = 0;
		block_sector_t index;
		while (j < 128 && cnt > 0) {
			ASSERT(free_map_allocate (1, &index));
			entry2 = buffer_enter(index);
			memset(entry2->cache_loc,0,BLOCK_SECTOR_SIZE);
			buffer_exit(entry2);
			memcpy(entry1->cache_loc + base_size * j, &index, base_size);
			j++;
			cnt--;
		}
		buffer_exit(entry1);
		i++;
	}
	*diff = *diff - add;
	data->d_indirect_no += add;

}

bool inode_extend(struct inode *inode, off_t offset) {
	struct buffer_cache_entry *i_entry=buffer_enter(inode->sector);
	struct inode_disk *i_disk=i_entry->cache_loc;
	block_sector_t new = bytes_to_sectors(offset);
	block_sector_t cur = bytes_to_sectors(i_disk->length);
	off_t diff = new - cur;

	if (diff < 0){
		buffer_exit(i_entry);
		return true;
	}
	direct_extend(i_disk, &diff);
	indirect_extend(i_disk, &diff);
	d_indirect_extend(i_disk, &diff);
	if (diff > 0){
		buffer_exit(i_entry);
		PANIC("too large file");
		return false;
	}
	i_disk->length = offset;
	inode->len=offset;

	buffer_exit(i_entry);
	//printf("offset:%d:cur:%d:new:%d::%d\n",offset,cur,new,inode_length(inode));

	return true;
}

/* Initializes the inode module. */
void inode_init(void) {
	list_init(&open_inodes);
	buffer_init();

}

/////INDEXED INODESSS
bool direct_allocate(uint32_t cnt, struct inode_disk *disk_inode) {
	struct buffer_cache_entry *entry=NULL;
	if (cnt <= 0) {
		return true;
	}
	//printf("\nDIrect %d\n",cnt);
	int i = 0;
	while (cnt > 0) {
		free_map_allocate(1, &disk_inode->direct[i]);
		entry = buffer_enter(disk_inode->direct[i]);
		memset(entry->cache_loc,0,BLOCK_SECTOR_SIZE);
		buffer_exit(entry);
		i++;
		cnt--;
	}
	disk_inode->start = disk_inode->direct[0];
	return true;
}
bool indirect_allocate(uint32_t cnt, struct inode_disk *disk_inode) {

	if (cnt <= 0) {
		return true;
	}
	//printf("\nINDIrect %d\n",cnt);
	struct buffer_cache_entry *entry1;
	struct buffer_cache_entry *entry2;
	//int indirect_sectors = (cnt - 1) / 128;
	int i = 0;
	//printf("\ninode_allocate %d\n",indirect_sectors);
	while (cnt > 0) {
		free_map_allocate(1, &disk_inode->indirect[i]);
		entry1 = buffer_enter(disk_inode->indirect[i]);
		memset(entry1->cache_loc,0,BLOCK_SECTOR_SIZE);
		int j = 0;
		block_sector_t index;
		while (j < 128 && cnt > 0) {
			ASSERT(free_map_allocate (1, &index));
			entry2 = buffer_enter(index);
			memset(entry2->cache_loc,0,BLOCK_SECTOR_SIZE);
			buffer_exit(entry2);
			//printf("%d:%d\n",j,index);
			memcpy(entry1->cache_loc + base_size * j, &index, base_size);
			j++;
			cnt--;
		}
		buffer_exit(entry1);
		i++;
		//indirect_sectors--;
		//printf("\ninode_allocate %d\n",indirect_sectors);
	}
	//printf("\nINDIrect  end\n");
	return true;
}
bool d_indirect_allocate(uint32_t cnt, struct inode_disk *disk_inode) {
	if (cnt <= 0)
		return true;

	PANIC("\doubly nINDIrect\n");
	struct buffer_cache_entry *entry1,*entry2,*entry3;
	free_map_allocate(1, &disk_inode->d_indirect);
	entry1=buffer_enter(disk_inode->d_indirect);
	memset(entry1->cache_loc,0,BLOCK_SECTOR_SIZE);
	int i = 0;
	int b_index;
	while (cnt > 0) {
		free_map_allocate(1, &b_index);
		entry2=buffer_enter(b_index);
		memset(entry2->cache_loc,0,BLOCK_SECTOR_SIZE);
		int j = 0;
		block_sector_t index;
		while (j > 128 && cnt > 0) {
			free_map_allocate(1, &index);
			entry3=buffer_enter(index);
			memset(entry3->cache_loc,0,BLOCK_SECTOR_SIZE);
			buffer_exit(entry3);
			memcpy(entry2->cache_loc + base_size * j, &index, base_size);
			j++;
			cnt--;
		}
		buffer_exit(entry2);
		memcpy(entry1->cache_loc + base_size * i, &b_index, base_size);
		i++;
	}
	buffer_exit(entry1);
	return true;
}

bool inode_create_indexed(block_sector_t sector, off_t length) {
	struct inode_disk *disk_inode = NULL;
	bool success = false;

	ASSERT(length >= 0);
	ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

	disk_inode = calloc(1, sizeof *disk_inode);

	size_t sectors = bytes_to_sectors(length);
	disk_inode->length = length;
	disk_inode->magic = INODE_MAGIC;

	block_sector_t direct = 0, indirect = 0, d_indirect = 0;

	if (sectors <= 100) {
		direct = sectors;
	} else if (sectors > 100 && sectors <= 2020) {
		direct = 100;
		indirect = sectors - 100;
	} else if (sectors > 2020 && sectors <= 18404) {
		direct = 100;
		indirect = 1920;
		d_indirect = sectors - 1920 - 100;
	} else {
		PANIC("TOO BIG FILE");
	}
	disk_inode->direct_no = direct;
	disk_inode->indirect_no = indirect;
	disk_inode->d_indirect_no = d_indirect;

	direct_allocate(direct, disk_inode);
	indirect_allocate(indirect, disk_inode);
	d_indirect_allocate(d_indirect, disk_inode);
	disk_inode->is_directory = 0;

	struct buffer_cache_entry *entry = buffer_enter(sector);
	memcpy(entry->cache_loc,disk_inode,BLOCK_SECTOR_SIZE);

	buffer_exit(entry);
	free(disk_inode);
	//printf("done inode_allocate\n");
	return true;
}

void inode_remove_indexed(struct inode *inode) {
	ASSERT(inode);
	struct buffer_cache_entry *i_entry = buffer_enter(inode->sector);
	struct inode_disk *disk_inode =i_entry->cache_loc;
	block_sector_t val;
	int sectors;
	struct buffer_cache_entry *entry;
	int i = 0;
	while (i < disk_inode->direct_no) {
		free_map_release(disk_inode->direct[i], 1);
		i++;
	}
	i = 0;
	if (disk_inode->indirect_no > 0) {
		sectors = (disk_inode->indirect_no - 1) / 128;
		while (i <= sectors && disk_inode->indirect_no>0) {
			entry = buffer_enter(disk_inode->indirect[i]);
			int j = 0;
			while (j < 128 && disk_inode->indirect_no>0) {
				memcpy(&val, entry->cache_loc + base_size * j, base_size);
				free_map_release(val, 1);
				j++;
				disk_inode->indirect_no--;
			}
			buffer_exit(entry);
			free_map_release(disk_inode->indirect[i], 1);
			i++;
		}
	}
	i = 0;
	if (disk_inode->d_indirect_no > 0) {
		PANIC("double indirect\n");
		sectors = (disk_inode->d_indirect_no - 1) / 128;
		entry = buffer_enter(disk_inode->d_indirect);
		struct buffer_cache_entry *entry2;
		while (i <= sectors && disk_inode->d_indirect_no>0) {
			memcpy(&val, entry->cache_loc + base_size * i, base_size);
			block_sector_t temp_val;
			entry2 = buffer_enter(val);
			int j = 0;
			while (j < 128 && disk_inode->d_indirect_no>0 ) {
				memcpy(&temp_val, entry2->cache_loc + base_size * j, base_size);
				free_map_release(temp_val, 1);
				j++;
				disk_inode->d_indirect_no--;
			}
			buffer_exit(entry2);
			free_map_release(val, 1);
			i++;
		}
		buffer_exit(entry);
		free_map_release(disk_inode->d_indirect, 1);
	}
	buffer_exit(i_entry);
}

/////END INDEXED INODESSS

/* Initializes an inode with LENGTH bytes of data and
 writes the new inode to sector SECTOR on the file system
 device.
 Returns true if successful.
 Returns false if memory or disk allocation fails. */

bool inode_create(block_sector_t sector, off_t length) {
	return inode_create_indexed(sector, length);

}

/* Reads an inode from SECTOR
 and returns a `struct inode' that contains it.
 Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector) {
	struct list_elem *e;
	struct inode *inode;

	/* Check whether this inode is already open. */
	for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e =
			list_next(e)) {
		inode = list_entry (e, struct inode, elem);
		if (inode->sector == sector) {
			inode_reopen(inode);
			return inode;
		}
	}

	/* Allocate memory. */
	inode = malloc(sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front(&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
	lock_init(&inode->grow_lock);
	struct buffer_cache_entry *entry = buffer_enter(sector);
	struct inode_disk *i_disk=entry->cache_loc;
	inode->len=i_disk->length;
	if(i_disk->is_directory==0)
		inode->isdir=false;
	else if(i_disk->is_directory==1)
		inode->isdir=true;
	else{
		PANIC("MEMLEAK");
	}
	buffer_exit(entry);
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode) {
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode) {
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 If this was the last reference to INODE, frees its memory.
 If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode) {

	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0) {
		/* Remove from inode list and release lock. */

		//block_write(fs_device,inode->sector,&inode->data);
		list_remove(&inode->elem);

		/* Deallocate blocks if removed. */
		if (inode->removed) {
			//printf("enter remove\n");
			inode_remove_indexed(inode);
			free_map_release(inode->sector, 1);
			//printf("exit remove\n");
		}
		free(inode);
	}

}

/* Marks INODE to be deleted when it is closed by the last caller who
 has it open. */
void inode_remove(struct inode *inode) {
	ASSERT(inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 Returns the number of bytes actually read, which may be less
 than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size,
		off_t offset) {

	///printf("enter read\n");
	off_t bytes_read = 0;
	if (offset + size - 1 > inode_length(inode)) {
		lock_acquire(&inode->grow_lock);
	}

	while (size > 0) {
		/* Disk sector to read, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		struct buffer_cache_entry *entry = buffer_enter(sector_idx);
		memcpy(buffer_+bytes_read,entry->cache_loc+sector_ofs,chunk_size);
		buffer_exit(entry);
		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
	if(lock_held_by_current_thread(&inode->grow_lock))
		lock_release(&inode->grow_lock);
	//printf("exit read\n");
	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 Returns the number of bytes actually written, which may be
 less than SIZE if end of file is reached or an error occurs.
 (Normally a write at end of file would extend the inode, but
 growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size,
		off_t offset) {

	off_t bytes_written = 0;
	//printf("enter write\n");
	if (inode->deny_write_cnt)
		return 0;
	if (offset + size - 1 > inode_length(inode)) {
		lock_acquire(&inode->grow_lock);
		inode_extend(inode, offset + size);
	}

	while (size > 0) {
		/* Sector to write, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

		struct buffer_cache_entry *entry = buffer_enter(sector_idx);
		memcpy(entry->cache_loc+sector_ofs,buffer_+bytes_written,chunk_size);

		buffer_exit(entry);


		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
	if(lock_held_by_current_thread(&inode->grow_lock))
		lock_release(&inode->grow_lock);
	//printf("exit write\n");
	return bytes_written;
}

/* Disables writes to INODE.
 May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode) {
	inode->deny_write_cnt++;
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 Must be called once by each inode opener who has called
 inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode) {
	ASSERT(inode->deny_write_cnt > 0);
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(const struct inode *inode) {
	return inode->len;
}

void inode_set_directory(struct inode *inode, int val) {
	struct buffer_cache_entry *entry = buffer_enter(inode->sector);
	struct inode_disk *disk=entry->cache_loc;
	disk->is_directory = val;
	buffer_exit(entry);
	if(val==0)
		inode->isdir=false;
	else
		inode->isdir=true;
}
bool inode_is_directory(struct inode *inode) {
	return inode->isdir;
}

off_t inode_opencount(struct inode *inode) {
	return inode->open_cnt;
}
