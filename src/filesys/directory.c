#include "filesys/directory.h"
#include <stdio.h>
#include <string.h>
#include <list.h>
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

#include "threads/thread.h"

/* A directory. */
struct dir 
  {
    struct inode *inode;                /* Backing store. */
    off_t pos;                          /* Current position. */
  };

/* A single directory entry. */
struct dir_entry 
  {
    block_sector_t inode_sector;        /* Sector number of header. */
    char name[NAME_MAX + 1];            /* Null terminated file name. */
    bool in_use;                        /* In use or free? */
  };




/* path를 통해 directory와 filename을 초기화한다. */
void split_path(const char *path, char *directory, char *filename){
  int l = strlen(path);
  char *full_dir = (char*) malloc( sizeof(char) * (l + 1) );
  memcpy (full_dir, path, sizeof(char) * (l + 1));

  // absolute path handling
  char *dir = directory;
  if(l > 0 && path[0] == '/') {
    if(dir) *dir++ = '/';
  }

  // tokenize
  char *token, *save_ptr, *last_token = "";
  for (token = strtok_r(full_dir, "/", &save_ptr); token != NULL; token = strtok_r(NULL, "/", &save_ptr)){
    int tl = strlen (last_token);
    if (dir && tl > 0) {
      memcpy (dir, last_token, sizeof(char) * tl);
      dir[tl] = '/';
      dir += tl + 1;
    }

    last_token = token;
  }

  if(dir) *dir = '\0';
  memcpy (filename, last_token, sizeof(char) * (strlen(last_token) + 1));
  free (full_dir);
}


/* Opens the directory for given path. */
struct dir * dir_open_path (const char *path){
  // copy of path, to tokenize
  int l = strlen(path);
  char path_temp[l + 1];
  strlcpy(path_temp, path, l + 1);

  // relative path
  struct dir *cur_dir;
  if(path[0] == '/') { // absolute path
    cur_dir = dir_open_root();
  }
  else { // relative path
    struct thread *t = thread_current();
    if (t->cwd == NULL) // idle
      cur_dir = dir_open_root();
    else {
      cur_dir = dir_reopen( t->cwd );
    }
  }

  char *token, *save_pointer;
  for (token = strtok_r(path_temp, "/", &save_pointer); token != NULL; token = strtok_r(NULL, "/", &save_pointer)){
    struct inode* dir_inode = NULL;
    if(! dir_lookup(cur_dir, token, &dir_inode)) {
      dir_close(cur_dir);
      return NULL; // such directory not exist
    }

    struct dir *next = dir_open(dir_inode);
    if(next == NULL) {
      dir_close(cur_dir);
      return NULL;
    }
    dir_close(cur_dir);
    cur_dir = next;
  }

  // prevent opening removed directories
  if (cur_dir->inode->removed) {
    dir_close(cur_dir);
    return NULL;
  }

  return cur_dir;
}







/* Creates a directory with space for ENTRY_CNT entries in the
   given SECTOR.  Returns true if successful, false on failure. */
bool
dir_create (block_sector_t sector, size_t entry_cnt)
{
  bool success = true;
  success = inode_create (sector, entry_cnt * sizeof (struct dir_entry), 1);
  if(!success) return false;

  // The first (offset 0) dir entry is for parent directory; do self-referencing
  // Actual parent directory will be set on execution of dir_add()
  struct dir *dir = dir_open( inode_open(sector) );
  ASSERT (dir != NULL);
  struct dir_entry e;
  e.inode_sector = sector;
  if (inode_write_at(dir->inode, &e, sizeof e, 0) != sizeof e) {
    success = false;
  }
  dir_close (dir);

  return success;
}

/* Opens and returns the directory for the given INODE, of which
   it takes ownership.  Returns a null pointer on failure. */
struct dir *
dir_open (struct inode *inode) 
{
  struct dir *dir = calloc (1, sizeof *dir);
  if (inode != NULL && dir != NULL)
    {
      dir->inode = inode;
      dir->pos = sizeof (struct dir_entry); // 0-pos is for parent directory
      return dir;
    }
  else
    {
      inode_close (inode);
      free (dir);
      return NULL; 
    }
}

/* Opens the root directory and returns a directory for it.
   Return true if successful, false on failure. */
struct dir *
dir_open_root (void)
{
  return dir_open (inode_open (ROOT_DIR_SECTOR));
}

/* Opens and returns a new directory for the same inode as DIR.
   Returns a null pointer on failure. */
struct dir *
dir_reopen (struct dir *dir) 
{
  return dir_open (inode_reopen (dir->inode));
}

/* Destroys DIR and frees associated resources. */
void
dir_close (struct dir *dir) 
{
  if (dir != NULL)
    {
      bool ret = inode_close (dir->inode);
#ifdef USERPROG
      if(ret == true) //inode가 free 되었다면
#endif
        free (dir); 
    }
}

/* Returns the inode encapsulated by DIR. */
struct inode *
dir_get_inode (struct dir *dir) 
{
  return dir->inode;
}

/* Searches DIR for a file with the given NAME.
   If successful, returns true, sets *EP to the directory entry
   if EP is non-null, and sets *OFSP to the byte offset of the
   directory entry if OFSP is non-null.
   dir->inode를 reading 하는 상황이다.
   otherwise, returns false and ignores EP and OFSP. */
static bool
lookup (const struct dir *dir, const char *name,
        struct dir_entry *ep, off_t *ofsp) 
{
  struct dir_entry e;
  size_t ofs;
  
  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  for (ofs = sizeof e; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
  {
    if (e.in_use && !strcmp (name, e.name)) 
      {
        if (ep != NULL)
          *ep = e;
        if (ofsp != NULL)
          *ofsp = ofs;
        return true;
      }
  }

  return false;
}

/* Searches DIR for a file with the given NAME
   and returns true if one exists, false otherwise.
   On success, sets *INODE to an inode for the file, otherwise to
   a null pointer.  The caller must close *INODE. */
bool
dir_lookup (const struct dir *dir, const char *name,
            struct inode **inode) 
{
  struct dir_entry e;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

#ifdef USERPROG
  lock_acquire(&(dir->inode->inode_readcnt_mutex));
  ++dir->inode->read_cnt;
  if(dir->inode->read_cnt == 1)
    sema_down(&(dir->inode->w));
  lock_release(&(dir->inode->inode_readcnt_mutex));
#endif

  if (strcmp (name, ".") == 0) {
    // current directory
    *inode = inode_reopen (dir->inode);
  }
  else if (strcmp (name, "..") == 0) {
    // 부모 정보는 0번 dir entry에 존재한다.
    inode_read_at (dir->inode, &e, sizeof e, 0);
    *inode = inode_open (e.inode_sector);
  }
  else if(lookup (dir, name, &e, NULL))
    *inode = inode_open (e.inode_sector);
  else
    *inode = NULL;

#ifdef USERPROG
  lock_acquire(&(dir->inode->inode_readcnt_mutex));
  --dir->inode->read_cnt;
  if(dir->inode->read_cnt == 0)
    sema_up(&(dir->inode->w));
  lock_release(&(dir->inode->inode_readcnt_mutex));
#endif

  return *inode != NULL;
}

/* Adds a file named NAME to DIR, which must not already contain a
   file by that name.  The file's inode is in sector
   INODE_SECTOR.
   Returns true if successful, false on failure.
   Fails if NAME is invalid (i.e. too long) or a disk or memory
   error occurs.
   dir->inode 에 writing하는 상황이다. */
bool
dir_add (struct dir *dir, const char *name, block_sector_t inode_sector, int is_dir)
{
  struct dir_entry e;
  off_t ofs;
  bool success = false;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

  /* Check NAME for validity. */
  if (*name == '\0' || strlen (name) > NAME_MAX){
    return false;
  }

#ifdef USERPROG
  sema_down(&(dir->inode->w));
#endif

  /* Check that NAME is not in use. */
  if (lookup (dir, name, NULL, NULL)){
    goto done;
  }

  /* directory일 경우에 child directory inode는 [inode_sector]에 존재한다.
     child directory inode의 0번째 dir entry에 부모 디렉토리를 이 디렉토리로 갱신한다. */
  if (is_dir){
    /* e is a parent-directory-entry here */
    struct dir *child_dir = dir_open( inode_open(inode_sector) );
    if(child_dir == NULL) {
      goto done;
    }

    e.inode_sector = dir->inode->sector;
    
    /* child directory inode에 write 하는 상황이다. */
    sema_down(&child_dir->inode->w);
    if (inode_write_at(child_dir->inode, &e, sizeof e, 0) != sizeof e) {
      sema_up(&child_dir->inode->w);
      dir_close (child_dir);
      goto done;
    }
    sema_up(&child_dir->inode->w);
    dir_close (child_dir);
  }

  /* Set OFS to offset of free slot.
     If there are no free slots, then it will be set to the
     current end-of-file.
     
     inode_read_at() will only return a short read at end of file.
     Otherwise, we'd need to verify that we didn't get a short
     read due to something intermittent such as low memory. */
  for (ofs = 0; inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e) 
    if (!e.in_use)
      break;

  /* Write slot. */
  e.in_use = true;
  strlcpy (e.name, name, sizeof e.name);
  e.inode_sector = inode_sector;
  success = inode_write_at (dir->inode, &e, sizeof e, ofs) == sizeof e;

 done:

#ifdef USERPROG
  sema_up(&(dir->inode->w));
#endif
  return success;
}

static bool
dir_is_empty (struct dir *dir)
{
  struct dir_entry e;
  off_t ofs;

  for (ofs = sizeof e; /* 0-pos is for parent directory */
       inode_read_at (dir->inode, &e, sizeof e, ofs) == sizeof e;
       ofs += sizeof e)
  {
    if (e.in_use)
      return false;
  }
  return true;
}

/* Removes any entry for NAME in DIR.
   Returns true if successful, false on failure,
   which occurs only if there is no file with the given NAME.
   dir->inode 에 writing하는 상황이다. */
bool
dir_remove (struct dir *dir, const char *name) 
{
  struct dir_entry e;
  struct inode *inode = NULL;
  bool success = false;
  off_t ofs;

  ASSERT (dir != NULL);
  ASSERT (name != NULL);

#ifdef USERPROG
  sema_down(&(dir->inode->w));
#endif

  /* Find directory entry. */
  if (!lookup (dir, name, &e, &ofs))
    goto done;

  /* Open inode. */
  inode = inode_open (e.inode_sector);
  if (inode == NULL)
    goto done;

  /* Prevent removing non-empty directory. */
  if (inode->data.is_dir == 1) {
    // target : the directory to be removed. (dir : the base directory)
    struct dir *target = dir_open (inode);
    bool is_empty = dir_is_empty(target);
    free(target);
    if (!is_empty) goto done;
  }

  /* Erase directory entry. */
  e.in_use = false;
  if (inode_write_at (dir->inode, &e, sizeof e, ofs) != sizeof e) 
    goto done;

  /* Remove inode. */
  inode_remove (inode);
  success = true;

 done:
#ifdef USERPROG
  sema_up(&(dir->inode->w));
#endif
  inode_close (inode);
  return success;
}

/* Reads the next directory entry in DIR and stores the name in
   NAME.  Returns true if successful, false if the directory
   contains no more entries. */
bool
dir_readdir (struct dir *dir, char name[NAME_MAX + 1])
{
  struct dir_entry e;

  while (inode_read_at (dir->inode, &e, sizeof e, dir->pos) == sizeof e) 
    {
      dir->pos += sizeof e;
      if (e.in_use)
        {
          strlcpy (name, e.name, NAME_MAX + 1);
          return true;
        } 
    }
  return false;
}
