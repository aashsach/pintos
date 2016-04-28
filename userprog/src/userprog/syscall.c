#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"

typedef int pid_t;
static void syscall_handler (struct intr_frame *);

static struct lock file_lock;

void validate_user_address(int *esp, int no){
	uint32_t *pd= thread_current()->pagedir;
	int i;
	for(i=0;i<no;i++)
		if(!(esp + 4*i) || !is_user_vaddr(esp + 4*i) || !pagedir_get_page(pd,(esp + 4*i)))
			exit(-1);
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
	for(i=0;i<128;i++){
		if(t->my_stats.open_file[i]){
			file_close(t->my_stats.open_file[i]);
			t->my_stats.open_file[i]=NULL;
		}
	}
	file_close(t->my_stats.my_exectutable);
	print_terminating_message(status);
	if(t->my_stats.pos){
		t->my_stats.pos->return_status=status;
		sema_up(&t->my_stats.pos->child_dead);
	}
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
		if(t->my_stats.open_file[fd]==NULL)
			break;
	if(fd > 127)
		return -1;
	t->my_stats.open_file[fd]=temp;
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
		if(t->my_stats.open_file[fd-2])
			return file_length(t->my_stats.open_file[fd-2]);
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
			if(t->my_stats.open_file[fd-2])
				return file_read(t->my_stats.open_file[fd-2],buffer,size);
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
		if(t->my_stats.open_file[fd-2])
			return file_write(t->my_stats.open_file[fd-2],buffer,size);
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
			if(t->my_stats.open_file[fd-2])
				file_seek(t->my_stats.open_file[fd-2],position);
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
			if(t->my_stats.open_file[fd-2])
				return file_tell(t->my_stats.open_file[fd-2]);
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
			if(t->my_stats.open_file[fd-2]){
				file_close(t->my_stats.open_file[fd-2]);
				t->my_stats.open_file[fd-2]=NULL;
				return;
			}
			break;
		}
	exit(-1);
}
/////END FUNCTIONS

static void
syscall_handler (struct intr_frame *f)
{
	//printf ("system call! %s: ", thread_name());
	void *esp=f->esp;
	validate_user_address(esp,1);

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
		validate_user_address(*(int *)(esp+4),1);
		lock_acquire(&file_lock);
		f->eax=read(*(int *)(esp),*(int *)(esp+4),*(unsigned *)(esp+8));
		lock_release(&file_lock);
		break;

	case SYS_WRITE:
		//printf("SYS_WRITE\n");
		esp=esp+4;
		validate_user_address(esp,3);
		validate_user_address(*(int *)(esp+4),1);
		lock_acquire(&file_lock);
		f->eax=write(*(int *)(esp),*(int *)(esp+4),*(unsigned *)(esp+8));
		lock_release(&file_lock);
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
