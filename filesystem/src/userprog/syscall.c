#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"



typedef int pid_t;
static void syscall_handler (struct intr_frame *);


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
	//if(file_lock.holder==thread_current())
		//lock_release(&file_lock);
	for(i=0;i<128;i++){
		if(t->my_stats.ofd[i].open_ptr){
			if(t->my_stats.ofd[i].is_dir==false)
				file_close(t->my_stats.ofd[i].open_ptr);
			else
				dir_close(t->my_stats.ofd[i].open_ptr);
			t->my_stats.ofd[i].open_ptr=NULL;
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
	//printf("file:%s\n",file);
	if(!file || strlen(file)==0){
		return false;
	}

	char *path=malloc(strlen(file)+1);
	struct dir *parent=NULL;
	char *directory=path_parser(file,path,&parent);
	//printf("file:%s\n",directory);
	if(!parent){
		//printf("parent NULL\n");
		free(path);
		return false;
	}
	if(!directory){
		dir_close(parent);
		free(path);
		return false;
	}
	bool success= filesys_create_nested(directory,initial_size,parent);
	dir_close(parent);
	free(path);
	//printf("success:%d\n",success);
	return success;
}

bool remove(const char *file){
	if(!file || strlen(file)==0){
			return false;
	}
	//printf("trying %s\n",file);
	char *path=malloc(strlen(file)+1);
	struct dir *parent=NULL;
	char *directory=path_parser(file,path,&parent);
	if(!parent){
		//printf("parent NULL\n");
		free(path);
		return false;
	}
	if(!directory){
		//printf("directory null %s\n",directory);
		free(path);
		return false;
	}
	struct inode *inode;
	if(!dir_lookup(parent,directory,&inode)){
		dir_close(parent);
		free(path);
		return false;
	}
	if(inode_is_directory(inode)){
		struct dir *dir=dir_open(inode);
		if(!dir_is_open(dir) && dir_count(dir)<=2){
			//printf("removing directory:%s at :%d\n",file,inode_get_inumber(dir_get_inode(dir)));
			dir_close(dir);
			bool success=dir_remove(parent,directory);
			dir_close(parent);
			free(path);
			return success;
		}
		else{
			dir_close(dir);
			dir_close(parent);
			free(path);
			//printf("yo\n");
			return false;
		}
	}
	else{
		inode_close(inode);
		bool success=filesys_remove_nested(directory,parent);
		dir_close(parent);
		//printf("%d\n",success);
		free(path);
		return success;
	}
}

int open(const char *file){

	if(!file || strlen(file)==0){
		return -1;
	}
	if(strlen(file)!=1 && *(file+strlen(file)-1)=='/'){
		return -1;
	}
	struct thread *t=thread_current();
	int fd;
	for(fd=0;fd<128;fd++)
		if(t->my_stats.ofd[fd].open_ptr==NULL)
			break;
	if(fd > 127)
		return -1;

	char *path=malloc(strlen(file)+1);
	struct dir *parent=NULL;
	char *directory=path_parser(file,path,&parent);
	//printf("open: %s\n",file);
	if(!parent){
		//printf("parent NULL\n");
		free(path);
		return -1;
	}

	if(!directory){
		//printf("directory null %s\n",directory);
		t->my_stats.ofd[fd].open_ptr=parent;
		t->my_stats.ofd[fd].is_dir=true;
		free(path);
		return fd+2;
	}
	struct inode *inode;
	if(!dir_lookup(parent,directory,&inode)){
		dir_close(parent);
		free(path);
		return -1;
	}

	if(inode_is_directory(inode)){
		//printf("dir\n");
		t->my_stats.ofd[fd].open_ptr=dir_open(inode);
		t->my_stats.ofd[fd].is_dir=true;
		dir_close(parent);
		free(path);
		return fd+2;
	}
	t->my_stats.ofd[fd].open_ptr=file_open(inode);
	t->my_stats.ofd[fd].is_dir=false;
	dir_close(parent);
	free(path);
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
		if(t->my_stats.ofd[fd-2].open_ptr && t->my_stats.ofd[fd-2].is_dir==false)
			return file_length(t->my_stats.ofd[fd-2].open_ptr);
		break;
	}
	return -1;
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
			if(t->my_stats.ofd[fd-2].open_ptr && t->my_stats.ofd[fd-2].is_dir==false)
				return file_read(t->my_stats.ofd[fd-2].open_ptr,buffer,size);
			break;
		}
	return -1;
}

int write(int fd, const void *buffer, unsigned size){
	if(fd<0||fd>129)
		exit(-1);

	switch(fd){
	case 0:
		return -1;
		break;
	case 1:
		putbuf(buffer, size);
		return size;
		break;
	default:;
		struct thread *t=thread_current();
		if(t->my_stats.ofd[fd-2].open_ptr && t->my_stats.ofd[fd-2].is_dir==false)
			return file_write(t->my_stats.ofd[fd-2].open_ptr,buffer,size);
		break;
	}
	//printf("wth\n");
	return -1;
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
			if(t->my_stats.ofd[fd-2].open_ptr && t->my_stats.ofd[fd-2].is_dir==false)
				file_seek(t->my_stats.ofd[fd-2].open_ptr,position);
			else
				return;
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
			if(t->my_stats.ofd[fd-2].open_ptr&& t->my_stats.ofd[fd-2].is_dir==false)
				return file_tell(t->my_stats.ofd[fd-2].open_ptr);
			break;
		}
	return 0;
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
			if(t->my_stats.ofd[fd-2].open_ptr){
				if(t->my_stats.ofd[fd-2].is_dir)
					dir_close(t->my_stats.ofd[fd-2].open_ptr);
				else
					file_close(t->my_stats.ofd[fd-2].open_ptr);
				t->my_stats.ofd[fd-2].open_ptr=NULL;
				t->my_stats.ofd[fd-2].is_dir=false;
				return;
			}
			break;
		}

}

bool chdir (const char *dir){
	if(!dir || strlen(dir)==0)
		return false;
	char *path=malloc(strlen(dir)+1);
	struct dir *parent=NULL;
	char *directory=path_parser(dir,path,&parent);
	if(!parent){
		free(path);
		return false;
	}
	if(!directory){
		thread_current()->my_stats.pwd_sector=inode_get_inumber(dir_get_inode(parent));
		dir_close(parent);
		free(path);
		return true;
	}

	struct inode *inode;
	if(!dir_lookup(parent,directory,&inode)){
		dir_close(parent);
		free(path);
		return false;
	}
	thread_current()->my_stats.pwd_sector=inode_get_inumber(inode);
	inode_close(inode);
	dir_close(parent);
	free(path);
	return true;
}
bool mkdir (const char *dir){
	//printf("%s\n",dir);
	if(!dir || strlen(dir)==0){
		return false;
	}
	char *path=malloc(strlen(dir)+1);
	struct dir *parent=NULL;
	char *directory=path_parser(dir,path,&parent);
	if(!parent){
		//printf("parent NULL\n");
		free(path);
		return false;
	}
	if(!directory){
		//printf("directory null %s\n",directory);
		dir_close(parent);
		free(path);
		return false;
	}
	//printf("\n%s\n\n",directory);
	uint32_t sector;
	struct inode *inode;
	if(dir_lookup(parent,directory,&inode)){
		//printf("found\n");
		inode_close(inode);
		dir_close(parent);
		free(path);
		return false;
	}
	if(!free_map_allocate(1,&sector)){
		//printf("cannoot allocate\n");
		dir_close(parent);
		free(path);
		return false;
	}
	if(!dir_create(sector,0)){
		//printf("cannot create\n");
		free_map_release(sector,1);
		dir_close(parent);
		free(path);
		return false;
	}
	if(!dir_add(parent,directory,sector)){
		//to-do:remove inode
		//printf("cannot add\n");
		free_map_release(sector,1);
		dir_close(parent);
		free(path);
		return false;
	}
	struct dir *new_dir=dir_open(inode_open(sector));
	dir_add(new_dir,".",sector);
	dir_add(new_dir,"..",inode_get_inumber(dir_get_inode(parent)));
	//printf("created directory:%s at :%d\n",dir,inode_get_inumber(dir_get_inode(new_dir)));
	dir_close(new_dir);
	dir_close(parent);
	free(path);

	return true;
}
bool readdir (int fd, char name[14 + 1]){
	bool suc=false;
	if(fd<0||fd>129)
		return false;
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.ofd[fd-2].open_ptr && t->my_stats.ofd[fd-2].is_dir==true){
				suc= dir_readdir(t->my_stats.ofd[fd-2].open_ptr,name);
			}
			break;
	}
		return suc;
}
bool isdir (int fd){
	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.ofd[fd-2].open_ptr)
				return t->my_stats.ofd[fd-2].is_dir;
			break;
	}
	return false;
}
int inumber (int fd){

	if(fd<0||fd>129)
		exit(-1);
	switch(fd){
		case 0:
			break;
		case 1:
			break;
		default:;
			struct thread *t=thread_current();
			if(t->my_stats.ofd[fd-2].open_ptr){
				if(t->my_stats.ofd[fd-2].is_dir){
					struct dir *dir=t->my_stats.ofd[fd-2].open_ptr;
					return inode_get_inumber(dir_get_inode(dir));
				}
				else{
					struct file *file=t->my_stats.ofd[fd-2].open_ptr;
					return inode_get_inumber(file_get_inode(file));
				}
			}
			break;
	}
	return -1;
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
		//lock_acquire(&file_lock);
		f->eax=create(*(int *)(esp),*(unsigned *)(esp+4));
	//	lock_release(&file_lock);
		break;

	case SYS_REMOVE:
		//printf("SYS_REMOVE\n");
		esp=esp+4;
		validate_user_address(esp,1);
		validate_user_address(*(int *)esp,1);
		//lock_acquire(&file_lock);
		f->eax=remove(*(int *)(esp));
		//lock_release(&file_lock);
		break;

	case SYS_OPEN:
		//printf("SYS_OPEN\n");
		esp=esp+4;
		validate_user_address(esp,1);
		validate_user_address(*(int *)esp,1);
		//lock_acquire(&file_lock);
		f->eax = open(*(int *)(esp));
		//lock_release(&file_lock);
		break;

	case SYS_FILESIZE:
		//printf("SYS_FILESIZE\n");
		esp=esp+4;
		validate_user_address(esp,1);
		//lock_acquire(&file_lock);
		f->eax=filesize(*(int *)(esp));
		//lock_release(&file_lock);
		break;

	case SYS_READ:
		//printf("SYS_READ\n");
		esp=esp+4;
		validate_user_address(esp,3);
		validate_user_address(*(int *)(esp+4),1);
		//lock_acquire(&file_lock);
		f->eax=read(*(int *)(esp),*(int *)(esp+4),*(unsigned *)(esp+8));
		//lock_release(&file_lock);
		break;

	case SYS_WRITE:
		//printf("SYS_WRITE\n");
		esp=esp+4;
		validate_user_address(esp,3);
		validate_user_address(*(int *)(esp+4),1);
		//lock_acquire(&file_lock);
		f->eax=write(*(int *)(esp),*(int *)(esp+4),*(unsigned *)(esp+8));
		//lock_release(&file_lock);
		//printf("in %d write: %d, written: %d\n",*(int *)(esp), *(int *)(esp+8),f->eax);
		break;

	case SYS_SEEK:
		//printf("SYS_SEEK\n");
		esp=esp+4;
		validate_user_address(esp,2);
		//lock_acquire(&file_lock);
		seek(*(int *)(esp),*(unsigned *)(esp+4));
		///lock_release(&file_lock);
		break;

	case SYS_TELL:
		//printf("SYS_TELL\n");
		esp=esp+4;
		validate_user_address(esp,1);
		//lock_acquire(&file_lock);
		f->eax=tell(*(int *)(esp));
		//lock_acquire(&file_lock);
		break;

	case SYS_CLOSE:
		//printf("SYS_CLOSE\n");
		esp=esp+4;
		validate_user_address(esp,1);
//		lock_acquire(&file_lock);
		close(*(int *)(esp));
	//	lock_release(&file_lock);
		break;

	case SYS_CHDIR:
		esp=esp+4;
		validate_user_address(esp,1);
//		lock_acquire(&file_lock);
		f->eax=chdir(*(int *)(esp));
//		lock_release(&file_lock);
		break;

	case SYS_MKDIR:
		esp=esp+4;
		validate_user_address(esp,1);
//		lock_acquire(&file_lock);
		f->eax=mkdir(*(int *)(esp));
//		lock_release(&file_lock);
		break;

	case SYS_READDIR:
		esp=esp+4;
		validate_user_address(esp,2);
	//	lock_acquire(&file_lock);
		f->eax=readdir(*(int *)(esp),*(int *)(esp+4));
		//lock_release(&file_lock);
		break;

	case SYS_ISDIR:
		esp=esp+4;
		validate_user_address(esp,1);
		//lock_acquire(&file_lock);
		f->eax=isdir(*(int *)(esp));
		//lock_release(&file_lock);
		break;

	case SYS_INUMBER:
		esp=esp+4;
		validate_user_address(esp,1);
		//lock_acquire(&file_lock);
		f->eax=inumber(*(int *)(esp));
		//lock_release(&file_lock);
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

