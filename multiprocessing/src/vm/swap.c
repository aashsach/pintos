#include "vm/swap.h"

#include "threads/vaddr.h"
void swap_init(){
	swap_partition=block_get_role(BLOCK_SWAP);
	//ASSERT(swap_partition!=NULL);
	//printf("\n%d\n", block_size(swap_partition)>>3);
	swap_map=bitmap_create(block_size(swap_partition)>>3);
	//swap_map=bitmap_create(block_size(swap_partition));
	bitmap_set_all(swap_map,true);
	lock_init(&swap_lock);
	//printf("swap: %d, sectors: %d, pages: %d\n",block_size(swap_partition)*512,block_size(swap_partition),(block_size(swap_partition)/PGSIZE)*512);
	//printf("swap: %d\n",PGSIZE/BLOCK_SECTOR_SIZE);
}

size_t move_to_swap(void *kpage){
	//printf("move_to_swap\n");
	lock_acquire(&swap_lock);
	size_t index=bitmap_scan_and_flip(swap_map,0,1,true);
	if(index == BITMAP_ERROR){
		lock_release(&swap_lock);
		return index;
	}
	//printf("index: %d\n",index);

	block_sector_t ofs= index*(PGSIZE/BLOCK_SECTOR_SIZE);
	block_sector_t i;
	for(i=0;i<PGSIZE/BLOCK_SECTOR_SIZE;i++){
		block_write(swap_partition,ofs+i,kpage+BLOCK_SECTOR_SIZE*i);
	}
	//printf("data: %d\n",*(int *)(kpage+1123));
	lock_release(&swap_lock);
	//printf("moved_to_swap\n");
	return index;

}

bool move_to_frame_table(size_t index, void *kpage){
	//printf("move_to_frame_table\n");
	lock_acquire(&swap_lock);
	if(bitmap_contains(swap_map,index,1,true)){
		lock_release(&swap_lock);
		return false;
	}
	//printf("index: %d\n",index);
	block_sector_t ofs= index*(PGSIZE/BLOCK_SECTOR_SIZE);
	block_sector_t i;
	for(i=0;i<PGSIZE/BLOCK_SECTOR_SIZE;i++){
		block_read(swap_partition,ofs+i,kpage+BLOCK_SECTOR_SIZE*i);
		//bitmap_set(swap_map,ofs+i,true);
	}

	bitmap_set(swap_map,index,true);
	lock_release(&swap_lock);
	//printf("moved_to_frame_table\n");
	return true;
}

void free_swap(int index){
	lock_acquire(&swap_lock);
	if(bitmap_contains(swap_map,index,1,true)){
		lock_release(&swap_lock);
		return;
	}
	bitmap_set(swap_map,index,true);
	lock_release(&swap_lock);
}

void swap_done(){
	bitmap_destroy(swap_map);
}
