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

/* 특정 inode에 대해 접근을 막는 lock.(very very strongly)
   동일한 inode는 sync 하게 open 하는데 필요하다. */
static struct lock** inode_lock;

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    block_sector_t start;               /* First data sector. */
    off_t length;                       /* File size in bytes. */
    unsigned magic;                     /* Magic number. */
    uint32_t unused[125];               /* Not used. */
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
    //int read_cnt;                       /* 읽는 프로세스의 숫자. File Sharing 의 경우를 고려하여 inode에 정의한다. */
    //struct lock w;
    //struct semaphore mutex_inode;
#endif
    int read_cnt;  
    struct lock w;
    struct lock inode_readcnt_mutex;
  };

void inode_init (void);
bool inode_create (block_sector_t, off_t, block_sector_t*);
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
