#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include "lib/user/syscall.h"

/* system call 은 그냥 정리용으로 적어둔 것임. */

void syscall_init (void);

/* terminate Pintos by calling shutdown_power_off()(declared in devices/shutdown.h)
   deadlock 조심. */
void halt(void);

/* terminate current user program, returning status to the kernel.
   if the process's parent waits for it, this is the status that will be returned.
   0 : success, nonzero : errors */
void exit(int status);

/* runs the executable whose name is given cmd_line,
   and returns the new process's program id (pid).
   자식이 실행가능된 것을 알때 까지는 return되어서 안된다. */
pid_t exec(const char* cmd_line);

int wait (pid_t pid);

/* fd로 open된 파일에서 buffer로 size 만큼을 읽어들인다.
   Return : the number of bytes actually read(0 at EOF) or -1(the file could not be read(EOF를 제외한 다른 모든 경우).
   Fd 가 0 인 경우는 input_getc()를 이용하여 키보드로 부터 읽어들인다. */
int read(int fd, void* buffer, unsigned size);

/* 버퍼에서 열린 파일 fd에 크기 바이트를 쓴다. */
int write (int fd, const void *buffer, unsigned size);

/* Opens the file called file.
   Return : fd or -1(not opened).
   0 : STDIN_FILENO,
   1 : STDOUT_FILENO,
   2 : STDERR_FINENO 이는 open의 반환값이 될 수 없으며, filesize(), read(), write(), seek(), tell(), close()의 parameter 로만 사용가능하다.
   When a single file is opened more than once, each open returns a new file descriptor.
   그리고 이 서로 다른 fd는 독립적이다. */
int open(const char* file);

/* fd를 닫는다. 
   프로세스를 종료하면 모두 이 함수를 호출하는 것 처럼 닫힌다. */
void close (int fd);

/* file 이라는 이름의 파일을 만든다. 처음의 크기는 initail_size 이다.
   이 작업이 open으로 이어지지는 않는다. */
bool create(const char* file, unsigned initial_size);

/* A file may be removed regardless of whether it is open or closed,
   and removing an open file does not close it */
bool remove(const char* file);

/* Changes the next byte to be read or written in open file fd to position,
   expressed in bytes from the beginning of the file. */
void seek (int fd, unsigned position);

/* Returns the position of the next byte to be read or written in open file fd,
   expressed in bytes from the beginning of the file. */
unsigned tell (int fd);

int filesize(int fd);

int sys_filesize(int fd);

int fibonacci(int n);

int max_of_four_int(int a, int b, int c, int d);

typedef int mmpid_t;
mmpid_t mmap(int fd, void* user_page);
void munmap(mmpid_t mapping);

bool sys_chdir(const char *dir);
bool sys_mkdir(const char *dir);
bool readdir (int fd, char *name);
bool isdir (int fd);
int inumber (int fd);


#endif /* userprog/syscall.h */
