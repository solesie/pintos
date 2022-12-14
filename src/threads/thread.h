#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "synch.h"
#include "vm/page.h"
#include <hash.h>

/* Project #3 */
extern bool thread_prior_aging;
/* Idle thread. */
extern struct thread *idle_thread;
/* sema_up에서 thread_yield()는 vtop()을 호출하는데, 이를 사용하려면 paging_init()을 먼저 호출해야 한다.
   thread가 시작되고 나서 yield 되는 것이 옳다고 여긴다. */
extern bool threading_started;

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */


struct mmap_descriptor{
   struct file* file;
   void* starting_page;
};

struct file_descriptor{
   struct file* file;
   /* If FILE is directory, DIR variable is valid(proj5). */
   struct dir* dir;
};


/* A kernel thread or user process. PCB

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */
    
    int wakeup_tick;                    /* 깨어나야 할 tick을 저장한다. */
    int recent_cpu;                     /* 최근에 얼마나 많은 cpu time을 사용했는가.(클수록 priority 낮아짐) */
    int nice;                           /* nice가 클수록 양보하는 정도가 크다.(클수록 priority 낮아짐) */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    struct list child;
    struct list_elem child_elem;
    int exit_status;
    /* wait 하고 나서 exit 되어야 한다.
       다른 프로세스하고 점유를 가지기 위해 싸우는 것이 아니므로 lock이 아닌 semaphore 사용. */
    struct semaphore wait_sema;
    struct semaphore exit_sema;

    bool load_success;
#endif

    struct file_descriptor* fd[128];

#ifdef VM
    struct hash spt;
    struct mmap_descriptor* mmap_d[128];
#endif
    struct dir *cwd;
    struct thread* parent_thread;

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);
/* 실행 중인 스레드를 슬립으로 만듬.
   ticks 때 깨운다. */
void thread_sleep(int64_t ticks);
/* 슬립큐에서 스레드를 깨움 */
void thread_awake(int64_t ticks); 
/*최소 틱을 가진 스레드를 저장/업데이트 */
void update_next_tick_to_awake(int64_t ticks);
/* next_tick_to_awake 반환 */
int64_t get_next_tick_to_awake(void); 
static struct list sleep_queue; /* THREAD_BLOCKED 상태의 스레드를 관리하기 위한 리스트  */
static int64_t next_tick_to_awake; /* sleep_list 에서 대기중인 스레드들의 wakeup_tick 값 중 최소값을 저장 */

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);
/* priority에 대해 내림차순으로 list에 정렬하기 위한 comparator. */
bool thread_priority_comparator(const struct list_elem*, const struct list_elem*, void* aux);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);
void update_load_avg(void);
void increment_running_thread_recent_cpu(void);
void update_all_thread_recent_cpu(void);
void update_all_thread_priority(void);


#endif /* threads/thread.h */
