#include "filesys/free-map.h"
#include <bitmap.h>
#include <debug.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"

static struct file *free_map_file;   /* Free map file. */
static struct bitmap *free_map;      /* Free map, one bit per sector. */

/* bitmap 조작만은 무조건 lock이 걸려져야 한다. */
static struct lock bitmap_lock;

/* Initializes the free map. */
void
free_map_init (void) 
{
  free_map = bitmap_create (block_size (fs_device)); //file system 크기만큼 free-map 생성 후 false로 초기화
  if (free_map == NULL)
    PANIC ("bitmap creation failed--file system device is too large");
  
#ifdef USERPROG 
  lock_init(&bitmap_lock);
#endif

  /* root 와 free map 의 inode sector을 사용중이라고 표시한다. */
  bitmap_mark (free_map, FREE_MAP_SECTOR);
  bitmap_mark (free_map, ROOT_DIR_SECTOR);
}

/* Allocates CNT consecutive sectors from the free map and stores
   the first into *SECTORP.
   Returns true if successful, false if not enough consecutive
   sectors were available or if the free_map file could not be
   written. */
bool
free_map_allocate (size_t cnt, block_sector_t *sectorp)
{

#ifdef USERPROG
  lock_acquire(&bitmap_lock);
#endif

  block_sector_t sector = bitmap_scan_and_flip (free_map, 0, cnt, false); 
  if (sector != BITMAP_ERROR
      && free_map_file != NULL
      && !bitmap_write (free_map, free_map_file))
    {
      bitmap_set_multiple (free_map, sector, cnt, false); 
      sector = BITMAP_ERROR;
    }
  if (sector != BITMAP_ERROR)
    *sectorp = sector;

#ifdef USERPROG
  lock_release(&bitmap_lock);
#endif
  return sector != BITMAP_ERROR;
}

/* Makes CNT sectors starting at SECTOR available for use. */
void
free_map_release (block_sector_t sector, size_t cnt)
{
#ifdef USERPROG
  lock_acquire(&bitmap_lock);
#endif

  ASSERT (bitmap_all (free_map, sector, cnt));
  bitmap_set_multiple (free_map, sector, cnt, false);
  bitmap_write (free_map, free_map_file);

#ifdef USERPROG
  lock_release(&bitmap_lock);
#endif
}

/* Opens the free_map file and reads it from disk.(free map 파일의 정보를 읽어들임) */
void
free_map_open (void) 
{
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_read (free_map, free_map_file))
    PANIC ("can't read free map");
}

/* Writes the free map to disk and closes the free map file. */
void
free_map_close (void) 
{
  file_close (free_map_file);
}

/* Creates a new free map file on disk and writes the free map to it.
   init()의 do_format()에서 사용된다. */
void
free_map_create (void) 
{
  /* Create inode. */
  if (!inode_create (FREE_MAP_SECTOR, bitmap_file_size (free_map),NULL))
    PANIC ("free map creation failed");

  /* Write bitmap to file. */
  free_map_file = file_open (inode_open (FREE_MAP_SECTOR));
  if (free_map_file == NULL)
    PANIC ("can't open free map");
  if (!bitmap_write (free_map, free_map_file)) //bitmap 의 데이터를 파일(디스크)에다 기록한다.
    PANIC ("can't write free map");
}
