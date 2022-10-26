#ifndef FILESYS_FILE_H
#define FILESYS_FILE_H

#include "filesys/off_t.h"
#include "threads/synch.h"

struct inode;

/* E.G) p1에서 file의 정보를 read 으로 수정(pos 등등이 바뀔 것)하는 상황이라 가정하자. 
   context switching 걸려서 p2로 바뀐 후, p2에서 해당 파일을 read 한다고 해보자.
   pos가 바뀌는 와중에 context switching이 걸리는 바람에 pos가 바뀌지 않는다면 에러 발생할 것이다. 
   이를 해결하기 위해 file 구조체의 내용을 수정할 때에는 mutex로 file 구조체의 내용이 올바르게 수정되도록 보호한다. */
struct semaphore mutex_filetable;

/* An open file. */
struct file 
  {
    struct inode *inode;        /* File's inode. */
    off_t pos;                  /* Current position. */
    bool deny_write;            /* Has file_deny_write() been called? */
    
// #ifdef USERPROG
//     int refcnt;                 /* 이 파일 테이블을 가르키는 것의 개수를 의미한다. not use now(prj2) */
// #endif
  };

/* Opening and closing files. */
struct file *file_open (struct inode *);
struct file *file_reopen (struct file *);
void file_close (struct file *);
struct inode *file_get_inode (struct file *);

/* Reading and writing. */
off_t file_read (struct file *, void *, off_t);
off_t file_read_at (struct file *, void *, off_t size, off_t start);
off_t file_write (struct file *, const void *, off_t);
off_t file_write_at (struct file *, const void *, off_t size, off_t start);

/* Preventing writes. */
void file_deny_write (struct file *);
void file_allow_write (struct file *);

/* File position. */
void file_seek (struct file *, off_t);
off_t file_tell (struct file *);
off_t file_length (struct file *);

#endif /* filesys/file.h */
