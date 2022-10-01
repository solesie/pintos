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

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  //printf ("system call! %d\n",*(int*)(f->esp));
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
      wait((pid_t)*(uint32_t*)(f->esp + 4));
      break;
    case SYS_WRITE:
      //echo x를 하면 echo, x, \n 이렇게 3번의 SYS_WRITE interrupt가 오는듯 하다.
      //fd는 esp + 4, 출력할 문자의 주소는 esp + 8, 출력할 문자의 크기는 esp + 12에 위치.(esp = 0xbfffeb0)
      write((int)*(uint32_t*)(f->esp + 20), (void*)*(uint32_t*)(f->esp + 24), (unsigned)*((uint32_t*)(f->esp + 28)));
      break;
    case SYS_READ:
      if(is_kernel_vaddr(f->esp + 20) || is_kernel_vaddr(f->esp + 24) || is_kernel_vaddr(f->esp + 28)){
        exit(-1);
      }
      read((int)*(uint32_t*)(f->esp + 20), (void*)*(uint32_t*)(f->esp + 24), (unsigned)*((uint32_t*)(f->esp + 28)));
      break;
    case SYS_FIBO:
      printf("fibonacci\n");
      break;
    case SYS_MAX4INT:
      printf("max of four int\n");
      break;
  }

  //thread_exit ();
}
