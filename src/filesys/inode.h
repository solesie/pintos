#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include <list.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include "threads/synch.h"

#include "devices/block.h"
#include <round.h>

struct bitmap;

#define NUM_DIRECT_BLOCKS 123
#define NUM_POINTER_BLOCKS 128
#define MAX_FILE_LENGTH 8388618
/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long.

   기존의 start 부터 length(size)만큼 저장되어있다고 알리는 continuous 구조에서
   indexed inode 구조로 바꾼다. 
   
   is_dir, length, magic: 12 bytes
   direct_blocks: 123 * 4 bytes
   indirect_block: 4 bytes
   doubly_indirect_block: 4bytes
   ==> 512 bytes = BLOCK_SECTOR_SIZE bytes long

   이 파일 시스템이 나타낼 수 있는 파일의 최대 크기(5.3.2에 따르면 8MB여야 한다)
   : 123*512 bytes + 128*512 bytes + 128*128*512 bytes = 8388618 bytes = 8MB */
struct inode_disk
  {
    block_sector_t direct_blocks[NUM_DIRECT_BLOCKS];
    block_sector_t indirect_block;
    block_sector_t doubly_indirect_block;

    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    int is_dir;                         /* is dirctory inode(1 : true, 0: false) */
  };

/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  
#ifdef USERPROG                      
    int read_cnt;
    struct semaphore w;
    struct lock inode_readcnt_mutex;
#endif
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, int);
struct inode *inode_open (block_sector_t);
struct inode *inode_reopen (struct inode *);
block_sector_t inode_get_inumber (const struct inode *);
bool inode_close (struct inode *);
void inode_remove (struct inode *);
off_t inode_read_at (struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at (struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write (struct inode *);
void inode_allow_write (struct inode *);
off_t inode_length (const struct inode *);

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long.(size bytes 길이에 해당하는 블록 개수 변환해서 반환) */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

#endif /* filesys/inode.h */
