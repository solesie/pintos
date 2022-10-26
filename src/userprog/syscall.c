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
  if(cur_process->fd[fd]==NULL)
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
  else if(fd >= 3 && fd <=128){
    struct thread* cur_process = thread_current ();
    struct file* f = cur_process->fd[fd];
    if(f == NULL) //이 프로세스에서는 f가 open 된 적이 없다.
      exit(-1);
    i = file_read (f, buffer, size);
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
  else if(fd >= 3 && fd <=128){
    if (cur_process->fd[fd] == NULL) {
      exit(-1);
    }
    ret = file_write(cur_process->fd[fd], buffer, size);
  } else{
    return 0;
  }
  return ret;
}

bool create(const char* file, unsigned initial_size){
  /* filesys_create 가 호출되는 동안 inode_close, inode_read_at, inode_write_at 등 메모리를 조작하는 많은 함수가 호출된다.
     1. filesys_create 되는 동안은 다른 프로세스가 해당 inode_sector 는 건들지 못하게 하던가 (dir, free-map 전부 고려해야함),
     2. 아예 어떠한 함수도 호출 못하게 하던가.
     해야한다. 1번의 방법을 선택한다. */
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
/*
 * lib/user/syscall.c 의 함수가 SYS_WRITE와 같은 신호를 보내면 이 함수가 호출된다.
 */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //printf ("system call! %d\n",*(int*)(f->esp));
  //hex_dump(f->esp,f->esp,100,1);
  switch(*(uint32_t*)(f->esp)){
    case SYS_HALT: 
      halt();
      break;
    case SYS_EXIT:
      if(is_kernel_vaddr(*(uint32_t *)(f->esp + 4))) //vaddr.h
        exit(-1);
      exit(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_EXEC:
      if(is_kernel_vaddr((char*)*(uint32_t *)(f->esp + 4)))
        exit(-1);
      f->eax = exec((char*)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_WAIT:
      f->eax = wait((pid_t)*(uint32_t*)(f->esp + 4));
      break;
    

    case SYS_WRITE:
      //echo x를 하면 echo, x, \n 이렇게 3번의 SYS_WRITE interrupt가 오는듯 하다.
      if(is_kernel_vaddr((int)*(uint32_t*)(f->esp + 4)) || is_kernel_vaddr((const void*)*(uint32_t*)(f->esp + 8))
       || is_kernel_vaddr((unsigned)*((uint32_t*)(f->esp + 12))))
        exit(-1);
      f->eax = write((int)*(uint32_t*)(f->esp + 4), (const void*)*(uint32_t*)(f->esp + 8), (unsigned)*((uint32_t*)(f->esp + 12)));
      break;
    case SYS_READ:
      if(is_kernel_vaddr((int)*(uint32_t*)(f->esp + 4)) || is_kernel_vaddr((int)*(uint32_t*)(f->esp + 8))
       || is_kernel_vaddr((int)*(uint32_t*)(f->esp + 12))){
        exit(-1);
      }
      f->eax = read((int)*(uint32_t*)(f->esp + 4), (void*)*(uint32_t*)(f->esp + 8), (unsigned)*((uint32_t*)(f->esp + 12)));
      break;
    case SYS_OPEN:
      if(is_kernel_vaddr((const char*)*(uint32_t *)(f->esp + 4)))
        exit(-1);
      f->eax = open((const char*)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_CLOSE:
      if(is_kernel_vaddr((int)*(uint32_t *)(f->esp + 4)))
        exit(-1);
      close((int)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_CREATE:
      if(is_kernel_vaddr((const char *)*(uint32_t *)(f->esp + 4)) || is_kernel_vaddr((unsigned)*(uint32_t *)(f->esp + 8)))
        exit(-1);
      f->eax = create((const char *)*(uint32_t *)(f->esp + 4), (unsigned)*(uint32_t *)(f->esp + 8));
      break;
    case SYS_REMOVE:
      if(is_kernel_vaddr((const char*)*(uint32_t *)(f->esp + 4)))
        exit(-1);
      f->eax = remove((const char*)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_FILESIZE:
			if(is_kernel_vaddr((int)*(uint32_t*)(f->esp + 4)))
        exit(-1);
			f->eax = filesize((int)*(uint32_t*)(f->esp+4));
			break;
		case SYS_SEEK:
			if(is_kernel_vaddr((int)*(uint32_t*)(f->esp + 4)) ||is_kernel_vaddr((unsigned)*(uint32_t*)(f->esp + 8)))
        exit(-1);
			seek((int)*(uint32_t*)(f->esp+4), (unsigned)*(uint32_t*)(f->esp+8));
			break;
		case SYS_TELL:
			if(is_kernel_vaddr((int)*(uint32_t*)(f->esp + 4)))
        exit(-1);
			f->eax = tell((int)*(uint32_t*)(f->esp+4));
			break;
    case SYS_FIBO:
      f->eax = fibonacci((int)*(uint32_t*)(f->esp+4));
      break;
    case SYS_MAX4INT:
      f->eax = max_of_four_int((int)*(uint32_t*)(f->esp+4), (int)*(uint32_t*)(f->esp+8),
			  (int)*(uint32_t*)(f->esp+12), (int)*(uint32_t*)(f->esp+16));
      break;
  }

  //thread_exit ();
}
