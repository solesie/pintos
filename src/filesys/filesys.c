#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"

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
filesys_create (const char *name, off_t initial_size) 
{
  block_sector_t inode_sector = 0;
  bool inode_freed = false;
#ifdef USERPROG
  block_sector_t data_sector = 1;
#endif
  struct dir *dir = dir_open_root (); //아직까지 루트 디렉토리에만 생성이 가능하다.
  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector) //빈 섹터를 찾고
                  && inode_create (inode_sector, initial_size, &data_sector) //섹터에 file의 inode를 만들고, 파일을 섹터에 할당하고
                  && dir_add (dir, name, inode_sector)); //name이 이미 존재하는지 확인한다. 없으면 기록한다.
  if (!success){
    if(inode_sector != 0) //free_map_allocate(1, ) 에서 할당된 부분을 해제한다.
      free_map_release (inode_sector, 1);
#ifdef USERPROG
    if(data_sector != 0) //inode_create()의 free_map_allocated() 에서 할당된 부분을 해제한다.
      free_map_release (data_sector, bytes_to_sectors (initial_size));
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
  struct dir *dir = dir_open_root ();
  struct inode *inode = NULL;
  bool success = false;

  if (dir != NULL) {
    success = dir_lookup (dir, name, &inode); //이미 열려 있다면 inode_reopen을 호출한다.
  }
  dir_close (dir);
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

  return success;
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
