#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "lib/user/syscall.h"
#include "threads/vaddr.h"

static void syscall_handler (struct intr_frame *);

void exit (int status){
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_current() -> exit_status = status;
  thread_exit ();
}

int write (int fd, const void* buffer, unsigned size){
  if (fd == 1) {
    putbuf(buffer, size);
    return size;
  }
  return -1; 
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

int read(int fd, void* buffer, unsigned size){
  int i;
  if (fd == 0) {
    for (i = 0; i < size; i ++) {
      if (((char *)buffer)[i] == '\0') {
        break;
      }
    }
  }
  return i;
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
      if(is_kernel_vaddr(f->esp + 4)) //vaddr.h
        exit(-1);
      exit(*(uint32_t *)(f->esp + 4));
      break;
    case SYS_EXEC:
      if(is_kernel_vaddr(f->esp + 4))
        exit(-1);
      f->eax = exec((char*)*(uint32_t *)(f->esp + 4));
      break;
    case SYS_WAIT:
      f->eax = wait((pid_t)*(uint32_t*)(f->esp + 4));
      break;
    case SYS_WRITE:
      //echo x를 하면 echo, x, \n 이렇게 3번의 SYS_WRITE interrupt가 오는듯 하다.
      //fd는 esp + 20, 출력할 문자의 주소는 esp + 24, 출력할 문자의 크기는 esp + 28에 위치.(esp = 0xbfffeb0)
      write((int)*(uint32_t*)(f->esp + 20), (void*)*(uint32_t*)(f->esp + 24), (unsigned)*((uint32_t*)(f->esp + 28)));
      break;
    case SYS_READ:
      if(is_kernel_vaddr(f->esp + 20) || is_kernel_vaddr(f->esp + 24) || is_kernel_vaddr(f->esp + 28)){
        exit(-1);
      }
      f->eax = read((int)*(uint32_t*)(f->esp + 20), (void*)*(uint32_t*)(f->esp + 24), (unsigned)*((uint32_t*)(f->esp + 28)));
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
