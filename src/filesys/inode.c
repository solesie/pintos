#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#include "filesys/cache.h"

/* 추후 inode reopen을 호출하도록 어떻게든 수정 */

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* 특정 inode에 대해 접근을 막는 lock.(very very strongly)
   동일한 inode는 sync 하게 open 하는데 필요하다. */
static struct lock** inode_lock;

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos < inode->data.length)
    return inode->data.start + pos / BLOCK_SECTOR_SIZE;
  else
    return -1;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

#ifdef USERPROG
/* open_inodes list에 대해 reader-writer problem 적용 */
int inodes_list_readcnt;
struct lock inl_rc_mutex;
struct semaphore inodes_list_w;

struct lock inode_ref_mutex;
#endif


/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);

#ifdef USERPROG
  inodes_list_readcnt = 0;
  lock_init(&inl_rc_mutex);
  lock_init(&inode_ref_mutex);
  sema_init(&inodes_list_w, 1);
  int sector_num = block_size (fs_device);
  inode_lock = malloc(sizeof(struct lock*)*sector_num);
  for(int i = 0;i < sector_num; ++i){
    inode_lock[i] = malloc(sizeof(struct lock));
    lock_init(inode_lock[i]);
  }
#endif
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails.
   start에는 length 만큼 길이가 충분한 sectors의 시작번호가 초기화된다. */
bool
inode_create (block_sector_t sector, off_t length, block_sector_t* start)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      size_t sectors = bytes_to_sectors (length);
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (free_map_allocate (sectors, &disk_inode->start)) //start에는 length 만큼 크기가 충분한 섹터의 시작번호 저장
        {

#ifdef USERPROG
          if(start != NULL)
            *start = disk_inode->start;
#endif

          block_write (fs_device, sector, disk_inode);
          if (sectors > 0) 
            {
              static char zeros[BLOCK_SECTOR_SIZE];
              size_t i;
              
              for (i = 0; i < sectors; i++) 
                block_write (fs_device, disk_inode->start + i, zeros);
            }
          success = true; 
        } 
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
#ifdef USERPROG
  /* 동일한 inode는 하나만 열고 닫힐 수 있도록 한다. */
  lock_acquire(inode_lock[sector]);
#endif

  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open.
     open_inods list를 reading 하는 상황이다. */
#ifdef USERPROG
  lock_acquire(&inl_rc_mutex);
  ++inodes_list_readcnt;
  if(inodes_list_readcnt == 1)
    sema_down(&inodes_list_w);
  lock_release(&inl_rc_mutex);
#endif
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode->open_cnt++;

#ifdef USERPROG
          lock_release(inode_lock[sector]);
          lock_acquire(&inl_rc_mutex);
          --inodes_list_readcnt;
          if(inodes_list_readcnt == 0)
            sema_up(&inodes_list_w);
          lock_release(&inl_rc_mutex);
#endif

          return inode; 
        }
    }

#ifdef USERPROG
  lock_acquire(&inl_rc_mutex);
  --inodes_list_readcnt;
  if(inodes_list_readcnt == 0)
    sema_up(&inodes_list_w);
  lock_release(&inl_rc_mutex);
#endif

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL){

#ifdef USERPROG
    lock_release(inode_lock[sector]);
#endif

    return NULL;
  }

  /* Initialize. */
#ifdef USERPROG
  sema_down(&inodes_list_w); //list_push 는 list에 writing 하는 상황이다.
#endif
  list_push_front (&open_inodes, &inode->elem);
#ifdef USERPROG
  sema_up(&inodes_list_w);
#endif
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;

#ifdef USERPROG
  inode->read_cnt = 0;
  sema_init(&(inode->w), 1);
  lock_init(&(inode->inode_readcnt_mutex));
#endif

  inode->removed = false;
  block_read (fs_device, inode->sector, &inode->data);


#ifdef USERPROG
  lock_release(inode_lock[sector]);
#endif

  return inode;
}

/* Reopens and returns INODE.
   이 함수나 inode_open을 통해서만 open_cnt는 늘어날 수 있다. */
struct inode *
inode_reopen (struct inode *inode)
{
  lock_acquire(&inode_ref_mutex);
  if(inode == NULL) return NULL;
  block_sector_t sector = inode->sector;
  lock_release(&inode_ref_mutex);

  lock_acquire(inode_lock[sector]);
  inode->open_cnt++;
  lock_release(inode_lock[sector]);
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks.
   이 함수를 통해서만 inode->open_cnt는 줄어들 수 있고, inode는 free될 수 있다.
   inode가 free 되었다면 true를 반환한다. */
bool
inode_close (struct inode *inode)
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return true;

#ifdef USERPROG
  bool success = false;
  block_sector_t sector = inode->sector;
  /* 동일한 inode는 하나만 열고 닫힐 수 있도록 한다. */
  lock_acquire(inode_lock[sector]);
#endif
  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. inodes_list에 writing 하는 상황이다. */
#ifdef USERPROG
      sema_down(&inodes_list_w);
      list_remove (&inode->elem);
      sema_up(&inodes_list_w);
#endif

      /* Deallocate blocks if removed. */
      if (inode->removed) //이값은 inode_open을 통해 이미 열은 후에만 변경 가능하다.
        {
          free_map_release (inode->sector, 1);
          free_map_release (inode->data.start,
                            bytes_to_sectors (inode->data.length)); 
        }
      free (inode);
      success = true;
    }
#ifdef USERPROG
  lock_release(inode_lock[sector]);
#endif
  return success;
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  /* Critical section
     Reading happens */
  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Read full sector directly into caller's buffer. */
          block_read (fs_device, sector_idx, buffer + bytes_read);
        }
      else 
        {
          /* Read sector into bounce buffer, then partially copy
             into caller's buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }
          block_read (fs_device, sector_idx, bounce);
          memcpy (buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.)
   파일 크기가 늘어나는 것은 아직 고려되지 않는다.(prj2) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
#ifdef USERPROG
  if(inode->deny_write_cnt > 0)
    return 0;
#endif
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  /* Critical section
     Writing happens */
  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;
        
      if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
        {
          /* Write full sector directly to disk. */
          block_write (fs_device, sector_idx, buffer + bytes_written);
        }
      else 
        {
          /* We need a bounce buffer. */
          if (bounce == NULL) 
            {
              bounce = malloc (BLOCK_SECTOR_SIZE);
              if (bounce == NULL)
                break;
            }

          /* If the sector contains data before or after the chunk
             we're writing, then we need to read in the sector
             first.  Otherwise we start with a sector of all zeros. */
          if (sector_ofs > 0 || chunk_size < sector_left) 
            block_read (fs_device, sector_idx, bounce);
          else
            memset (bounce, 0, BLOCK_SECTOR_SIZE);
          memcpy (bounce + sector_ofs, buffer + bytes_written, chunk_size);
          block_write (fs_device, sector_idx, bounce);
        }

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);
  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener.(file table이 inode opener이다.) */
void
inode_deny_write (struct inode *inode) 
{
  sema_down(&inode->w);
  inode->deny_write_cnt++;
  sema_up(&inode->w);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  sema_down(&inode->w);
  inode->deny_write_cnt--;
  sema_up(&inode->w);
}

/* Returns the length, in bytes, of INODE's data. 
   Writing 중인 가능성이 있다면 호출하는데 주의해야한다. */
off_t
inode_length (const struct inode *inode)
{
  off_t len = inode->data.length;
  return len;
}
