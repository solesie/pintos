#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "threads/malloc.h"

#include "filesys/cache.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* 특정 inode에 대해 접근을 막는 lock.(very very strongly)
   동일한 inode는 sync 하게 open 하는데 필요하다. */
static struct lock** inode_lock;

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


static inline size_t
min (size_t a, size_t b)
{
  return a < b ? a : b;
}


/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS.
   INODE의 pos번째 bytes가 위치한 sector를 반환한다. */
static block_sector_t
byte_to_sector (const struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (0 <= pos && pos < inode->data.length) {
    struct inode_disk* idisk = &inode->data;
    off_t pos_to_sectors = pos / BLOCK_SECTOR_SIZE;

    off_t start = 0, end = NUM_DIRECT_BLOCKS;
    
    // direct blocks
    if (pos_to_sectors < end) {
      return idisk->direct_blocks[pos_to_sectors];
    }

    // indirect block
    start = end;
    end += NUM_POINTER_BLOCKS;
    if (pos_to_sectors < end) {
      block_sector_t indirect_block[NUM_POINTER_BLOCKS];
      buffer_cache_read (idisk->indirect_block, indirect_block, 0, BLOCK_SECTOR_SIZE);

      return indirect_block[pos_to_sectors - start];
    }

    // doubly indirect block
    start = end;
    end += NUM_POINTER_BLOCKS * NUM_POINTER_BLOCKS;
    if (pos_to_sectors < end) {
      block_sector_t first_indirect_block[NUM_POINTER_BLOCKS];
      buffer_cache_read (idisk->doubly_indirect_block, first_indirect_block, 0, BLOCK_SECTOR_SIZE);

      off_t first_offt =  (pos_to_sectors - start) / NUM_POINTER_BLOCKS;
      block_sector_t second_indirect_block[NUM_POINTER_BLOCKS];
      buffer_cache_read (first_indirect_block[first_offt], second_indirect_block, 0, BLOCK_SECTOR_SIZE);

      off_t second_offt = (pos_to_sectors - start) % NUM_POINTER_BLOCKS;
      return second_indirect_block[second_offt];
    }
  }
  else
    return -1;
}



/* indirect_block_root 이 나타내는 indirect_blocks의 개수를
   num_indirect_block 로 설정한다. */
static bool inode_set_indirect_block(block_sector_t* indirect_block_root, off_t num_indirect_block){
  static char zeros[BLOCK_SECTOR_SIZE];

  // indirect_block_root 이 아직 초기화되지 않은 경우, indirect_block을 할당해주고 0으로 초기화한다.
  if(*indirect_block_root == 0){
    if(!free_map_allocate(1, indirect_block_root))
      return false;
    buffer_cache_write(*indirect_block_root, zeros, 0, BLOCK_SECTOR_SIZE);
  }

  // indirect_block을 읽어온다.
  block_sector_t indirect_block[NUM_POINTER_BLOCKS];
  buffer_cache_read(*indirect_block_root, indirect_block, 0, BLOCK_SECTOR_SIZE);

  for(int i = 0; i < num_indirect_block; ++i){
    // 기존에 유효하지 않은 indirect block의 경우 새로 할당하고 0으로 초기화한다.
    if(indirect_block[i] == 0){
      if(! free_map_allocate(1, &indirect_block[i]))
        return false;
      buffer_cache_write(indirect_block[i], zeros, 0, BLOCK_SECTOR_SIZE);
    }
  }

  //indirect block 을 저장한다.
  buffer_cache_write(*indirect_block_root, indirect_block, 0, BLOCK_SECTOR_SIZE);
  return true;
}


/* idoubly_block_root 이 나타내는 doubly blocks의 개수를
   num_doubly_block 로 설정한다. */
static bool 
inode_set_doubly_indirect_block(block_sector_t* doubly_block_root, size_t num_doubly_block)
{
  static char zeros[BLOCK_SECTOR_SIZE];

  // doubly_block_root 이 아직 초기화되지 않은 경우, first indirect block을 할당해주고 0으로 초기화한다.
  if(*doubly_block_root == 0){
    if(!free_map_allocate(1, doubly_block_root))
      return false;
    buffer_cache_write(*doubly_block_root, zeros, 0, BLOCK_SECTOR_SIZE);
  }

  //first indirect_block을 읽어온다.
  block_sector_t first_indirect_block[NUM_POINTER_BLOCKS];
  buffer_cache_read(*doubly_block_root, first_indirect_block, 0, BLOCK_SECTOR_SIZE);

  int first_offt = (num_doubly_block - 1) / NUM_POINTER_BLOCKS;
  for(int i = 0; i <= first_offt; ++i){

    //유효하지 않은 first_indirect_block의 경우 second_indirect_block을 할당하고 초기화한다.
    if(first_indirect_block[i] == 0){
      if(! free_map_allocate(1, &first_indirect_block[i]))
        return false;
      buffer_cache_write(first_indirect_block[i], zeros, 0, BLOCK_SECTOR_SIZE);
    }

    // second indirect block을 읽어온다.
    block_sector_t second_indirect_block[NUM_POINTER_BLOCKS];
    buffer_cache_read(first_indirect_block[i], second_indirect_block, 0, BLOCK_SECTOR_SIZE);

    // 마지막 first indirect block entry의 경우에는 second indirect block을 읽을 때
    // 128개 전부가 아닌 일부만 읽어야 한다.
    int second_offt = i == first_offt ? ((num_doubly_block - 1) % NUM_POINTER_BLOCKS) : (NUM_POINTER_BLOCKS - 1);

    for(int j = 0; j <= second_offt; ++j){
      //유효하지 않은 second_indirect_block의 경우 데이터를 저장할 block을 할당하고 초기화한다.
      if(second_indirect_block[j] == 0){
        if(! free_map_allocate(1, &second_indirect_block[j]))
          return false;
        buffer_cache_write(second_indirect_block[j], zeros, 0, BLOCK_SECTOR_SIZE);
      }
    }

    buffer_cache_write(first_indirect_block[i], second_indirect_block, 0, BLOCK_SECTOR_SIZE);
  }

  buffer_cache_write(*doubly_block_root, first_indirect_block, 0, BLOCK_SECTOR_SIZE);
  return true;
}


/* idisk->length를 bytes로 설정한다.
   그 후 적절한 업데이트를 실행한다. */
static bool inode_set_file_length(struct inode_disk* idisk, off_t new_bytes){
  // idisk의 크기를 줄일수는 없고, 최대 파일의 크기를 넘을 수는 없다.
  if(idisk->length > new_bytes) return false;
  if(new_bytes > MAX_FILE_LENGTH) return false;

  size_t new_blocks = bytes_to_sectors(new_bytes);
  static char zeros[BLOCK_SECTOR_SIZE];

  /* direct blocks */
  int num_direct_blocks = min(new_blocks, NUM_DIRECT_BLOCKS);
  int i;
  for (i = 0; i < num_direct_blocks; ++ i) {
    if (idisk->direct_blocks[i] == 0) {
      if(! free_map_allocate (1, &idisk->direct_blocks[i]))
        return false;
      buffer_cache_write (idisk->direct_blocks[i], zeros, 0, BLOCK_SECTOR_SIZE);
    }
  }
  new_blocks -= num_direct_blocks;
  if(new_blocks == 0) return true;

  // indirect block
  int num_indirect_block = min(new_blocks, NUM_POINTER_BLOCKS);
  if(! inode_set_indirect_block (&idisk->indirect_block, num_indirect_block))
    return false;
  new_blocks -= num_indirect_block;
  if(new_blocks == 0) return true;

  // doubly indirect block
  int num_doubly_indirect_block = min(new_blocks, NUM_POINTER_BLOCKS * NUM_POINTER_BLOCKS);
  if(! inode_set_doubly_indirect_block (& idisk->doubly_indirect_block, num_doubly_indirect_block))
    return false;
  new_blocks -= num_doubly_indirect_block;
  if(new_blocks == 0) return true;

  ASSERT (new_blocks == 0);
  return false;
}


static void
inode_deallocate_indirect (block_sector_t entry, size_t num_sectors, int level)
{
  // only supports 2-level indirect block scheme as of now
  ASSERT (level <= 2);

  if (level == 0) {
    free_map_release (entry, 1);
    return;
  }

  block_sector_t indirect_block[NUM_POINTER_BLOCKS];
  buffer_cache_read(entry, indirect_block, 0, BLOCK_SECTOR_SIZE);

  size_t unit = (level == 1 ? 1 : NUM_POINTER_BLOCKS);
  size_t i, l = DIV_ROUND_UP (num_sectors, unit);

  for (i = 0; i < l; ++ i) {
    size_t subsize = min(num_sectors, unit);
    inode_deallocate_indirect (indirect_block[i], subsize, level - 1);
    num_sectors -= subsize;
  }

  ASSERT (num_sectors == 0);
  free_map_release (entry, 1);
}


static
bool inode_deallocate (struct inode *inode)
{
  off_t file_length = inode->data.length; // bytes
  if(file_length < 0) return false;

  // (remaining) number of sectors, occupied by this file.
  size_t num_sectors = bytes_to_sectors(file_length);

  // direct blocks
  int num_direct_blocks = min(num_sectors, NUM_DIRECT_BLOCKS);
  for (int i = 0; i < num_direct_blocks; ++ i) {
    free_map_release (inode->data.direct_blocks[i], 1);
  }
  num_sectors -= num_direct_blocks;

  // indirect block
  int num_indirect_block = min(num_sectors, NUM_POINTER_BLOCKS);
  if(num_indirect_block > 0) {
    inode_deallocate_indirect (inode->data.indirect_block, num_indirect_block, 1);
    num_sectors -= num_indirect_block;
  }

  // doubly indirect block
  int num_doubly_indirect_block = min(num_sectors, NUM_POINTER_BLOCKS * NUM_POINTER_BLOCKS);
  if(num_doubly_indirect_block > 0) {
    inode_deallocate_indirect (inode->data.doubly_indirect_block, num_doubly_indirect_block, 2);
    num_sectors -= num_doubly_indirect_block;
  }

  ASSERT (num_sectors == 0);
  return true;
}








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
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, int is_dir)
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
      disk_inode->is_dir = is_dir;
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      if (inode_set_file_length(disk_inode, disk_inode->length))
        {
          buffer_cache_write(sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
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
  buffer_cache_read (inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);


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
          inode_deallocate(inode);
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

      /* 5.3.4) bounce buffer을 없애야한다. 대신에 buffer cache로 바로 복사한다(proj5). */
      buffer_cache_read(sector_idx, buffer + bytes_read, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs. */
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

  // beyond the EOF: extend the file
  if( byte_to_sector(inode, offset + size - 1) == -1u ) {
    // extend and reserve up to [offset + size] bytes
    bool success;
    success = inode_set_file_length (& inode->data, offset + size);
    if (!success) return 0;

    // write back the (extended) file size
    inode->data.length = offset + size;
    buffer_cache_write (inode->sector, & inode->data, 0, BLOCK_SECTOR_SIZE);
  }

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
        
      /* 5.3.4) bounce buffer을 없애야한다. 대신에 buffer cache로 바로 복사한다(proj5). */
      buffer_cache_write(sector_idx, buffer + bytes_written, sector_ofs, chunk_size);

      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
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
