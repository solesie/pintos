#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/synch.h"
#include "filesys/inode.h"

#include "vm/page.h"
#include "userprog/pagedir.h"
#include "vm/frame.h"
#include "filesys/cache.h"
#include "filesys/directory.h"

static void syscall_handler (struct intr_frame *);

static bool is_valid_user_provided_pointer(void* user_pointer_inclusive, size_t bytes);

void exit (int status){
  printf("%s: exit(%d)\n", thread_name(), status);
  struct thread* t = thread_current();
  t -> exit_status = status;
  for (int i = 3; i < 128; ++i) {
    if (t->fd[i] != NULL) {
      close(i);
    }
  }
  for(int i = 0; i <128; ++i){
    if (t->mmap_d[i] != NULL) {
      munmap(i);
    }
  }
  if(t->cwd) dir_close(t->cwd);
  thread_exit ();
}

void halt(void){
  shutdown_power_off();
}

pid_t exec(const char* cmd_line){
  return process_execute(cmd_line);
}

int wait(pid_t pid){
  return process_wait(pid);
}

int open(const char* file){
  if(file == NULL)
    exit(-1);

  struct file* f = filesys_open(file);

  struct thread* cur_process = thread_current();
  if (f == NULL) {
    return -1;
  }
  for(int  i = 3; i < 128; ++i){
    if(cur_process->fd[i] == NULL){ //pcb의 fd table을 조회한다.
      /* 실행 파일은 실행된 후에 삭제, write 되더라도 메모리에 있으므로 상관 없을수 있지만...
         핀토스는 실행중인 파일의 실행 파일의 삭제, write 를 원하지 않는다.
         현재 프로세스를 연 경우에는(e.g. 'open open.c' is OK, but 'write write.c' NO) 이 파일의 삭제, write를 막아둔다. */
      if(strcmp(cur_process->name, file) == 0){
        file_deny_write(f);
      }
      cur_process->fd[i] = malloc(sizeof(struct file_descriptor));
      cur_process->fd[i]->file = f;
      // directory
      if(f->inode != NULL && f->inode->data.is_dir == 1)
        cur_process->fd[i]->dir = dir_open( inode_reopen(f->inode) );
      else 
        cur_process->fd[i]->dir = NULL;

      return i;
    }
  }
  return -1;
}

void close(int fd){
  struct thread* cur_process = thread_current();
  if(fd < 0 || fd >= 128 || cur_process->fd[fd] == NULL)
		exit(-1);
  
  if(cur_process->fd[fd]->dir != NULL) {
    ASSERT(cur_process->fd[fd]->file->inode->data.is_dir == 1);
    dir_close(cur_process->fd[fd]->dir);
  }
  file_close(cur_process->fd[fd]->file);
  
  free(cur_process->fd[fd]);
	cur_process->fd[fd] = NULL;
}

int read(int fd, void* buffer, unsigned size){
  int i;
  if (fd == 0) {
    for(i = 0; i < size; ++i)
        *(char*)(buffer + i) = input_getc();
  } 
  else if(fd >= 3 && fd < 128){
    struct thread* cur_process = thread_current ();
    if(cur_process->fd[fd]==NULL)//이 프로세스에서는 f가 open 된 적이 없다.
      exit(-1);

    if(cur_process->fd[fd]->dir != NULL)
      return -1;
    
    struct file* f = cur_process->fd[fd]->file;
    //preload_and_pin_pages(buffer,size);
    i = file_read (f, buffer, size);
    //unpin_preloaded_pages(buffer,size);
  }
  else{
    return -1;
  }
  return i;
}

int write (int fd, const void* buffer, unsigned size){
  int ret = -1;
  struct thread* cur_process = thread_current();
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  else if(fd >= 3 && fd < 128){
    if (cur_process->fd[fd] == NULL) {
      exit(-1);
    }

    if(cur_process->fd[fd]->dir != NULL)
      return ret;

    ret = file_write(cur_process->fd[fd]->file, buffer, size);

  } else{
    return ret;
  }
  return ret;
}

bool create(const char* file, unsigned initial_size){
  /* filesys_create 가 호출되는 동안 inode_close, inode_read_at, inode_write_at 등 메모리를 조작하는 많은 함수가 호출된다. */
  if (file == NULL) {
      exit(-1);
  }
  return filesys_create(file, initial_size, 0);
}

bool remove(const char* file){
  if(file == NULL){
    exit(-1);
  }
  return filesys_remove (file);
}

void seek(int fd, unsigned position){
  struct thread* cur_process = thread_current();
	if(cur_process->fd[fd]==NULL)
		exit(-1);
  
  if(cur_process->fd[fd]->dir != NULL)
    exit(-1);

	file_seek(cur_process->fd[fd]->file, position);
}

unsigned tell(int fd){
  struct thread* cur_process = thread_current();
	if(cur_process->fd[fd]==NULL)
		exit(-1);

  if(cur_process->fd[fd]->dir != NULL)
    exit(-1);
  
	return file_tell(cur_process->fd[fd]->file);
}

int filesize(int fd){
  struct thread* t = thread_current();
	if(t->fd[fd]==NULL)
		exit(-1);
  
  if(t->fd[fd]->dir != NULL)
    exit(-1);

	return file_length(t->fd[fd]->file);
}

int fibonacci(int n){
  if(n == 1 || n == 2){
    return 1;
  }
  if(n <= 0){
    return -1;
  }
  return fibonacci(n - 1) + fibonacci(n - 2);
}

int max_of_four_int(int a, int b, int c, int d){
  int max = a;
  if(max < b){
    max = b;
  }
  if(max < c){
    max = c;
  }
  if(max < d){
    max= d;
  }
  return max;
}

/* 4.3.4) file_open으로 연 fd를 user_page부터 연속하는 page로 매핑시킨다.
   성공시, 프로세스마다 따로 가지는 mapping ID를 반환한다.
   실패시, -1을 반환한다. 이때 아무런 변화가 없어야한다. */
mmpid_t mmap(int fd, void* user_page){
  //fd가 0,1인경우 실패한다.
  if(fd <= 1 || fd >= 128) return -1;

  //user_page = 0x0인 경우 실패한다.
  if(user_page == NULL) return -1;

  //user_page가 page-aligned가 아니라면 실패한다.
  if(pg_ofs(user_page) != 0) return -1;

  struct thread* cur = thread_current();
  if(cur->fd[fd] == NULL) return -1;
  if(cur->fd[fd]->dir != NULL) return -1;

  struct file* f = cur->fd[fd]->file;
  off_t file_bytes;
  if(f == NULL) return -1;

  //fd를 열었을 때 파일 길이가 0이면 실패한다.
  file_bytes = file_length(f);
  if(file_bytes == 0) return -1;

  for (off_t i = 0; i < file_bytes; i += PGSIZE) {
    void *user_page_aligned = user_page + i;
    //이미 존재하는 매핑된 페이지들을 덮어씌우려고 할경우 실패한다.
    if(is_kernel_vaddr(user_page_aligned)) return -1;
    if(vm_spt_lookup(&cur->spt, user_page_aligned) != NULL) return -1;
  }

  mmpid_t ret;
  for(ret = 0; ret < 128; ++ret)
    if(cur->mmap_d[ret] == NULL)
      break;
  if(ret == 128) return -1;

  /* file_close(fd), file_remove(fd_name)은 이 mmap에 아무런 영향을 주지 않아야한다.
     한번 mmap되면, munmap이나 exit될때 까지는 항상 유효해야한다(Unix convention).
     
     그리고 서로다른 user program에서 똑같은 파일을 mmap시, sharing은 PintOs에선 요구되지 않는다.
     (동일한 file table을 보지 않아도 된다는 뜻인듯 하다)
     
     같은 inode를 참조하는 file table 두개가 생성되는 상황이다. */
  f = file_reopen(f);
  if(f == NULL) return -1;

  cur->mmap_d[ret] = malloc(sizeof(struct mmap_descriptor));
  cur->mmap_d[ret]->file = f;
  cur->mmap_d[ret]->starting_page = user_page;

  /* spt에 데이터를 적는다. */
  for (off_t i = 0; i < file_bytes; i += PGSIZE) {
    void *user_page_aligned = user_page + i;

    size_t read_bytes = (i + PGSIZE < file_bytes ? PGSIZE : file_bytes - i);
    size_t zero_bytes = PGSIZE - read_bytes;//final mapped page sticked out beyond the EOF.(나중에 기록할때 버린다)

    vm_spt_install_IN_FILE_page(&cur->spt, user_page_aligned, f, i, read_bytes, zero_bytes, true);
  }
  
  return ret;
}

void munmap(mmpid_t mapping){
  struct thread* cur = thread_current();
  
  if(mapping < 0 || mapping >=128 || cur->mmap_d[mapping] == NULL)
    exit(-1);

  off_t file_bytes = file_length(cur->mmap_d[mapping]->file);
  for(off_t i = 0; i < file_bytes; i += PGSIZE){
    void* user_page_aligned = cur->mmap_d[mapping]->starting_page + i;
    struct supplemental_page_table_entry* spte = vm_spt_lookup(&cur->spt, user_page_aligned);
    make_user_pointer_in_physical_memory(user_page_aligned, PGSIZE);
    vm_save_IN_FRAME_to_file(cur, spte);
  }

  file_close(cur->mmap_d[mapping]->file);
  free(cur->mmap_d[mapping]);
  cur->mmap_d[mapping] = NULL;
}



bool chdir(const char *filename)
{
  return filesys_chdir(filename);// not synch yet
}

bool mkdir(const char *filename)
{
  return filesys_create(filename, 0, 1);
}

bool readdir(int fd, char *name)
{
  struct thread* t = thread_current();

  if(t->fd[fd] == NULL) return false;
  struct file* f = t->fd[fd]->file;
  if(f == NULL) return false;

  if(f->inode->data.is_dir == 0) {
    ASSERT(t->fd[fd]->dir == NULL);
    return false;
  }

  // ASSERT (file_d->dir != NULL); // see sys_open()
  return dir_readdir (t->fd[fd]->dir, name);
}

bool isdir(int fd)
{
  struct thread* t = thread_current();
  if(t->fd[fd] == NULL) return false;

  ASSERT((t->fd[fd]->dir != NULL) == (t->fd[fd]->file->inode->data.is_dir == 1));

  return t->fd[fd]->dir != NULL;
}

int inumber(int fd)
{
  struct thread* t = thread_current();
  if(t->fd[fd] == NULL) return false;
  return (int)inode_get_inumber (t->fd[fd]->file->inode);
}






void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}


static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* user mode -> kernel mode 전환 시 CPU는 user_pool에서의 
     레지스터 정보들을 intr_frame f에 저장한다(page_fault()주석 참조).
     user mode에서 %esp의 정보를 지멋대로 바꿨을 수도 있다.
     switch문에서 f->esp를 uint32_t*로 번역한다음 참조하므로 4bytes를 검사한다 */
  if(!is_valid_user_provided_pointer(f->esp, 4)){
    exit(-1);
  }

  switch(*(uint32_t*)(f->esp)){

    case SYS_HALT: {
      halt();
      break;
    }


    case SYS_EXIT:{
      //Before interpret f->esp + 4 as a pointer for int and read, check validation.
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
      
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));

      exit(*(int *)(f->esp + 4));

      unmake(f->esp + 4, sizeof(int));
      break;
    }


    case SYS_EXEC:{
      //f->esp + 4에 저장되어있는 주소를 참조하여 cmd_line이 저장된 위치로 가야한다. 
      //f->esp + 4를 참조하기 전에, 주소가 유효한지 확인한다(주소는 uint32_t와 호환된다).
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(uint32_t)))
          exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(uint32_t));
      
      //ok. 이제 f->esp + 4를 참조해서 cmd_line의 주소를 얻어도 된다.
      //cmd_line은 모두 유효한 user pointer여야 한다.
      char* start = (char*)*(uint32_t *)(f->esp + 4);
      struct thread* t = thread_current();
      int i;
      for(i = 0; ;++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);
        if(i % PGSIZE == 0) //TIMEOUT...
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }

      f->eax = exec((char*)*(uint32_t *)(f->esp + 4));

      unmake(f->esp + 4, sizeof(uint32_t));
      unmake(start, i);
      
      break;
    }

    case SYS_WAIT:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(pid_t)))
        exit(-1);

      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(pid_t));

      f->eax = wait(*(pid_t*)(f->esp + 4));

      unmake(f->esp + 4, sizeof(pid_t));

      break;
    }

    case SYS_WRITE:{
      //f->esp + x를 참조해도 되는지 확인
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)) 
       || !is_valid_user_provided_pointer(f->esp + 8, sizeof(uint32_t))
       || !is_valid_user_provided_pointer(f->esp + 12, sizeof(unsigned)))
        exit(-1);
      
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));
      make_user_pointer_in_physical_memory(f->esp + 8, sizeof(uint32_t));
      make_user_pointer_in_physical_memory(f->esp + 12, sizeof(unsigned));

      const uint8_t* start = (const uint8_t*)*(uint32_t *)(f->esp + 8);
      int i;
      for(i = 0; i < *(unsigned*)(f->esp + 12); ++i){
        if(!is_valid_user_provided_pointer(start + i, 1)) //쓸려는 buf 공간 또한 유효해야한다.
          exit(-1);
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
      }
      make_user_pointer_in_physical_memory(start +i, 1);

      f->eax = write(*(int*)(f->esp + 4), (const void*)*(uint32_t*)(f->esp + 8),
                       *(unsigned*)(f->esp + 12));
      
      unmake(f->esp + 4, sizeof(int));
      unmake(f->esp + 8, sizeof(uint32_t));
      unmake(f->esp + 12, sizeof(unsigned));
      unmake(start, i);

      break;
    }


    case SYS_READ:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)) 
       || !is_valid_user_provided_pointer(f->esp + 8, sizeof(uint32_t))
       || !is_valid_user_provided_pointer(f->esp + 12, sizeof(unsigned)))
        exit(-1);

      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int)) ;
      make_user_pointer_in_physical_memory(f->esp + 8, sizeof(uint32_t));
      make_user_pointer_in_physical_memory(f->esp + 12, sizeof(unsigned));

      //buffer가 유효한 공간인지도 확인한다.
      struct thread* t = thread_current();
      uint8_t* start = (uint8_t*)*(uint32_t *)(f->esp + 8);
      int i;
      for(i = 0; i < *(unsigned*)(f->esp + 12); ++i){
        if(!is_valid_user_provided_pointer(start + i, 1))
          exit(-1);
        //buffer가 writable한지도 확인해야한다(pt-write-code-2 test 참조).
        if(!vm_spt_lookup(&t->spt, pg_round_down(start + i))->writable)
          exit(-1);
        
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
      }
      make_user_pointer_in_physical_memory(start + i, 1);

      f->eax = read(*(int*)(f->esp + 4), (void*)*(uint32_t*)(f->esp + 8), *(unsigned*)(f->esp + 12));

      unmake(f->esp + 4,sizeof(int));
      unmake(f->esp + 8, sizeof(uint32_t));
      unmake(f->esp + 12, sizeof(unsigned));
      unmake(start, i);

      break;
    }

    case SYS_OPEN:{
      //same as SYS_EXEC
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(uint32_t*)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(uint32_t*));

      const char* start = (const char*)*(uint32_t *)(f->esp + 4);
      int i;
      for(i = 0; ; ++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);
          
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }
      f->eax = open((const char*)*(uint32_t *)(f->esp + 4));

      unmake(f->esp + 4, sizeof(uint32_t*));
      unmake(start, i);

      break;
    }

    case SYS_CLOSE:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));

      close(*(int *)(f->esp + 4));
      
      unmake(f->esp + 4, sizeof(int));
      break;
    }

    case SYS_CREATE:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(uint32_t)) 
       || !is_valid_user_provided_pointer(f->esp + 8, sizeof(unsigned)))
        exit(-1);

      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(uint32_t));
      make_user_pointer_in_physical_memory(f->esp + 8, sizeof(unsigned));
      
      const char* start = (const char*)*(uint32_t *)(f->esp + 4);
      int i;
      for(i = 0; ; ++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);
        
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }
      f->eax = create((const char *)*(uint32_t *)(f->esp + 4), *(unsigned *)(f->esp + 8));

      unmake(f->esp + 4, sizeof(uint32_t));
      unmake(f->esp + 8, sizeof(uint32_t));
      unmake(start, i);
      
      break;
    }

    case SYS_REMOVE:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(uint32_t*)))
        exit(-1);

      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(uint32_t*));

      const char* start = (const char*)*(uint32_t *)(f->esp + 4);
      int i;
      for(i = 0; ; ++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);

        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }
      f->eax = remove((const char*)*(uint32_t *)(f->esp + 4));

      unmake(f->esp + 4, sizeof(uint32_t*));
      unmake(start, i);

      break;
    }

    case SYS_FILESIZE:{
			if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
			f->eax = filesize(*(int*)(f->esp+4));
			break;
    }

		case SYS_SEEK:{
			if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int))
       ||!is_valid_user_provided_pointer(f->esp + 8, sizeof(unsigned)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));
      make_user_pointer_in_physical_memory(f->esp + 8, sizeof(unsigned));
			seek(*(int*)(f->esp+4), *(unsigned*)(f->esp+8));
      unmake(f->esp + 4, sizeof(int));
      unmake(f->esp + 8, sizeof(unsigned));
			break;
    }

		case SYS_TELL:{
			if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
			f->eax = tell(*(int*)(f->esp+4));
			break;
    }

    case SYS_FIBO:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
      f->eax = fibonacci(*(int*)(f->esp+4));
      break;
    }

    case SYS_MAX4INT:{
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)) 
       || !is_valid_user_provided_pointer(f->esp + 8, sizeof(int))
       || !is_valid_user_provided_pointer(f->esp + 12, sizeof(int))
       || !is_valid_user_provided_pointer(f->esp + 16, sizeof(int)))
        exit(-1);
      f->eax = max_of_four_int(*(int*)(f->esp+4), *(int*)(f->esp+8),
			  *(int*)(f->esp+12), *(int*)(f->esp+16));
      break;
    }

    case SYS_MMAP: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)) 
       || !is_valid_user_provided_pointer(f->esp + 8, sizeof(void*)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));
      make_user_pointer_in_physical_memory(f->esp + 8, sizeof(void*));

      f->eax = mmap(*(int*)(f->esp + 4), (void*)*(uint32_t*)(f->esp + 8));

      unmake(f->esp + 4, sizeof(int));
      unmake(f->esp + 8, sizeof(void*));
      break;
    }

    case SYS_MUNMAP: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));

      munmap(*(int*)(f->esp + 4));

      unmake(f->esp + 4, sizeof(int));
      break;
    }

    case SYS_CHDIR: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(uint32_t*)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(uint32_t*));

      const char* start = (const char*)*(uint32_t *)(f->esp + 4);
      int i;
      for(i = 0; ; ++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);
          
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }

      f->eax = chdir((const char*)*(uint32_t *)(f->esp + 4));

      unmake(f->esp + 4, sizeof(uint32_t*));
      unmake(start, i);

      break;
    }

    case SYS_MKDIR: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(uint32_t*)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(uint32_t*));

      const char* start = (const char*)*(uint32_t *)(f->esp + 4);
      int i;
      for(i = 0; ; ++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);
          
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }

      f->eax = mkdir((const char*)*(uint32_t *)(f->esp + 4));

      unmake(f->esp + 4, sizeof(uint32_t*));
      unmake(start, i);

      break;
    }

    case SYS_READDIR: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)) 
       || !is_valid_user_provided_pointer(f->esp + 8, sizeof(uint32_t)))
        exit(-1);

      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));
      make_user_pointer_in_physical_memory(f->esp + 8, sizeof(uint32_t));
      
      char* start = (char*)*(uint32_t *)(f->esp + 8);
      int i;
      for(i = 0; ; ++i){
        if(!is_valid_user_provided_pointer(start+i, 1))
          exit(-1);
        
        if(i % PGSIZE == 0)
          make_user_pointer_in_physical_memory(start + i, 1);
        if(start[i] == NULL){
          make_user_pointer_in_physical_memory(start + i, 1);
          break;
        }
      }

      f->eax = readdir(*(int *)(f->esp + 4),(char *)*(uint32_t *)(f->esp + 8));

      unmake(f->esp + 4, sizeof(int));
      unmake(f->esp + 8, sizeof(uint32_t));
      unmake(start, i);
      
      break;
    }

    case SYS_ISDIR: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));

      f->eax = isdir(*(int*)(f->esp + 4));

      unmake(f->esp + 4, sizeof(int));
      break;
    }

    case SYS_INUMBER: {
      if(!is_valid_user_provided_pointer(f->esp + 4, sizeof(int)))
        exit(-1);
      make_user_pointer_in_physical_memory(f->esp + 4, sizeof(int));

      f->eax = inumber(*(int*)(f->esp + 4));

      unmake(f->esp + 4, sizeof(int));
      break;
    }

  }

}

/* user_pointer_inclusive부터 bytes만큼 유효한지 확인한다.
   proj4) exception.c:page_fault()를 위해 추가한다.
   page_fault() 주석 참조. */
static bool is_valid_user_provided_pointer(void* user_pointer_inclusive, size_t bytes){
  struct thread* t = thread_current();
  for(size_t i = 0; i < bytes; ++i){
    /* null pointer check, kernel space(above PHYS_BASE) check. */
    if(user_pointer_inclusive + i == NULL || is_kernel_vaddr(user_pointer_inclusive + i))
      return false;

    /* unmapped virtual memory check. */
    void* user_page = pg_round_down(user_pointer_inclusive + i);
#ifdef VM
    if(vm_spt_lookup(&(t->spt), user_page) == NULL)
      return false;
#else
    if(pagedir_get_page (t->pagedir, user_page) == NULL)
      return false;
#endif
  }
  return true;
}