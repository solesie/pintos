#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"

#include "threads/synch.h"
#include "filesys/cache.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

/* Initializes the file system module.
   If FORMAT is true, reformats the file system.
   이 함수 후에 "Boot is complete" 메세지 출력.(핀토스의 파일 시스템을 초기화한다.) */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();
  buffer_cache_init ();

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
  buffer_cache_terminate();
}

/* Creates a file named NAME with the given INITIAL_SIZE.
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *path, off_t initial_size, int is_dir) 
{
  block_sector_t inode_sector = 0;
  bool inode_freed = false;

  char directory[ strlen(path) ];
  char file_name[ strlen(path) ];
  split_path(path, directory, file_name);
  struct dir *dir = dir_open_path (directory);


  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector) //빈 섹터를 찾고
                  && inode_create (inode_sector, initial_size, is_dir) //섹터에 file의 inode를 만들고, 파일을 섹터에 할당하고
                  && dir_add (dir, file_name, inode_sector, is_dir));
  if (!success){
    if(inode_sector != 0) //free_map_allocate(1, ) 에서 할당된 부분을 해제한다.
      free_map_release (inode_sector, 1);
#ifdef USERPROG
    if(dir != NULL) //dir_open_root에서 늘어난 dir->inode->open_cnt 를 줄인다.
      inode_freed = inode_close(dir_get_inode(dir));
    if(inode_freed) //만약 dir->inode가 free 되었다면 dir도 free 한다.
      free(dir);
#endif
  }
  else{
    dir_close (dir);
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
  int name_len = strlen(name);
  if (name_len == 0) return NULL;

  char directory[ name_len + 1 ];
  char file_name[ name_len + 1 ];
  split_path(name, directory, file_name);
  struct dir *dir = dir_open_path (directory);
  struct inode *inode = NULL;

  // removed directory handling
  if (dir == NULL) return NULL;

  if (strlen(file_name) > 0) {
    dir_lookup (dir, file_name, &inode);
    dir_close (dir);
  }
  else { // empty filename
    inode = dir_get_inode (dir);
  }

  // removed file handling
  if (inode == NULL || inode->removed)
    return NULL;

  return file_open (inode);
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool filesys_remove (const char *name) {
  char directory[ strlen(name) ];
  char file_name[ strlen(name) ];
  split_path(name, directory, file_name);
  struct dir *dir = dir_open_path (directory);

  bool success = (dir != NULL && dir_remove (dir, file_name));
  dir_close (dir);

  return success;
}

bool filesys_chdir (const char *name){
  struct dir *dir = dir_open_path (name);
  if(dir == NULL)
    return false;

  // switch
  dir_close (thread_current()->cwd);
  thread_current()->cwd = dir;
  return true;
}

/* Formats the file system.
   bitmap의 inode 생성 후 파일(디스크)에 기록
   root directory의 inode 생성.  */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}
