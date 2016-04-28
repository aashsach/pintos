#ifndef VM_SWAP_H
#define VM_SWAP_H

#include <stdio.h>
#include <stdlib.h>
#include "threads/synch.h"
#include "devices/block.h"
#include "threads/vaddr.h"
#include <bitmap.h>

struct block *swap_partition;
struct bitmap *swap_map;
struct lock swap_lock;


void swap_init();
size_t move_to_swap(void *kpage);
bool move_to_frame_table(size_t index, void *kpage);
void free_swap(int index);
void swap_done();

#endif
