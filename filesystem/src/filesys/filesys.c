#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

/* Partition that contains the file system. */
struct block *fs_device;
static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
  buffer_flush();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size)
{
  block_sector_t inode_sector = 0;

  struct dir *dir=dir_open_root ();
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0)
    free_map_release (inode_sector, 1);
  else{
	  struct inode *inode=inode_open(inode_sector);
	  inode_set_directory(inode,0);
	  //printf("creating file :%s at %d::%d:%d \n: ",name,inode_sector,inode_get_inumber(inode),inode_get_self(inode));
	  inode_close(inode);
  }
  dir_close (dir);
  return success;
}
bool
filesys_create_nested (const char *name, off_t initial_size,struct dir *dir)
{
  block_sector_t inode_sector = 0;
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size)
                  && dir_add (dir, name, inode_sector));
  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
 // printf("creating file :%s at %d :%d \n: ",name,inode_sector,success);
  else{
	  struct inode *inode=inode_open(inode_sector);
	  inode_set_directory(inode,0);
	  inode_close(inode);
  }
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  dir_close (dir);
  //printf("opening file :%s at :%d:%d \n ",name,inode_get_inumber(inode),inode_get_self(inode));
  return file_open (inode);
}

struct file *
filesys_open_nested (const char *name,struct dir *dir)
{

  struct inode *inode = NULL;

  if (dir != NULL)
    dir_lookup (dir, name, &inode);
  //printf("opening file :%s at :%u\n ",name,inode_get_inumber(inode));
  return file_open (inode);
}
/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  struct dir *dir = dir_open_root ();
  bool success = dir != NULL && dir_remove (dir, name);
  dir_close (dir); 
 // printf("removing file :%s \n ",name);
  return success;
}
bool
filesys_remove_nested (const char *name,struct dir *dir)
{

  bool success = dir != NULL && dir_remove (dir, name);

  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  struct dir *root_dir=dir_open_root();
  dir_add(root_dir,".",ROOT_DIR_SECTOR);
  dir_add(root_dir,"..",ROOT_DIR_SECTOR);
  dir_close(root_dir);
  free_map_close ();
  printf ("done.\n");
}
