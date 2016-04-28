#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"


typedef int mapid_t;
typedef int pid_t;
static void syscall_handler (struct intr_frame *);

struct lock file_lock;

bool get_file_lock(){
	if(lock_held_by_current_thread(&file_lock))
		return false;
	lock_acquire(&file_lock);
	return true;
}
void release_file_lock(bool flag){
	if(flag)
		lock_release(&file_lock);

}

void try_grow_stack(void *addr,void *esp){
	//printf("hi1\n");
	if(addr<esp)
		exit(-1);
	if(!addr || !is_user_vaddr(addr))
			exit(-1);
	//printf("hi2\n");
	//if(!pagedir_get_page(thread_current()->pagedir,addr)){
		//printf("hi3\n");
		if(addr < PHYS_BASE-2048*PGSIZE)
			exit(-1);
		//printf("hi4\n");
		new_apt_entry(NULL,0,pg_round_down(addr),NULL,0,0,true,MEMORY);
		//if(!grow_stack(addr))
			//exit(-1);
	//}
	//printf("hi5\n");
}

void invalidate_buffer(void *addr, unsigned size){
	//printf("trying %0xd\n", (int *)addr);
	void *temp=addr;
	while(temp-addr <= size){
		//printf("invalidating %x: %d\n", (int *)temp, size);
		void *frame_no=pg_round_down(pagedir_get_page(thread_current()->pagedir,temp));
		frame_set_swappable(frame_no,true);
		temp=pg_round_down(temp+PGSIZE);
	}
	//printf("sucess\n");
}



void validate_buffer(void *addr, unsigned size, bool writable, void *esp){
	//printf("trying %0xd\n", (int *)addr);
	void *temp=addr;
	struct addon_page_table *entry;
	//printf("%d\n",writable);
	while(temp-addr <= size){
	//	printf("validating %x: %d\n", (int *)temp, size);

		if(!temp || !is_user_vaddr(temp)){
			//printf("faillleddddd\n");
				exit(-1);
		}
		entry=lookup_apt(temp);
		if(!entry){
			//printf("wola1\n");
			try_grow_stack(temp,esp);
			entry=lookup_apt(temp);
			//ASSERT(entry!=NULL);
			//exit(-1);
		}
		if(!pagedir_get_page(thread_current()->pagedir,temp))
		{
			//printf("wola1\n");
			if(!load_frame(entry->file,entry->upage,
					entry->ofs,entry->read_bytes,entry->zero_bytes,
					entry->writable,entry->location,false)){
				//printf("wola2\n");
				exit(-1);
			}
		}
		else{
			void *frame_no=pg_round_down(pagedir_get_page(thread_current()->pagedir,temp));
			frame_set_swappable(frame_no,false);
		}
		if(writable && !entry->writable){
			exit(-1);
		}
		temp=pg_round_down(temp+PGSIZE);
	}
	//printf("sucess\n");
}

void validate_user_address(int *esp, int no){

	uint32_t *pd= thread_current()->pagedir;
	int i;
	for(i=0;i<no;i++)
	{
		if(!(esp + 4*i) || !is_user_vaddr(esp + 4*i)) //|| !pagedir_get_page(pd,(esp + 4*i)))
		{
			//printf("wasted\n");
			exit(-1);
		}
		if(!pagedir_get_page(pd,(esp + 4*i))){
			exit(-1);
		}
	}
	return;
}

void
syscall_init (void) 
{
	lock_init(&file_lock);
	intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

////SYSTEM CALLL FUNCTIONSSSS

void print_terminating_message(int status){
	char *s=malloc(15),*save_ptr;
		strlcpy(s,thread_name(),15);
		printf ("%s: exit(%d)\n",strtok_r(s," ",&save_ptr),status);
		free(s);
}

void halt(){
	shutdown_power_off();
}

void exit(int status){
	struct thread *t=thread_current();
	//printf("EXIT\n");
	int i;
	if(file_lock.holder==thread_current())
		lock_release(&file_lock);
	lock_acquire(&file_lock);
	for(i=0;i<128;i++){
		if(t->my_stats.file[i]){
			file_close(t->my_stats.file[i]);
			t->my_stats.file[i]=NULL;
		}
		if(t->my_stats.mfile[i]){
			munmap(i);
		}
	}
	file_close(t->my_stats.my_exectutable);
	lock_release(&file_lock);
	print_terminating_message(status);
	sema_down(&thread_current()->my_stats.parent_died);
	if(t->my_stats.pos){
		t->my_stats.pos->return_status=status;
		sema_up(&t->my_stats.pos->child_dead);
	}
	sema_up(&thread_current()->my_stats.parent_died);
	frame_thread_delete(thread_current());
	empty_apt();
	thread_exit();
}

pid_t exec(const char *cmd_line){
	int return_status=process_execute(cmd_line);
	if(return_status==TID_ERROR)
		return -1;
	struct children_stats *child=find_child(return_status);
	if(!child)
		return -1;
	sema_down(&child->child_load);
	if(child->is_alive==false)
		return -1;
	return return_status;
}

int wait(pid_t pid){
	return process_wait(pid);
}

bool create(const char *file, unsigned initial_size){
	return filesys_create(file,initial_size);
}

bool remove(const char *file){
	return filesys_remove(file);
}

int open(const char *file){
	struct thread *t=thread_current();
	struct file *temp=filesys_open(file);
	if(!temp)
		return -1;
	int fd;
	for(fd=0;fd<128;fd++)
		if(t->my_stats.file[fd]==NULL){
			break;
		}

	if(fd > 127)
		return -1;

	t->my_stats.file[fd]=temp;
	return fd+2;
}

int filesize(int fd){
	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
	case 0:
		break;
	case 1:
		break;
	default:;
		struct thread *t=thread_current();
		if(t->my_stats.file[fd-2])
			return file_length(t->my_stats.file[fd-2]);
		break;
	}
	exit(-1);
}

int read(int fd, void *buffer, unsigned size){
	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.file[fd-2])
				return file_read(t->my_stats.file[fd-2],buffer,size);
			break;
		}
	exit(-1);
}

int write(int fd, const void *buffer, unsigned size){
	if(fd<0||fd>129)
		exit(-1);

	switch(fd){
	case 0:
		return size;
		break;
	case 1:
		putbuf(buffer, size);
		return size;
		break;
	default:;
		struct thread *t=thread_current();
		if(t->my_stats.file[fd-2])
			return file_write(t->my_stats.file[fd-2],buffer,size);
		break;
	}
	exit(-1);
}

void seek(int fd, unsigned position){
	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.file[fd-2])
				file_seek(t->my_stats.file[fd-2],position);
			else
				exit(-1);
			break;
		}
}

unsigned tell(int fd){
	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.file[fd-2])
				return file_tell(t->my_stats.file[fd-2]);
			break;
		}
	exit(-1);
}

void close(int fd){
	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.file[fd-2]){
				file_close(t->my_stats.file[fd-2]);
				t->my_stats.file[fd-2]=NULL;
				return;
			}
			break;
		}
	exit(-1);
}

mapid_t mmap (int fd, void *addr){
//	/printf("hi there\n");
	struct thread *t=thread_current();
	if (!addr || !is_user_vaddr(addr) || addr != pg_round_down(addr))
		return -1;
	if(fd<0 || fd>129)
		return -1;
	switch(fd){

	case 0:
		return -1;
	case 1:
		return -1;
	default:
		fd=fd-2;
		if(!t->my_stats.file[fd])
			return -1;

		unsigned file_len=file_length(t->my_stats.file[fd]);
		if(file_len==0)
			return -1;
		unsigned extra_offset=file_len & PGMASK;
		unsigned abs_file_pages= file_len >> PGBITS;
		unsigned file_pages= (extra_offset==0)?abs_file_pages:(abs_file_pages+1);
		void *temp=addr;
		int i;
		for(i=0;i<file_pages;i++){
			if(lookup_apt(temp)!=NULL)
				return -1;
			temp+=PGSIZE;
		}

		int mid;
		for(mid=0;mid<128;mid++)
			if(t->my_stats.mfile[mid]==NULL){
				t->my_stats.mfile[mid]=malloc(sizeof(struct mmaped_files));
				break;
			}

		if(mid>127)
			return -1;

		t->my_stats.mfile[mid]->file=file_reopen(t->my_stats.file[fd]);
		t->my_stats.mfile[mid]->addr=addr;
		t->my_stats.mfile[mid]->no_of_pages=file_pages;

		temp=addr;
		///int i;
		for(i=0;i<abs_file_pages;i++){
			new_apt_entry(t->my_stats.mfile[mid]->file,i*PGSIZE,temp,NULL,PGSIZE,0,true,MMAP);
			temp+=PGSIZE;
		}
		if(extra_offset)
			new_apt_entry(t->my_stats.mfile[mid]->file,i*PGSIZE,temp,NULL,extra_offset,PGSIZE-extra_offset,true,MMAP);

		return mid;
		break;
	}

	return -1;
}
void munmap (mapid_t mid){
	struct thread *t=thread_current();
	struct mmaped_files *mfile=t->my_stats.mfile[mid];
	if(mid<0 || mid>127)
		return;
	if(!mfile)
		return;
	void *temp=mfile->addr;
	struct file *file=mfile->file;
	if(!file)
		return;
	int i;
	void *kpage;
	for(i=0;i<mfile->no_of_pages;i++){
		kpage=pagedir_get_page(t->pagedir,temp);
		if( kpage!=NULL){
			if(pagedir_is_dirty(t->pagedir,temp)){
				struct addon_page_table *entry=lookup_apt(temp);
				if(entry){
					file_seek(file,entry->ofs);
					file_write(file,temp,entry->read_bytes);
				}
			}
			pagedir_clear_page(thread_current()->pagedir,temp);
			free_frame(kpage);
		}
		apt_delete(temp);
		temp+=PGSIZE;
	}
	file_close(file);
	free(mfile);
	t->my_stats.mfile[mid]=NULL;
}
/////END FUNCTIONS

static void
syscall_handler (struct intr_frame *f)
{
	//printf ("system call! %s: ", thread_name());
	void *esp=f->esp;
	//try_grow_stack(esp);
	validate_user_address(esp,1);
	//printf("%d\n",*(int*)(esp));
	switch(*(int*)(esp)){

	case SYS_HALT:
		//printf("SYS_HALT\n");
		halt();
		break;

	case SYS_EXIT:
		//printf("SYS_EXIT\n");
		esp=esp+4;
		validate_user_address(esp,1);
		exit(*(int *)(esp));
		break;

	case SYS_EXEC:
		//printf("SYS_EXEC\n");
		esp=esp+4;
		validate_user_address(esp,1);
		validate_user_address(*(int *)esp,1);
		f->eax=exec(*(int *)(esp));
		break;

	case SYS_WAIT:
		//printf("SYS_WAIT\n");
		esp=esp+4;
		validate_user_address(esp,1);
		f->eax=wait(*(int *)(esp));
		break;

	case SYS_CREATE:
		//printf("SYS_CREATE\n");
		esp=esp+4;
		validate_user_address(esp,2);
		validate_user_address(*(int *)esp,1);
		lock_acquire(&file_lock);
		f->eax=create(*(int *)(esp),*(unsigned *)(esp+4));
		lock_release(&file_lock);

		break;

	case SYS_REMOVE:
		//printf("SYS_REMOVE\n");
		esp=esp+4;
		validate_user_address(esp,1);
		validate_user_address(*(int *)esp,1);
		lock_acquire(&file_lock);
		f->eax=remove(*(int *)(esp));
		lock_release(&file_lock);
		break;

	case SYS_OPEN:
		//printf("SYS_OPEN\n");
		esp=esp+4;
		validate_user_address(esp,1);
		//validate_buffer(*(int *)(esp+4),15, false);
		validate_user_address(*(int *)esp,1);
		lock_acquire(&file_lock);
		f->eax = open(*(int *)(esp));
		lock_release(&file_lock);
		break;

	case SYS_FILESIZE:
		//printf("SYS_FILESIZE\n");
		esp=esp+4;
		validate_user_address(esp,1);
		lock_acquire(&file_lock);
		f->eax=filesize(*(int *)(esp));
		lock_release(&file_lock);
		break;

	case SYS_READ:
		//printf("SYS_READ\n");
		esp=esp+4;
		validate_user_address(esp,3);
		//printf("passed %0xu\n",*(int *)(esp+4));
		validate_buffer(*(int *)(esp+4),*(unsigned *)(esp+8), true,esp-4);
		void *addr=*(int *)(esp+4);
		unsigned size=*(unsigned *)(esp+8);
		//validate_user_address(*(int *)(esp+4),1);
		//printf("passed 2\n");
		lock_acquire(&file_lock);
		f->eax=read(*(int *)(esp),*(int *)(esp+4),*(unsigned *)(esp+8));
		lock_release(&file_lock);
		invalidate_buffer(addr,size);
		break;

	case SYS_WRITE:
		//printf("SYS_WRITE\n");
		esp=esp+4;
		validate_user_address(esp,3);
		//validate_user_address(*(int *)(esp+4),1);
		validate_buffer(*(int *)(esp+4),*(unsigned *)(esp+8), false,esp-4);
		addr=*(int *)(esp+4);
		size=*(unsigned *)(esp+8);
		lock_acquire(&file_lock);
		f->eax=write(*(int *)(esp),*(int *)(esp+4),*(unsigned *)(esp+8));
		lock_release(&file_lock);
		invalidate_buffer(addr,size);
		//printf("in %d write: %d, written: %d\n",*(int *)(esp), *(int *)(esp+8),f->eax);
		break;

	case SYS_SEEK:
		//printf("SYS_SEEK\n");
		esp=esp+4;
		validate_user_address(esp,2);
		lock_acquire(&file_lock);
		seek(*(int *)(esp),*(unsigned *)(esp+4));
		lock_release(&file_lock);
		break;

	case SYS_TELL:
		//printf("SYS_TELL\n");
		esp=esp+4;
		validate_user_address(esp,1);
		lock_acquire(&file_lock);
		f->eax=tell(*(int *)(esp));
		lock_acquire(&file_lock);
		break;

	case SYS_CLOSE:
		//printf("SYS_CLOSE\n");
		esp=esp+4;
		validate_user_address(esp,1);
		lock_acquire(&file_lock);
		close(*(int *)(esp));
		lock_release(&file_lock);
		break;

	case SYS_MMAP:
		//printf("SYS_MMAP\n");
		esp=esp+4;
		validate_user_address(esp,2);
		lock_acquire(&file_lock);
		f->eax=mmap (*(int *)(esp), *(int *)(esp+4));
		lock_release(&file_lock);
		break;

	case SYS_MUNMAP:
		//printf("SYS_MUNMAP\n");
		esp=esp+4;
		validate_user_address(esp,1);
		lock_acquire(&file_lock);
		munmap (*(int *)(esp));
		lock_release(&file_lock);
		break;

	default:
		break;
	}

/*
	printf("%d\n", *(int*)(esp));
	esp=esp+4;
	printf("%d\n", *(int*)(esp));
	esp=esp+4;
	printf("%s\n", (char *)(*(int *)(esp)));
*/
}
