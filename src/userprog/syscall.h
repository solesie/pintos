#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"

void syscall_init (void);

/* 
 * terminate Pintos by calling shutdown_power_off()(declared in devices/shutdown.h
 * deadlock 조심.
 */
void halt(void);

/*
 * terminate current user program, returning status to the kernel.
 * if the process's parent waits for it, this is the status that will be returned.
 * 0 : success, nonzero : errors
 */
void exit(int status);

/*
 * runs the executable whose name is given cmd_line,
 * and returns the new process's program id (pid).
 * 자식이 실행가능된 것을 알때 까지는 return되어서 안된다.
 */
pid_t exec(const char* cmd_line);

int wait (pid_t pid);

int read(int fd, void* buffer, unsigned size);

int write (int fd, const void *buffer, unsigned size);





#endif /* userprog/syscall.h */
