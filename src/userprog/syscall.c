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

static void syscall_handler (struct intr_frame *);

void exit (int status){
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_current() -> exit_status = status;
  for (int i = 3; i < 128; i++) {
      if (thread_current()->fd[i] != NULL) {
          close(i);
      }   
  }
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
      cur_process->fd[i] = f;
      return i;
    }
  }
  return -1;
}

void close(int fd){
  struct thread* cur_process = thread_current();
  if(fd < 0 || fd >= 128 || cur_process->fd[fd]==NULL)
		exit(-1);
  
	file_close(cur_process->fd[fd]);
	cur_process->fd[fd]=NULL;
}

int read(int fd, void* buffer, unsigned size){
  int i;
  if (fd == 0) {
    for(i = 0; i < size; ++i)
        *(char*)(buffer + i) = input_getc();
  } 
  else if(fd >= 3 && fd < 128){
    struct thread* cur_process = thread_current ();
    struct file* f = cur_process->fd[fd];
    if(f == NULL) //이 프로세스에서는 f가 open 된 적이 없다.
      exit(-1);
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
    //preload_and_pin_pages(buffer,size);
    ret = file_write(cur_process->fd[fd], buffer, size);
    //unpin_preloaded_pages(buffer,size);
  } else{
    return 0;
  }
  return ret;
}

bool create(const char* file, unsigned initial_size){
  /* filesys_create 가 호출되는 동안 inode_close, inode_read_at, inode_write_at 등 메모리를 조작하는 많은 함수가 호출된다. */
  if (file == NULL) {
      exit(-1);
  }
  return filesys_create(file, initial_size);
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
	file_seek(cur_process->fd[fd], position);
}

unsigned tell(int fd){
  struct thread* cur_process = thread_current();
	if(cur_process->fd[fd]==NULL)
		exit(-1);
	return file_tell(cur_process->fd[fd]);
}

int filesize(int fd){
	if(thread_current()->fd[fd]==NULL)
		exit(-1);
	return file_length(thread_current()->fd[fd]);
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



void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}










static bool is_valid_user_provided_pointer(void* user_pointer_inclusive, size_t bytes);
static void make_user_pointer_in_physical_memory(void* user_pointer_inclusive, size_t bytes);
static void unmake(void* user_pointer_inclusive, size_t bytes);

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

/* 4.3.5) user_pointer_inclusive부터 bytes만큼 physical memory에 고정시킨다.
   BLOCK_FILESYS, BLOCK_SWAP 와 같은 block device를 컨트롤하는 block device driver가 있다.
   BLOCK_FILESYS에서 block_read(), block_write()를 호출하는 도중에 page_fault()가 발생하여
   BLOCK_SWAP에서 block_read(), block_write()를 호출하는 상황이 발생하면 안된다.
   
   user_pointer_inclusive 부터 bytes까지 swap device에 존재하는 페이지는 physical memory로 데려온다. */
static void make_user_pointer_in_physical_memory(void* user_pointer_inclusive, size_t bytes){
  struct thread* t = thread_current();
  
  void* new_page = NULL;
  for(size_t i = 0; i < bytes; ++i){
      //페이지가 달라진 경우
      if(new_page != pg_round_down(user_pointer_inclusive + i)){
        //advanced
        new_page = pg_round_down(user_pointer_inclusive + i);

        //이번 user_pointer_inclusive + i가 속하는 페이지의 spte를 구한다.
        struct supplemental_page_table_entry* spte = vm_spt_lookup(&t->spt, new_page);
        if(spte->frame_data_clue == SWAP)
          vm_load_spte_to_user_pool (spte);

        //이번 user_pointer_inclusive +i가 나타내는 frame을 구하고 user pointer를 위해 쓰인다고 기록한다.
        struct vm_ft_same_keys* founds = vm_frame_lookup(spte->kernel_virtual_page_in_user_pool); 
        vm_frame_set_for_user_pointer(founds, true);
        vm_ft_same_keys_free(founds);
      }
  }
}
static void unmake(void* user_pointer_inclusive, size_t bytes){
  struct thread* t = thread_current();

  void* new_page = NULL;
  for(size_t i = 0; i < bytes; ++i){
      //페이지가 달라진 경우
      if(new_page != pg_round_down(user_pointer_inclusive + i)){
        //advanced
        new_page = pg_round_down(user_pointer_inclusive + i);

        //이번 user_pointer_inclusive + i가 속하는 페이지의 spte를 구한다.
        struct supplemental_page_table_entry* spte = vm_spt_lookup(&t->spt, new_page);

        //이번 user_pointer_inclusive +i가 나타내는 frame을 구하고 user pointer를 위해 쓰인다고 기록한다.
        struct vm_ft_same_keys* founds = vm_frame_lookup(spte->kernel_virtual_page_in_user_pool); 
        vm_frame_set_for_user_pointer(founds, false);
        vm_ft_same_keys_free(founds);
      }
  }
}